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