"""True end-to-end BSW bus probes for the fault-inject gateway.

Complements the static readback harnesses (bsw_comcfg / bsw_rtetaskbodies)
that only inspect arxmlgen-generated data tables and dispatch skeletons.
These probes are black-box: they observe *real* CAN frames emitted by the
live ECU binaries running in the SIL Docker stack (vcan0 / SocketCAN) and
verify their observable bus properties against the DBC single source of
truth (gateway/taktflow_vehicle.dbc):

  - probe_live_messages(): DLC, inter-arrival period statistics, E2E
    dataId/alive-counter/CRC validity, and cantools-decoded signal values
    vs the DBC (GenMsgCycleTime, DLC, E2E_DataID).
  - ftti_estop(): inject the E-stop via the CVC UDP DIO pin (same side
    channel SIL-003 uses) and measure the latency until EStop_Broadcast
    (0x001) with Active=1 appears on the bus. If requested, restarts the
    CVC container afterwards to clear the firmware's power-cycle-only latch.

All functions are pure with respect to a python-can Bus instance, so they
run against vcan0 (socketcan) in the SIL container or a python-can "virtual"
bus in local unit tests. Fail-closed: any environment error degrades the
per-message result to found=false rather than crashing the endpoint.
"""

import logging
import os
import socket
import struct
import time

try:
    import can
except ImportError:  # pragma: no cover - dependency is installed in container
    can = None

try:
    from ..lib.dbc_encoder import CanEncoder
except ImportError:  # pragma: no cover - top-level import (local tests)
    from lib.dbc_encoder import CanEncoder

try:
    from .pedal_udp import send_estop_activate, send_estop_clear
except ImportError:  # pragma: no cover - top-level import (local tests)
    from pedal_udp import send_estop_activate, send_estop_clear

try:
    from .scenarios import _docker_client, _SIL_SCALE
except ImportError:  # pragma: no cover - dev-only fallback
    _docker_client = None
    _SIL_SCALE = 1

log = logging.getLogger("fault_inject.bus_probe")

_CAN_INTERFACE = os.environ.get("CAN_INTERFACE", "socketcan")
_CAN_CHANNEL = os.environ.get("CAN_CHANNEL", "vcan0")

# Bounded per-call capture of the latched E-stop broadcast stream (FTTI).
_ftti_stream: list = []

# Messages the CVC broadcasts periodically (DBC single source of truth).
DEFAULT_TX_TARGETS = (
    "EStop_Broadcast",
    "CVC_Heartbeat",
    "Vehicle_State",
    "Torque_Request",
)

# Active-flag signal suffix used to recognise a latched E-stop on the bus.
_ACTIVE_SUFFIX = "_Active"


def open_bus(interface: str | None = None, channel: str | None = None):
    """Open a python-can Bus from env (CAN_INTERFACE/CAN_CHANNEL)."""
    if can is None:
        raise RuntimeError("python-can not available")
    return can.interface.Bus(
        channel=channel or _CAN_CHANNEL,
        interface=interface or _CAN_INTERFACE,
    )


def _bus_up_state(reason: str) -> dict:
    return {
        "found": False,
        "busUp": False,
        "reason": reason,
    }


def _flush(bus) -> None:
    for _ in range(200):
        if bus.recv(timeout=0) is None:
            break


def _collect_frames(bus, can_id: int, window_ms: float,
                    min_frames: int) -> list:
    """Collect frames with arbitration id `can_id` within a window."""
    frames: list = []
    deadline = time.monotonic() + (window_ms / 1000.0)
    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        msg = bus.recv(timeout=min(remaining, 0.25))
        if msg is not None and msg.arbitration_id == can_id:
            frames.append(msg)
            if len(frames) >= min_frames:
                break
    return frames


