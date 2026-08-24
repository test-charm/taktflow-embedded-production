"""Unit tests for the BSW bus-probe module.

The probes are black-box by design — they observe *real* frames on a CAN
bus. These tests emulate the live ECU with a python-can "virtual" bus and
the same DBC-driven encoder (with real E2E) that the SIL firmware uses,
then assert the probe reports the observable bus properties correctly.

Run: python3 -m unittest discover -s gateway/tests -p 'test_bsw_bus_probe.py'
"""
import os
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fault_inject"))

import can
from lib.dbc_encoder import CanEncoder
from bsw_bus_probe import observe_message, probe_live_messages, ftti_estop

CHANNEL = "bsw-probe-test"


class FakeCvc:
    """Emits DBC-encoded CVC frames on a virtual bus (like the SIL ECU)."""

    def __init__(self, encoder: CanEncoder, bus: can.Bus, estop_latch=False):
        self.enc = encoder
        self.bus = bus
        self.estop_latch = estop_latch
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2.0)

    def set_estop(self, active: bool):
        self.estop_latch = active

    def _send(self, name: str, signals: dict):
        data = self.enc.encode(name, signals)
        id_ = self.enc.get_id(name)
        self.bus.send(can.Message(arbitration_id=id_, data=data,
                                  is_extended_id=False))

    def _run(self):
        next_vs = next_hb = time.monotonic()
        while not self._stop.is_set():
            now = time.monotonic()
            if now >= next_vs:
                self._send("Vehicle_State", {"Vehicle_State_Mode": 1,
                                             "Vehicle_State_FaultMask": 0,
                                             "Vehicle_State_TorqueLimit": 50,
                                             "Vehicle_State_SpeedLimit": 10})
                next_vs += 0.01
            if now >= next_hb:
                self._send("CVC_Heartbeat", {"CVC_Heartbeat_ECU_ID": 1,
                                             "CVC_Heartbeat_OperatingMode": 1,
                                             "CVC_Heartbeat_FaultStatus": 0})
                next_hb += 0.05
            if self.estop_latch:
                self._send("EStop_Broadcast", {"EStop_Broadcast_Active": 1,
                                               "EStop_Broadcast_Source": 1})
            time.sleep(0.002)


class TestObserveMessage(unittest.TestCase):

    def setUp(self):
        self.enc = CanEncoder()
        self.tx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        self.rx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        self.fake = FakeCvc(self.enc, self.tx_bus)
        self.fake.start()
        time.sleep(0.05)

    def tearDown(self):
        self.fake.stop()
        try:
            self.tx_bus.shutdown()
        except Exception:
            pass
        try:
            self.rx_bus.shutdown()
        except Exception:
            pass

    def test_vehicle_state_observed_true_end_to_end(self):
        state = observe_message(self.rx_bus, self.enc, "Vehicle_State",
                                window_ms=250, min_frames=3)
        self.assertTrue(state["found"])
        self.assertTrue(state["busUp"])
        self.assertEqual(state["target"], "Vehicle_State")
        self.assertEqual(state["canId"], 0x100)
        self.assertEqual(state["dlc"], 6)
        self.assertTrue(state["dlcOk"])
        self.assertEqual(state["cycleMs"], 10)
        self.assertTrue(state["cycleOk"])
        self.assertTrue(state["e2e"])
        self.assertEqual(state["dataId"], 5)
        self.assertTrue(state["dataIdOk"])
        self.assertTrue(state["aliveMonotonic"])
        self.assertTrue(state["crcValid"])
        self.assertEqual(int(state["decoded"]["Vehicle_State_Mode"]), 1)
        self.assertGreaterEqual(state["frames"], 3)

    def test_heartbeat_50ms_cadence(self):
        state = observe_message(self.rx_bus, self.enc, "CVC_Heartbeat",
                                window_ms=300, min_frames=3)
        self.assertTrue(state["found"])
        self.assertEqual(state["cycleMs"], 50)
        self.assertTrue(state["cycleOk"])

    def test_unknown_target_fail_closed(self):
        state = observe_message(self.rx_bus, self.enc, "NoSuch_Msg")
        self.assertFalse(state["found"])
        self.assertTrue(state["busUp"])

    def test_no_frames_fail_closed(self):
        # EStop is not emitted until latched — observing it yields found=false.
        state = observe_message(self.rx_bus, self.enc, "EStop_Broadcast",
                                window_ms=200, min_frames=3)
        self.assertFalse(state["found"])
        self.assertTrue(state["busUp"])


class TestProbeLiveMessages(unittest.TestCase):

    def test_multiple_targets(self):
        enc = CanEncoder()
        tx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        rx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        fake = FakeCvc(enc, tx_bus)
        fake.start()
        try:
            results = probe_live_messages(enc, ["Vehicle_State", "NoSuch"],
                                          window_ms=250, min_frames=3, bus=rx_bus)
            self.assertEqual(len(results), 2)
            self.assertTrue(results[0]["found"])
            self.assertFalse(results[1]["found"])
        finally:
            fake.stop()
            tx_bus.shutdown()
            rx_bus.shutdown()


class TestE2eCorruptionDetection(unittest.TestCase):
    """The probe must catch corrupted E2E on the bus (like SIL-009)."""

    def test_identical_alive_and_bad_crc_reported(self):
        enc = CanEncoder()
        tx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        rx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")

        # Three frames with a static alive counter and corrupted CRC.
        signals = {"Vehicle_State_Mode": 1, "Vehicle_State_FaultMask": 0,
                   "Vehicle_State_TorqueLimit": 50, "Vehicle_State_SpeedLimit": 10}
        for _ in range(3):
            data = enc.encode("Vehicle_State", signals,
                              corrupt_crc=True, replay_counter=True)
            tx_bus.send(can.Message(arbitration_id=0x100, data=data,
                                    is_extended_id=False))
            time.sleep(0.02)

        state = observe_message(rx_bus, enc, "Vehicle_State",
                                window_ms=150, min_frames=3)
        self.assertTrue(state["found"])
        self.assertFalse(state["aliveMonotonic"])
        self.assertFalse(state["crcValid"])


class TestFttiEstop(unittest.TestCase):

    def test_estop_latency_within_budget(self):
        enc = CanEncoder()
        tx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        rx_bus = can.interface.Bus(channel=CHANNEL, interface="virtual")
        fake = FakeCvc(enc, tx_bus)
        fake.start()

        def inject():
            fake.set_estop(True)

        def clear():
            fake.set_estop(False)

        try:
            state = ftti_estop(enc, budget_ms=200, restart_cvc=False,
                               inject=inject, clear=clear, bus=rx_bus)
            self.assertTrue(state["found"])
            self.assertTrue(state["busUp"])
            self.assertTrue(state["estopFrameSeen"])
            self.assertIsNotNone(state["latencyMs"])
            self.assertLessEqual(state["latencyMs"], 500)
            self.assertTrue(state["withinBudget"])
            self.assertFalse(state["cvcRestarted"])
        finally:
            fake.stop()
            tx_bus.shutdown()
            rx_bus.shutdown()


if __name__ == "__main__":
    unittest.main()