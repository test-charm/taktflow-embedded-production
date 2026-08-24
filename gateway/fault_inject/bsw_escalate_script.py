"""Standalone subprocess backing /api/test/bsw/e2ereject/cvc (op=escalate).

Runs as a fresh Python process so its CAN sockets behave like the proven
standalone quarantine scripts.

SIL-009 family: the CVC is stopped and fully exited (so its valid 0x100 no
longer resets the RZC E2E window), then `count` corrupted Vehicle_State
frames are injected on 0x100 and the RZC — which E2E-protects 0x100 with Dem
event 5 / DTC 0xE601 — must reject every corrupted frame (never believes the
corrupted torque), latch its E2E supervision, and escalate to a confirmed
DTC 0xE601 broadcast on 0x500 while its own behaviour (heartbeat + motor
status) stays unchanged. The CVC is restored afterwards.

Usage: python3 bsw_escalate_script.py --count 16 --interval-ms 100 --observe-ms 5000 [--no-restart]
Output: one JSON object on stdout.
"""
import argparse
import json
import socket
import struct
import sys
import threading
import time

import can

sys.path.insert(0, "/app")
sys.path.insert(0, "/app/lib")
sys.path.insert(0, "/app/fault_inject")

from lib.dbc_encoder import CanEncoder  # noqa: E402
from fault_inject.bsw_bus_probe import (  # noqa: E402
    _CVC_CONTAINER_CANDIDATES,
    corrupt_e2e_frame,
    open_bus,
)
from fault_inject.scenarios import _docker_client  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--interval-ms", type=int, default=100)
    parser.add_argument("--observe-ms", type=int, default=5000)
    parser.add_argument("--no-restart", action="store_true")
    args = parser.parse_args()

    encoder = CanEncoder()
    try:
        client = _docker_client()
        cvc = None
        for name in _CVC_CONTAINER_CANDIDATES:
            try:
                cvc = client.containers.get(name)
                break
            except Exception:
                cvc = None
        if cvc is None:
            print(json.dumps({"found": False, "busUp": True,
                              "reason": "CVC container not found"}))
            return 1
    except Exception as exc:
        print(json.dumps({"found": False, "busUp": True,
                          "reason": f"docker error: {exc}"}))
        return 1

    # Isolate the CVC so its valid 0x100 does not reset the RZC window.
    # docker stop() only sends SIGTERM; the daemon force-kills after the stop
    # timeout (10s), so block until the container is truly exited.
    cvc.stop()
    for _ in range(60):
        try:
            if client.containers.get(cvc.name).status == "exited":
                break
        except Exception:
            break
        time.sleep(0.5)
    time.sleep(0.5)  # bus quiesce after the last CVC TX

    # Observation window starts only now that the bus is quiet; the raw
    # observer socket is primed in this main thread before the python-can
    # injection socket, which reliably captures the sparse injected / DTC
    # frames in this environment.
    sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind(("vcan0",))
    sock.settimeout(0.05)

    evidence = {"dtc_frames": [], "raw500": 0, "e601": 0,
                "rzc_ok": 0, "rzc_fail": 0,
                "torque": [], "speed": [], "bad100": 0}
    observation_s = 2.0 + args.observe_ms / 1000.0

    def _observer():
        end = time.monotonic() + observation_s
        while time.monotonic() < end:
            try:
                frame = sock.recv(16)
            except socket.timeout:
                continue
            except Exception:
                break
            if len(frame) < 8:
                continue
            fid = struct.unpack("=I", frame[:4])[0] & 0x1FFFFFFF
            dlen = min(frame[4], 8)
            data = frame[8:8 + dlen]
            if fid == 0x500:
                evidence["raw500"] += 1
                try:
                    d = encoder.decode(0x500, bytes(data))
                    num = int(d.get("DTC_Broadcast_Number") or 0)
                    if num == 0xE601:
                        evidence["e601"] += 1
                        occ = d.get("DTC_Broadcast_OccurrenceCount")
                        if not evidence["dtc_frames"] or occ != \
                                evidence["dtc_frames"][-1].get(
                                    "DTC_Broadcast_OccurrenceCount"):
                            evidence["dtc_frames"].append(d)
                except Exception:
                    pass
            elif fid == 0x12:
                if encoder.verify_e2e(0x12, bytes(data)):
                    evidence["rzc_ok"] += 1
                else:
                    evidence["rzc_fail"] += 1
            elif fid == 0x300:
                try:
                    d = encoder.decode(0x300, bytes(data))
                    evidence["torque"].append(
                        int(d.get("Motor_Status_TorqueEcho") or 0))
                    evidence["speed"].append(
                        int(d.get("Motor_Status_MotorSpeed_RPM") or 0))
                except Exception:
                    pass
            elif fid == 0x100 and len(data) >= 2 and \
                    (data[0] & 0x0F) != \
                    (encoder._e2e_data_ids.get(0x100) & 0x0F):
                evidence["bad100"] += 1

    bus = open_bus()
    try:
        obs = threading.Thread(target=_observer, daemon=True)
        obs.start()

        corrupt = 0
        for _ in range(args.count):
            frame = corrupt_e2e_frame(encoder, "Vehicle_State", "dataid")
            bus.send(can.Message(arbitration_id=0x100, data=frame,
                                 is_extended_id=False))
            corrupt += 1
            time.sleep(args.interval_ms / 1000.0)

        time.sleep(max(0.0, args.observe_ms / 1000.0))
        obs.join(timeout=3.0)

        dtc_frames = evidence["dtc_frames"]
        last = dtc_frames[-1] if dtc_frames else {}
        motor_unchanged = (
            not evidence["torque"] or max(evidence["torque"]) == 0
        ) and (not evidence["speed"] or max(evidence["speed"]) == 0)

        result = {
            "found": True,
            "busUp": True,
            "count": corrupt,
            "corruptOnBus": evidence["bad100"],
            "dtcBroadcast": bool(dtc_frames),
            "dtcCode": int(last.get("DTC_Broadcast_Number") or 0),
            "dtcEcuSource": int(last.get("DTC_Broadcast_ECU_Source") or 0),
            "dtcFrames": evidence["e601"],
            "rzcHeartbeatValid": bool(evidence["rzc_ok"] > 0 and
                                      evidence["rzc_fail"] == 0),
            "motorUnchanged": bool(motor_unchanged),
            "motorTorqueMax": max(evidence["torque"], default=None),
            "motorSpeedMax": max(evidence["speed"], default=None),
        }
        print(json.dumps(result))
        return 0
    finally:
        try:
            sock.close()
        except Exception:
            pass
        if cvc is not None:
            try:
                if args.no_restart:
                    cvc.start()
                else:
                    client.containers.get(cvc.name).restart(timeout=5)
                    time.sleep(10.0)
            except Exception:
                pass
        try:
            bus.shutdown()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