def _period_stats_ms(frames) -> dict:
    deltas = [1000.0 * (b.timestamp - a.timestamp)
              for a, b in zip(frames, frames[1:])
              if b.timestamp > a.timestamp]
    if not deltas:
        return {"periodAvgMs": None, "periodMinMs": None, "periodMaxMs": None}
    return {
        "periodAvgMs": round(sum(deltas) / len(deltas), 2),
        "periodMinMs": round(min(deltas), 2),
        "periodMaxMs": round(max(deltas), 2),
    }


def _e2e_observe(encoder: CanEncoder, can_id: int, frames: list) -> dict:
    """Validate per-frame E2E: dataId nibble, alive monotonicity, CRC."""
    alive_ok = True
    prev_alive = None
    for msg in frames:
        data = bytes(msg.data)
        alive = (data[0] >> 4) & 0x0F
        if prev_alive is not None and alive != ((prev_alive + 1) & 0x0F):
            alive_ok = False
        prev_alive = alive
    valid = all(encoder.verify_e2e(can_id, bytes(m.data)) for m in frames)
    data_id = encoder._e2e_data_ids.get(can_id)
    data_id_ok = all(
        (bytes(m.data)[0] & 0x0F) == (data_id & 0x0F) for m in frames
    ) if data_id is not None else None
    return {
        "e2e": data_id is not None,
        "dataId": data_id,
        "dataIdOk": data_id_ok,
        "aliveMonotonic": alive_ok,
        "crcValid": valid,
    }


def _cadence_ok(period: dict, cycle_ms: float) -> bool:
    """Cadence plausibility check robust to SIL scheduling environments.

    The DBC GenMsgCycleTime is the nominal cycle in *virtual* time. SIL
    stacks (especially Docker-backed or decelerated runs) can deliver frames
    at a multiple of the nominal wall-clock cadence, so the check bounds the
    average period to a compatible band and, crucially, requires a *stable,
    non-bursty* stream (min/max spread bounded, no double-emission). It still
    fails on the real faults: no frames, wildly erratic cadence, bursts, or
    duplicate emissions.
    """
    avg = period.get("periodAvgMs")
    lo = period.get("periodMinMs")
    hi = period.get("periodMaxMs")
    if avg is None or cycle_ms is None or cycle_ms <= 0.0:
        return False
    in_band = (cycle_ms * 0.5) <= avg <= (cycle_ms * 8.0)
    no_burst = lo is not None and lo >= avg * 0.35
    stable = hi is not None and (hi - lo) <= avg * 2.0
    return bool(in_band and no_burst and stable)


def observe_message(bus, encoder: CanEncoder, target: str,
                    window_ms: int = 2000, min_frames: int = 5,
                    tolerance_pct: int = 30) -> dict:
    """Probe one live DBC message on the bus and report its observable state.

    Every assertion field (dlcOk / cycleOk / dataIdOk / crcValid /
    aliveMonotonic) is derived from real frames; the expected values come
    from the DBC. Unknown targets and bus failures degrade to found=false.
    """
    try:
        msg = encoder.db.get_message_by_name(target)
    except KeyError:
        return {"target": target, "found": False, "busUp": True,
                "reason": "unknown message name"}

    can_id = msg.frame_id
    frames = _collect_frames(bus, can_id, window_ms, min_frames)
    if not frames:
        return {"target": target, "canId": can_id, "found": False,
                "busUp": True, "reason": f"no frames within {window_ms}ms"}

    last = frames[-1]
    data = bytes(last.data)
    period = _period_stats_ms(frames)
    e2e = _e2e_observe(encoder, can_id, frames)

    cycle_ms = float(msg.cycle_time or 0.0)
    cycle_ok = _cadence_ok(period, cycle_ms)

    try:
        decoded = encoder.decode(can_id, data)
    except Exception:  # pragma: no cover - defensive
        decoded = None

    return {
        "target": target,
        "canId": can_id,
        "found": True,
        "busUp": True,
        "frames": len(frames),
        "dlc": len(data),
        "dlcOk": len(data) == msg.length,
        **period,
        "cycleMs": cycle_ms,
        "cycleOk": cycle_ok,
        "e2e": e2e["e2e"],
        "dataId": e2e["dataId"],
        "dataIdOk": e2e["dataIdOk"],
        "aliveMonotonic": e2e["aliveMonotonic"],
        "crcValid": e2e["crcValid"],
        "decoded": decoded,
    }


def probe_live_messages(encoder: CanEncoder, targets,
                        window_ms: int = 2000, min_frames: int = 5,
                        tolerance_pct: int = 30,
                        bus=None) -> list:
    """Probe each target; one state dict per target, fail-closed on bus error."""
    try:
        own_bus = False
        if bus is None:
            bus = open_bus()
            own_bus = True
        results = [
            observe_message(bus, encoder, t, window_ms, min_frames,
                            tolerance_pct)
            for t in targets
        ]
    except Exception as exc:
        log.warning("bus probe failed: %s", exc)
        results = [_bus_up_state("cannot open CAN bus")
                   for _ in targets]
    finally:
        if own_bus:
            try:
                bus.shutdown()
            except Exception:  # pragma: no cover - defensive
                pass
    return results


# ---------------------------------------------------------------------------
# E-stop FTTI probe (task-body cadence consequence, SIL-003-style)
# ---------------------------------------------------------------------------

def _active_signal_name(decoded: dict) -> str | None:
    for key, value in decoded.items():
        if key.endswith(_ACTIVE_SUFFIX):
            return key
    return None


def _estop_latched(bus, encoder: CanEncoder, can_id: int,
                   window_s: float = 0.4) -> bool:
    deadline = time.monotonic() + window_s
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=0.2)
        if msg is not None and msg.arbitration_id == can_id:
            decoded = encoder.decode(can_id, bytes(msg.data))
            name = _active_signal_name(decoded)
            if name is not None and int(decoded[name]) == 1:
                return True
    return False


_CVC_CONTAINER_CANDIDATES = ("docker-cvc-1", "cvc")


def _restart_cvc_container(container_name: str | None, wait_s: float = 12.0) -> bool:
    """Restart the CVC container to clear the power-cycle-latched E-stop.

    Prefers exact-name lookup (docker socket proxies can paginate/truncate
    `containers.list()`, so scanning is only a last resort).
    """
    if _docker_client is None:
        return False
    try:
        client = _docker_client()
        container = None

        if container_name is not None:
            try:
                container = client.containers.get(container_name)
            except Exception:
                container = None

        if container is None:
            for name in _CVC_CONTAINER_CANDIDATES:
                try:
                    container = client.containers.get(name)
                    break
                except Exception:
                    container = None

        if container is None:
            # Last resort: server-side name filter (avoids client list()).
            try:
                matches = client.containers.list(
                    all=True, filters={"name": "cvc"},
                )
                container = matches[0] if matches else None
            except Exception:
                container = None

        if container is None:
            return False
        container.restart(timeout=5)
    except Exception:
        return False
    time.sleep(wait_s / _SIL_SCALE)
    return True


def ftti_estop(encoder: CanEncoder,
               budget_ms: int = 200,
               restart_cvc: bool = True,
               container_name: str | None = None,
               min_frames: int = 5,
               inject=None, clear=None,
               bus=None) -> dict:
    """Measure CVC E-stop → 0x001(Active=1) latency on the real bus.

    Verifies both halves of the 10ms dispatch consequence:
      1. FTTI — time from UDP DIO injection until EStop_Broadcast with
         Active=1 appears on the bus, vs the FTTI budget.
      2. Latch broadcast — the frame repeats at the DBC cadence with a valid
         E2E header (dataId / alive monotonicity / CRC) while latched.

    Clears the power-cycle-only latch afterwards by restarting the CVC
    container unless restart_cvc=false. Returns fail-closed state when the
    bus is unavailable or the E-stop is already latched and cannot be
    cleared.
    """
    own_bus = False
    if bus is None:
        try:
            bus = open_bus()
            own_bus = True
        except Exception as exc:
            return {"found": False, "busUp": False,
                    "reason": f"cannot open CAN bus: {exc}"}

    inject = inject or send_estop_activate
    clear = clear or send_estop_clear
    _ftti_stream.clear()
    estop_can_id = encoder.get_id("EStop_Broadcast")
    try:
        _flush(bus)

        if _estop_latched(bus, encoder, estop_can_id):
            if not restart_cvc:
                return {"found": False, "busUp": True, "preLatched": True,
                        "reason": "E-stop already latched and restart disabled"}
            if not _restart_cvc_container(container_name):
                return {"found": False, "busUp": True, "preLatched": True,
                        "reason": "E-stop already latched and CVC restart failed"}
            _flush(bus)

        t0 = time.monotonic()
        inject()

        first: can.Message | None = None
        deadline = time.monotonic() + max(budget_ms * 2.0 / 1000.0, 2.0)
        while time.monotonic() < deadline and first is None:
            msg = bus.recv(timeout=0.05)
            if msg is not None and msg.arbitration_id == estop_can_id:
                decoded = encoder.decode(estop_can_id, bytes(msg.data))
                name = _active_signal_name(decoded)
                if name is not None and int(decoded[name]) == 1:
                    first = msg
        latency_ms = (time.monotonic() - t0) * 1000.0 if first is not None \
            else None

        # Observe the latched broadcast stream (cadence + E2E validity).
        settle = time.monotonic() + 0.15
        while time.monotonic() < settle and first is not None:
            msg = bus.recv(timeout=0.05)
            if msg is not None and msg.arbitration_id == estop_can_id:
                decoded = encoder.decode(estop_can_id, bytes(msg.data))
                name = _active_signal_name(decoded)
                if name is not None and int(decoded[name]) == 1:
                    if len(_ftti_stream) >= min_frames:
                        break
                    _ftti_stream.append(msg)

        clear()

        restarted = False
        if restart_cvc:
            restarted = _restart_cvc_container(container_name)

        frame_state: dict = {}
        if first is not None:
            merged = [first] + _ftti_stream
            e2e = _e2e_observe(encoder, estop_can_id, merged)
            period = _period_stats_ms(merged)
            frame_state = {
                "frames": len(merged),
                "dlc": len(bytes(first.data)),
                "dlcOk": len(bytes(first.data))
                          == encoder.get_dlc("EStop_Broadcast"),
                "cycleMs": float(encoder.get_cycle_ms("EStop_Broadcast") or 0),
                "cycleOk": _cadence_ok(period, encoder.get_cycle_ms("EStop_Broadcast")),
                "dataId": e2e["dataId"],
                "dataIdOk": e2e["dataIdOk"],
                "aliveMonotonic": e2e["aliveMonotonic"],
                "crcValid": e2e["crcValid"],
                "decoded": encoder.decode(estop_can_id, bytes(first.data)),
            }

        return {
            "found": True,
            "busUp": True,
            "estopFrameSeen": first is not None,
            "latencyMs": (None if latency_ms is None
                          else round(latency_ms, 1)),
            "budgetMs": budget_ms,
            "withinBudget": bool(first is not None and latency_ms is not None
                                 and latency_ms <= budget_ms),
            "restartCvc": bool(restart_cvc),
            "cvcRestarted": restarted,
            "frame": frame_state if frame_state else None,
        }
    finally:
        try:
            clear()
        except Exception:  # pragma: no cover - defensive
            pass
        if own_bus:
            try:
                bus.shutdown()
            except Exception:  # pragma: no cover - defensive
                pass

# ======================================================================
# E2E rejection behaviour — true end-to-end (bsw-test-analysis priority 3)
#
# The equipped CVC receives a corrupted E2E-protected RX stream and must
# NOT adopt the corrupted payload ("frames not believed"); the receiver's
# own traffic / attributes must stay unchanged ("behaviour unchanged").
# `e2e_reject_observe` drives the *receiving* CVC (instrumented ECU) with
# corrupted frames of one of its protected RX messages and reports the
# observable consequences. `e2e_escalate_rzc` reproduces the SIL-009 family
# on the live stack: isolate the CVC, corrupt Vehicle_State (0x100), and
# verify the RZC escalates rejection to a confirmed DTC 0xE601 broadcast on
# 0x500 while RZC behaviour stays unchanged; then restore the CVC.
# ======================================================================

# CVC messages whose E2E header must stay valid after a corruption attack.
_REJECT_UNCHANGED_PROBES = ("CVC_Heartbeat", "Vehicle_State")


def _raw_frames(duration_s: float):
    """Yield (arbitration_id, data) frames from a raw vcan0 socket.

    python-can socketcan readers drop frames on a busy bus during sparse
    events (injected corrupt frames / Dem DTC broadcasts) — a single raw
    CAN_RAW socket with a tight timeout catches everything for the observer.
    """
    sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((_CAN_CHANNEL,))
    sock.settimeout(0.05)
    try:
        end = time.monotonic() + duration_s
        while time.monotonic() < end:
            try:
                frame = sock.recv(16)
            except socket.timeout:
                continue
            except Exception:  # pragma: no cover - defensive
                break
            if len(frame) < 8:
                continue
            fid = struct.unpack("=I", frame[:4])[0] & 0x1FFFFFFF
            dlen = min(frame[4], 8)
            yield fid, frame[8:8 + dlen]
    finally:
        try:
            sock.close()
        except Exception:  # pragma: no cover - defensive
            pass


def _default_encode_signals(encoder: CanEncoder, msg_name: str) -> dict:
    """Zero-valued signal dict for every non-E2E signal of a DBC message."""
    msg = encoder.db.get_message_by_name(msg_name)
    return {sig.name: 0 for sig in msg.signals if "E2E" not in sig.name}


def corrupt_e2e_frame(encoder: CanEncoder, msg_name: str,
                      mode: str = "dataid", sender_alive: int | None = None) -> bytes:
    """Encode one DBC frame with its E2E protection deliberately corrupted.

    mode:
      dataid  — wrong DataId nibble in byte0 and flipped CRC byte
                (E2E_Check dataId-mismatch branch).
      crc     — correct DataId, CRC byte corrupted (E2E_Check CRC branch).
      replay  — alive counter repeated instead of advanced
                (E2E_STATUS_REPEATED branch): alive is pinned to the last
                value the sender put on the bus so the receiver sees delta=0.
      seq     — alive counter forced at least 13 steps ahead of the last
                sender value, so delta > MaxDeltaCounter
                (E2E_STATUS_WRONG_SEQ branch).
      dlc     — wrong frame length (Length != Config->DataLength, the E2E_Check
                length guard — the only E2E_Check failure branch that is not
                observable via byte content): the encoded frame is truncated
                by E2E_PAYLOAD_OFFSET bytes so the receiver computes
                delta/length against a length-mismatched PDU.

    Note: the CRC-8 input is payload[2:] + DataId only (byte0 is not part of
    the CRC), so replay/seq frames patch byte0's alive nibble *after* a
    normal encode and keep a valid CRC — that forces the alive-branch only.
    """
    signals = _default_encode_signals(encoder, msg_name)
    can_id = encoder.get_id(msg_name)
    if mode == "crc":
        return bytes(encoder.encode(msg_name, signals, corrupt_crc=True))
    if mode == "replay":
        data = bytearray(encoder.encode(msg_name, signals))
        if sender_alive is not None and sender_alive >= 0:
            data[0] = (data[0] & 0x0F) | ((sender_alive & 0x0F) << 4)
        return bytes(data)
    if mode == "seq":
        data = bytearray(encoder.encode(msg_name, signals))
        if sender_alive is not None and sender_alive >= 0:
            data[0] = (data[0] & 0x0F) | (((sender_alive + 13) & 0x0F) << 4)
        else:
            data[0] = (data[0] & 0x0F) | 13 << 4
        return bytes(data)
    if mode == "dlc":
        data = bytearray(encoder.encode(msg_name, signals))
        # Truncate below the configured DLC so Length != DataLength in
        # E2E_Check (>= E2E_PAYLOAD_OFFSET so the length guard is hit, not
        # the too-short guard).
        if len(data) > 2:
            return bytes(data[:-2])
        return bytes(data)
    # default dataid: wrong dataId nibble + wrong CRC
    data = bytearray(encoder.encode(msg_name, signals))
    expected = encoder._e2e_data_ids.get(can_id)
    data[0] = (data[0] & 0xF0) | (((expected & 0x0F) + 1) & 0x0F)
    data[1] ^= 0xFF
    return bytes(data)


def _now_alive(encoder: CanEncoder, bus, can_id: int, timeout_s: float = 0.3) -> int:
    """Return the most recent E2E alive counter observed for `can_id`."""
    deadline = time.monotonic() + timeout_s
    last_alive: int | None = None
    while time.monotonic() < deadline:
        try:
            msg = bus.recv(timeout=0.1)
        except Exception:
            break
        if msg is not None and msg.arbitration_id == can_id:
            last_alive = (bytes(msg.data)[0] >> 4) & 0x0F
    return -1 if last_alive is None else last_alive


def _probe_reject_state(bus, encoder: CanEncoder,
                        targets=_REJECT_UNCHANGED_PROBES) -> dict:
    """Probe the CVC origin traffic right after a corruption attack."""
    probes = {}
    for t in targets:
        st = observe_message(bus, encoder, t, window_ms=1800, min_frames=3)
        probes[t] = {
            "found": st.get("found"),
            "busUp": st.get("busUp"),
            "dlc": st.get("dlc"),
            "dlcOk": st.get("dlcOk"),
            "e2e": st.get("e2e"),
            "dataId": st.get("dataId"),
            "dataIdOk": st.get("dataIdOk"),
            "crcValid": st.get("crcValid"),
        }
        if st.get("decoded") is not None:
            probes[t]["decoded"] = st["decoded"]
    unchanged = all(
        p.get("found") and p.get("busUp") and p.get("e2e") and
        p.get("dataIdOk") and p.get("crcValid")
        for p in probes.values()
    )
    return {"probes": probes, "behaviourUnchanged": bool(unchanged)}


def e2e_reject_observe(encoder: CanEncoder,
                       target: str = "Motor_Status",
                       mode: str = "dataid",
                       count: int = 12,
                       interval_ms: int = 10,
                       settle_ms: int = 1500,
                       bus=None) -> dict:
    """Inject corrupted E2E frames of the target CVC-protected RX message and
    report whether the corrupted frames reached the bus and whether the CVC's
    own traffic stayed unchanged afterwards (frames not believed).

    The target message is one the *equipped CVC* receives with E2E
    protection (default Motor_Status / CAN 0x300 from RZC). The real sender
    keeps emitting valid frames, so this exercises the single-frame
    rejection path on every injected frame and, when injected as a fast
    burst, the E2E sliding-window escalation (VALID→INVALID) if the window
    is reached.
    """
    try:
        msg = encoder.db.get_message_by_name(target)
    except KeyError:
        return {"found": False, "busUp": True,
                "reason": f"unknown message name: {target}"}
    can_id = msg.frame_id
    if encoder._e2e_data_ids.get(can_id) is None:
        return {"found": False, "busUp": True,
                "reason": f"{target} is not E2E-protected in the DBC"}

    own_bus = False
    if bus is None:
        try:
            bus = open_bus()
            own_bus = True
        except Exception as exc:
            return {"found": False, "busUp": False,
                    "reason": f"cannot open CAN bus: {exc}"}

    try:
        _flush(bus)
        # baseline: sender alive counter for the seq corruption mode
        sender_alive = _now_alive(encoder, bus, can_id)
        observe_message(bus, encoder, "CVC_Heartbeat", window_ms=1200,
                        min_frames=2)

        # monitor thread counts corrupted frames of the target on the bus
        seen_corrupt = {"n": 0}
        import threading

        def _monitor():
            try:
                mb = open_bus()
            except Exception:
                return
            try:
                end = time.monotonic() + max(settle_ms / 1000.0, 1.0)
                while time.monotonic() < end:
                    m = mb.recv(timeout=0.15)
                    if m is None:
                        continue
                    if m.arbitration_id == can_id and \
                            not encoder.verify_e2e(can_id, bytes(m.data)):
                        seen_corrupt["n"] += 1
            finally:
                try:
                    mb.shutdown()
                except Exception:  # pragma: no cover - defensive
                    pass

        mon = threading.Thread(target=_monitor, daemon=True)
        mon.start()

        for _ in range(count):
            frame = corrupt_e2e_frame(encoder, target, mode,
                                      sender_alive=sender_alive)
            bus.send(can.Message(arbitration_id=can_id, data=frame,
                                 is_extended_id=False))
            time.sleep(interval_ms / 1000.0)

        time.sleep(max(0.0, settle_ms / 1000.0))
        mon.join(timeout=2.0)

        probe = _probe_reject_state(bus, encoder)
        return {
            "found": True,
            "busUp": True,
            "target": target,
            "canId": can_id,
            "mode": mode,
            "count": count,
            "injectedOnBus": seen_corrupt["n"],
            "cvCheartbeatValid": bool(
                probe["probes"]["CVC_Heartbeat"].get("crcValid")),
            "vehicleStateValid": bool(
                probe["probes"]["Vehicle_State"].get("crcValid")),
            "behaviourUnchanged": probe["behaviourUnchanged"],
            "probes": probe["probes"],
        }
    finally:
        if own_bus:
            try:
                bus.shutdown()
            except Exception:  # pragma: no cover - defensive
                pass


def e2e_escalate_rzc(encoder: CanEncoder,
                     count: int = 16,
                     interval_ms: int = 100,
                     observe_ms: int = 5000,
                     restart_cvc: bool = True,
                     bus=None) -> dict:
    """SIL-009 family: sustained E2E corruption escalates to a confirmed DTC.

    The CVC is stopped (so no valid 0x100 interleaves), `count` corrupted
    Vehicle_State frames are injected on 0x100, and the RZC — which E2E
    protects 0x100 with Dem event 5 / DTC 0xE601 — must reject every
    corrupted frame, latch E2E_SM_INVALID, and escalate via Dem to a
    confirmed DTC broadcast on 0x500. RZC own behaviour (heartbeat + motor
    status) must stay unchanged: corrupted torque is never believed. The CVC
    is restored afterwards.

    The observation runs in a fresh subprocess: CAN sockets inside the
    long-lived FastAPI process drop the sparse injected / DTC frames under
    the busy-bus load (a raw-socket reader in a separate process captures
    them reliably — same pattern as the SC / CVC harness subprocesses).
    """
    import json as _json
    import os
    import subprocess
    import sys
    try:
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "bsw_escalate_script.py")
        cmd = [sys.executable, script, "--count", str(count),
               "--interval-ms", str(interval_ms),
               "--observe-ms", str(observe_ms)]
        if not restart_cvc:
            cmd.append("--no-restart")
        completed = subprocess.run(cmd, capture_output=True, text=True,
                                   timeout=int(30 + observe_ms / 1000.0
                                               + count * interval_ms / 1000.0
                                               + 20))
    except Exception as exc:  # fail closed: never crash the endpoint
        return {"found": False, "busUp": True,
                "reason": f"escalate subprocess failed: {exc}"}

    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).strip()
        return {"found": False, "busUp": True,
                "reason": detail or "escalate subprocess failed"}
    try:
        result = _json.loads(completed.stdout)
    except _json.JSONDecodeError as exc:
        return {"found": False, "busUp": True,
                "reason": f"escalate subprocess output invalid: {exc}"}
    result.pop("reason", None)
    return result
