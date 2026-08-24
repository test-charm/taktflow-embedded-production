"""Taktflow Fault Injection API — FastAPI server for triggering CAN
fault scenarios during demo.

Endpoints:
    POST /api/fault/scenario/{name}     — trigger a scenario by name
    POST /api/fault/reset               — reset all actuators to safe idle
    GET  /api/fault/scenarios           — list available scenarios
    GET  /api/fault/health             — health check
    POST /api/fault/control/acquire    — acquire 5-min controller lock
    POST /api/fault/control/release    — release controller lock early
    GET  /api/fault/control/status     — current lock state

Runs on FAULT_PORT (default 8091).
"""

import json
import logging
import os
import shutil
import subprocess
import threading
import time

import paho.mqtt.client as paho_mqtt
import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .scenarios import (
    SCENARIOS, reset as reset_scenario, set_mqtt_client,
    _get_bus, _send, _brake_frame, _steer_frame,
    CAN_BRAKE_COMMAND, CAN_STEER_COMMAND,
)

from .bsw_bus_probe import (
    DEFAULT_TX_TARGETS,
    ftti_estop,
    probe_live_messages,
)

try:
    from ..lib.dbc_encoder import CanEncoder
except ImportError:  # pragma: no cover
    from lib.dbc_encoder import CanEncoder
from .test_runner import DashboardTestRunner

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [FAULT] %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("fault_inject")

# API key for mutating endpoints (empty = no auth, dev mode)
FAULT_API_KEY = os.environ.get("FAULT_API_KEY", "")

# MQTT client for publishing reset/command messages
_mqtt_client: paho_mqtt.Client | None = None

# Idle command TX — paused during active fault scenarios
_idle_paused = False
# Unix timestamp until which cruise remains suppressed after a scenario
# completes. Lets the observation window see the scenario's pedal input
# without cruise overwriting it on the very next tick.
_idle_paused_until = 0.0
# Seconds to keep cruise paused after a scenario returns. Must exceed
# the longest scenario observe_sec (battery_low = 30s).
SCENARIO_POST_PAUSE_SEC = 35.0
IDLE_CMD_INTERVAL = float(os.environ.get("IDLE_CMD_INTERVAL", "0.05"))  # 50ms

# ---------------------------------------------------------------------------
# Controller lock — single in-memory lock for fault-inject control
# ---------------------------------------------------------------------------
LOCK_DURATION_SEC = int(os.environ.get("LOCK_DURATION_SEC", "900"))  # 15 min default

_control_lock = {
    "client_id": None,      # str | None — who holds it
    "expires_at": 0.0,      # unix timestamp
    "acquired_at": 0.0,     # when lock was taken
}
_lock_mu = threading.Lock()


class ClientIdBody(BaseModel):
    client_id: str


class TestRunBody(BaseModel):
    tests: list[str] | None = None


class CvcPedalTorqueBody(BaseModel):
    sensor1Pct: int
    sensor2Pct: int
    vehicleState: str | None = None   # None → use server-side stored state
    cycles: int | None = None         # None → use server-side stored state
    spiFaultSensor: int | None = None # None → use server-side stored state
    recoverSensor1Pct: int | None = None  # recovery phase: sensor 1 percentage
    recoverSensor2Pct: int | None = None  # recovery phase: sensor 2 percentage
    recoverCycles: int | None = None      # recovery phase: cycles to run
    ditherAmplitude: int | None = None    # 0=no dither (stuck-test), None=default 16
    bridgeRx: bool = False                # call Swc_CvcCom_BridgeRxToRte after cycle
    getPosition: bool = False             # call Swc_Pedal_GetPosition and report
    rxBrakeFault: int | None = None       # Com shadow: Brake_Status.BrakeFaultStatus
    rxMotorCutoff: int | None = None      # Com shadow: Motor_Cutoff_Req.RequestType
    rxBattery: int | None = None          # Com shadow: Battery_Status.Level
    rxSteeringFault: int | None = None    # Com shadow: Steering_Status.SteerFaultStatus
    rxMotorFault: int | None = None       # Com shadow: Motor_Status.MotorFaultStatus
    rxScRelay: int | None = None          # Com shadow: SC_Status.RelayEnergized
    rxFzcAlive: int | None = None         # Com shadow: FZC_Heartbeat E2E alive
    rxzAlive: int | None = None           # Com shadow: RZC_Heartbeat E2E alive


class VehicleStatePhase(BaseModel):
    """One phase of the CVC vehicle-state harness script."""
    cycles: int = 0
    selfTestPass: bool = False
    estop: bool = False
    scRelayEnergized: bool = True      # 1 = relay energized / OK
    fzcComm: int = 0                   # 0=OK, 1=TIMEOUT
    rzcComm: int = 0
    pedalFault: bool = False
    motorCutoff: bool = False
    brakeFault: bool = False
    steeringFault: bool = False
    batteryStatus: int = 2             # 2=NORMAL
    motorFaultRzc: bool = False
    motorSpeed: int = 0
    torqueRequest: int = 0
    pedalPosition: int = 0
    pedalFaultDual: bool = False       # inject EVT_PEDAL_FAULT_DUAL (no prod trigger)
    comBrakeFault: int = -1            # -1=follow RTE brake_fault, 0/1=override Com shadow
    comMotorCutoff: int = -1           # -1=follow RTE motor_cutoff, 0/1=override Com shadow
    motorPduTimedOut: bool = False     # Motor_Status PDU quality = TIMED_OUT


class CvcVehicleStateSetupBody(BaseModel):
    phases: list[VehicleStatePhase] = []


class CvcVehicleStateRunBody(BaseModel):
    phases: list[VehicleStatePhase] | None = None  # stimulus phases, appended after server-side stored precondition


class CvcEStopPhase(BaseModel):
    """One phase of the CVC E-stop harness script."""
    cycles: int = 0
    pin: int = 0              # 0=LOW (released), 1=HIGH (pressed)
    readFail: bool = False    # IoHwAb_ReadEStop returns E_NOT_OK (fail-safe active)
    skipInit: bool = False    # skip Swc_EStop_Init (uninitialized no-op guard test)


class CvcEStopSetupBody(BaseModel):
    phases: list[CvcEStopPhase] = []


class CvcCvcComPhase(BaseModel):
    """One phase of the CVC CAN-communication harness script.
    Drives Swc_CvcCom_TransmitSchedule() (TX) and optionally
    Swc_CvcCom_BridgeRxToRte() (RX bridge) against injected RTE fault
    signals and Com RX shadows.
    """
    cycles: int = 1                # TransmitSchedule calls
    skipInit: bool = False         # skip Swc_CvcCom_Init (uninitialized guard)
    bridgeRx: bool = False         # call BridgeRxToRte after TX
    vehicleState: int = 1          # 0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN
    # TX fault inputs (RTE → faultMask composition)
    estop: int = 0
    relayKill: int = 1             # 1=energized (OK), 0=killed → 0x02
    motorCutoff: int = 0
    brakeFault: int = 0
    steerFault: int = 0
    pedalFault: int = 0
    fzcComm: int = 0               # 0=OK, 1=TIMEOUT
    rzcComm: int = 0
    torque: int = 0                # CVC_SIG_TORQUE_REQUEST (clamped at 100)
    # RX bridge inputs (Com shadows → RTE fault signals)
    rxBrakeEvent: int = 0
    rxBrakeStatus: int = 0
    rxMotorCutoff: int = 0
    rxScRelay: int = 1             # 1=energized (OK), 0=killed
    rxBattery: int = 0
    rxSteerFault: int = 0
    rxMotorFault: int = 0
    rxFzcAlive: int = 0
    rxRzcAlive: int = 0


class CvcCvcComSetupBody(BaseModel):
    phases: list[CvcCvcComPhase] = []


class CvcCvcComRunBody(BaseModel):
    phases: list[CvcCvcComPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcHeartbeatPhase(BaseModel):
    """One phase of the CVC heartbeat harness script.
    Drives Swc_Heartbeat_Init / MainFunction / RxIndication / ResetCommStatus
    against the real Swc_Heartbeat.c production code.
    """
    cycles: int = 1                # Swc_Heartbeat_MainFunction calls
    skipInit: bool = False         # skip Swc_Heartbeat_Init (uninitialized guard)
    vehicleState: int = 1          # RTE CVC_SIG_VEHICLE_STATE read at TX boundary
    rxEcu: int = 0                 # Swc_Heartbeat_RxIndication arg (0=none, 2=FZC, 3=RZC, else unknown)
    resetComm: bool = False        # call Swc_Heartbeat_ResetCommStatus after cycles


class CvcHeartbeatSetupBody(BaseModel):
    phases: list[CvcHeartbeatPhase] = []


class CvcHeartbeatRunBody(BaseModel):
    phases: list[CvcHeartbeatPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcCanMonitorPhase(BaseModel):
    """One phase of the CVC CAN monitor harness script.
    Drives Swc_CanMonitor_Init / Check / Recovery against the real
    Swc_CanMonitor.c production code.
    """
    cycles: int = 1                # Swc_CanMonitor_Check calls
    skipInit: bool = False         # skip Swc_CanMonitor_Init (uninitialized guard)
    isBusOff: bool = False         # Check isBusOff arg (bus-off detection)
    rxMsgCount: int | None = None  # Check rxMsgCount arg; None = carry over the monotonic counter
    rxInc: bool = False            # increment the running RX counter each Check call (messages arriving)
    errorWarning: bool = False     # Check errorWarning arg
    timeStartMs: int = 0           # currentTimeMs for first Check call
    timeStepMs: int = 100          # currentTimeMs delta between Check calls
    recovery: bool = False         # call Swc_CanMonitor_Recovery after cycles
    recoveryTimeMs: int = 0        # currentTimeMs for the Recovery call


class CvcCanMonitorSetupBody(BaseModel):
    phases: list[CvcCanMonitorPhase] = []


class CvcCanMonitorRunBody(BaseModel):
    phases: list[CvcCanMonitorPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcWatchdogPhase(BaseModel):
    """One phase of the CVC watchdog harness script.
    Drives Swc_Watchdog_Init / Feed against the real Swc_Watchdog.c
    production code with mocked Dio_FlipChannel.
    """
    skipInit: bool = False         # skip Swc_Watchdog_Init (uninitialized guard)
    initNull: bool = False         # call Swc_Watchdog_Init(NULL_PTR) (NULL-config guard)
    loopComplete: bool = True      # Swc_Watchdog_Feed arg (main loop finished)
    canaryOk: bool = True          # Swc_Watchdog_Feed arg (stack canary intact)
    ramOk: bool = True             # Swc_Watchdog_Feed arg (RAM pattern passed)
    canOk: bool = True             # Swc_Watchdog_Feed arg (CAN not bus-off)
    feedCount: int = 1             # Swc_Watchdog_Feed calls


class CvcWatchdogSetupBody(BaseModel):
    phases: list[CvcWatchdogPhase] = []


class CvcWatchdogRunBody(BaseModel):
    phases: list[CvcWatchdogPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcSelfTestPhase(BaseModel):
    """One phase of the CVC self-test harness script.
    Drives Swc_SelfTest_Startup against the real Swc_SelfTest.c production
    code with per-check pass/fail pinning for the seven diagnostic checks.
    """
    spi: bool = True                # SelfTest_Hw_SpiLoopback result (True=E_OK)
    can: bool = True                # SelfTest_Hw_CanLoopback result
    nvm: bool = True                # SelfTest_Hw_NvmCheck result
    oled: bool = True               # SelfTest_Hw_OledAck result (non-critical)
    mpu: bool = True                # SelfTest_Hw_MpuVerify result
    canary: bool = True             # SelfTest_Hw_CanaryCheck result
    ram: bool = True                # SelfTest_Hw_RamPattern result


class CvcSelfTestSetupBody(BaseModel):
    phases: list[CvcSelfTestPhase] = []


class CvcSelfTestRunBody(BaseModel):
    phases: list[CvcSelfTestPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcSchedulerPhase(BaseModel):
    """One phase of the CVC scheduler harness script.
    Drives Swc_Scheduler_Init against the real Swc_Scheduler.c production
    code with config-guard pinning (NULL config / null runnables / zero count)
    and runnable-table selection for valid initialization.
    """
    skipInit: bool = False        # skip Swc_Scheduler_Init (uninitialized guard)
    initNull: bool = False        # Swc_Scheduler_Init(NULL_PTR) (NULL-config guard)
    nullRunnables: bool = False   # Init with config.runnables == NULL (guard)
    zeroCount: bool = False       # Init with config.runnableCount == 0 (guard)
    tableIndex: int = 0           # valid-init table: 0=production 8-run, 1=min 1-run, 2=max 16-run


class CvcSchedulerSetupBody(BaseModel):
    phases: list[CvcSchedulerPhase] = []


class CvcSchedulerRunBody(BaseModel):
    phases: list[CvcSchedulerPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcNvmPhase(BaseModel):
    """One phase of the CVC NVM harness script.
    Drives Swc_Nvm_Init / StoreDtc / LoadDtc / ReadCal / WriteCal /
    CalcCrc16 against the real Swc_Nvm.c production code, with test-only
    hooks to observe the internal circular-buffer state (write index /
    DTC count) and to corrupt stored CRCs to drive the corruption-detection
    paths (LoadDtc CRC mismatch, ReadCal fallback to defaults).
    """
    op: str = "init"                  # init|storeDtc|loadDtc|readCal|writeCal|
                                      # corruptDtcCrc|corruptCalCrc|calcCrc
    skipInit: bool = False            # skip Swc_Nvm_Init (uninitialized guard)
    repeats: int = 1                  # storeDtc: store count
    dtcId: int = 0                    # storeDtc: DTC event ID
    status: int = 0                   # storeDtc: DTC status mask
    ffMode: int = 0                   # storeDtc: 0=NULL freeze frame, 1=0xA0+i pattern
    slot: int = 0                     # loadDtc/corruptDtcCrc: slot index
    nullEntry: bool = False           # loadDtc: pass NULL_PTR as entry
    nullCal: bool = False             # readCal/writeCal: pass NULL_PTR
    pThreshold: int = 0               # writeCal: plausThreshold
    pDebounce: int = 0                # writeCal: plausDebounce
    stuckThreshold: int = 0           # writeCal: stuckThreshold
    stuckCycles: int = 0              # writeCal: stuckCycles
    lut0: int = 0                     # writeCal: torqueLut[0]
    dataLen: int = 4                  # calcCrc: buffer length
    nullCrc: bool = False             # calcCrc: pass NULL_PTR as data


class CvcNvmSetupBody(BaseModel):
    phases: list[CvcNvmPhase] = []


class CvcNvmRunBody(BaseModel):
    phases: list[CvcNvmPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcNvmPhase(BaseModel):
    """One phase of the FZC NVM harness script.
    Drives Swc_FzcNvm_Init / StoreDtc / LoadDtc / LoadCal / StoreCal /
    Crc16 against the real Swc_FzcNvm.c production code, with UNIT_TEST
    hooks to observe initialization state and to corrupt in-RAM CRCs,
    plus a mock NvM backend to verify re-init persistence and Init-time
    fallback on backend calibration corruption.
    """
    op: str = "init"                  # init|storeDtc|loadDtc|readCal|writeCal|
                                      # corruptDtcCrc|corruptCalCrc|
                                      # corruptBackendCalCrc|calcCrc
    skipInit: bool = False            # skip initial Swc_FzcNvm_Init on harness startup
    repeats: int = 1                  # storeDtc: repeat count
    dtcId: int = 0                    # storeDtc: DTC event ID
    steerAngle: int = 0               # storeDtc: freezeSteer
    brakePos: int = 0                 # storeDtc: freezeBrake
    lidarDist: int = 0                # storeDtc: freezeLidar
    slot: int = 0                     # loadDtc/corruptDtcCrc: slot index
    nullRecord: bool = False          # loadDtc: pass NULL_PTR
    nullCal: bool = False             # readCal/writeCal: pass NULL_PTR
    steerCenterOffset: int = 0        # writeCal
    steerGain: int = 0                # writeCal
    brakePosOffset: int = 0           # writeCal
    brakeGain: int = 0                # writeCal
    lidarWarnCm: int = 0              # writeCal
    lidarBrakeCm: int = 0             # writeCal
    lidarEmergencyCm: int = 0         # writeCal
    dataLen: int = 4                  # calcCrc: buffer length
    nullCrc: bool = False             # calcCrc: pass NULL_PTR


class FzcNvmSetupBody(BaseModel):
    phases: list[FzcNvmPhase] = []


class FzcNvmRunBody(BaseModel):
    phases: list[FzcNvmPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcSteeringPhase(BaseModel):
    """One phase of the FZC steering servo harness script.

    Drives Swc_Steering_MainFunction() against injected RTE command,
    IoHwAb SPI feedback, and fault injections, and reports PWM / Dio /
    RTE / DEM outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Steering_Init (uninitialized guard)
    initNull: bool = False         # call Swc_Steering_Init(NULL) (NULL-config guard)
    cmdAngle: int = 0              # commanded angle in degrees (RTE FZC_SIG_STEER_CMD)
    rteReadFail: bool = False      # Rte_Read returns E_NOT_OK (command timeout path)
    actualAngle: int = 0           # IoHwAb feedback in degrees (14-bit SPI raw)
    actualTrack: bool = False      # feedback tracks previous RTE output (healthy)
    spiFail: bool = False          # IoHwAb_ReadSteeringAngle returns E_NOT_OK
    getAngle: bool = False         # call Swc_Steering_GetAngle at end of phase
    getAngleNull: bool = False     # call Swc_Steering_GetAngle(NULL)


class FzcSteeringSetupBody(BaseModel):
    phases: list[FzcSteeringPhase] = []


class FzcSteeringRunBody(BaseModel):
    phases: list[FzcSteeringPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcBrakePhase(BaseModel):
    """One phase of the FZC brake servo harness script.

    Drives Swc_Brake_MainFunction() against injected RTE brake command,
    E-stop flag, IoHwAb ADC feedback, and fault injections, and reports
    PWM / RTE / DEM outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Brake_Init (uninitialized guard)
    initNull: bool = False         # call Swc_Brake_Init(NULL) (NULL-config guard)
    cmdBrake: int = 0              # commanded brake force 0-100+ (RTE FZC_SIG_BRAKE_CMD)
    rteReadFail: bool = False      # Rte_Read returns E_NOT_OK (command timeout path)
    estop: int = 0                 # RTE FZC_SIG_ESTOP_ACTIVE (immediate 100% brake)
    actualPos: int = 0             # IoHwAb ADC feedback 0-1000 (0-100% in 10ths)
    actualTrack: bool = False      # feedback tracks the commanded brake (healthy)
    posReadFail: bool = False      # IoHwAb_ReadBrakePosition returns E_NOT_OK
    getPos: bool = False           # call Swc_Brake_GetPosition at end of phase
    getPosNull: bool = False       # call Swc_Brake_GetPosition(NULL)


class FzcBrakeSetupBody(BaseModel):
    phases: list[FzcBrakePhase] = []


class FzcBrakeRunBody(BaseModel):
    phases: list[FzcBrakePhase] | None = None  # stimulus phases, appended after stored precondition


class FzcLidarPhase(BaseModel):
    """One phase of the FZC lidar harness script.

    Drives Swc_Lidar_MainFunction() against injected TFMini-S UART frames
    and fault injections, and reports RTE / DEM / GetDistance outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Lidar_Init (uninitialized guard)
    initNull: bool = False         # call Swc_Lidar_Init(NULL) (NULL-config guard)
    distCm: int = 0                # distance in cm for the injected frame
    signal: int = 0                # signal strength for the injected frame
    noFrame: bool = False          # feed no UART bytes (timeout path)
    badChecksum: bool = False      # corrupt the frame checksum byte
    garbageHeader: bool = False    # feed 32 non-header bytes (sync fail)
    partialFrame: bool = False     # feed header + 3 bytes only (incomplete)
    uartFailAt: int = 0            # fail UART reads at/after call index (0=never; 1=sync, 3=payload)
    getDist: bool = False          # call Swc_Lidar_GetDistance at end of phase
    getDistNull: bool = False      # call Swc_Lidar_GetDistance(NULL)


class FzcLidarSetupBody(BaseModel):
    phases: list[FzcLidarPhase] = []


class FzcLidarRunBody(BaseModel):
    phases: list[FzcLidarPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcCurrentMonitorPhase(BaseModel):
    """One phase of the RZC current-monitor harness script.

    Drives Swc_CurrentMonitor_MainFunction() against an injected raw motor
    current via the IoHwAb mock, and reports the averaged current /
    overcurrent flag / DEM / DIO outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_CurrentMonitor_Init (uninitialized guard)
    currentMa: int = 2048          # raw motor current (mA) injected to IoHwAb


class RzcCurrentMonitorSetupBody(BaseModel):
    phases: list[RzcCurrentMonitorPhase] = []


class RzcCurrentMonitorRunBody(BaseModel):
    phases: list[RzcCurrentMonitorPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcEncoderPhase(BaseModel):
    """One phase of the RZC encoder harness script.

    Drives Swc_Encoder_MainFunction() against injected encoder count /
    direction and commanded motor direction / torque echo, and reports the
    computed speed, encoder direction, stall flag, DEM, and DIO outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Encoder_Init (uninitialized guard)
    count: int | None = None       # absolute encoder counter before this phase
    deltaPerCycle: int = 0         # encoder count increment before each cycle
    encoderDir: int = 0            # 0=FORWARD 1=REVERSE 2=STOP (IoHwAb feedback)
    commandedDir: int = 0          # 0=FORWARD 1=REVERSE 2=STOP (RTE command)
    torqueEcho: int = 0            # RTE torque echo % for stall detection


class RzcEncoderSetupBody(BaseModel):
    phases: list[RzcEncoderPhase] = []


class RzcEncoderRunBody(BaseModel):
    phases: list[RzcEncoderPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcSafetyPhase(BaseModel):
    """One phase of the RZC safety harness script.

    Drives Swc_RzcSafety_Init / MainFunction / NotifyCanRx against the real
    Swc_RzcSafety.c production code (watchdog feed with 4-condition gate,
    fault aggregation, CAN bus loss detection with silence / error-warning /
    bus-off / latch, motor disable on CAN loss, safety status publication,
    WATCHDOG_FAIL edge DTC report).
    """
    cycles: int = 1                # Swc_RzcSafety_MainFunction calls
    skipInit: bool = False         # skip Swc_RzcSafety_Init (uninitialized guard)
    reinit: bool = False           # call Swc_RzcSafety_Init again at phase start
    overcurrent: int = 0           # RTE RZC_SIG_OVERCURRENT input
    overtemp: int = 0              # RTE RZC_SIG_TEMP_FAULT input
    directionFault: int = 0        # RTE RZC_SIG_ENCODER_DIR input
    stallFault: int = 0            # RTE RZC_SIG_ENCODER_STALL input
    batteryFault: int = 0          # RTE RZC_SIG_BATTERY_STATUS input
    selfTestResult: int = 1        # RTE RZC_SIG_SELF_TEST_RESULT input (1=PASS 0=FAIL)
    estopActive: int = 0           # RTE RZC_SIG_ESTOP_ACTIVE input
    vehicleState: int = 1          # RTE RZC_SIG_VEHICLE_STATE input
                                   # (0=INIT 1=RUN 2=DEGRADED 3=LIMP
                                   #  4=SAFE_STOP 5=SHUTDOWN)
    canErrorState: int = 0         # Can_GetControllerErrorState(0)
                                   # (0=ACTIVE 1=WARNING 2=BUSOFF)
    notifyCanRx: bool = False      # call Swc_RzcSafety_NotifyCanRx before each
                                   # MainFunction call (resets silence counter)


class RzcSafetySetupBody(BaseModel):
    phases: list[RzcSafetyPhase] = []


class RzcSafetyRunBody(BaseModel):
    phases: list[RzcSafetyPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcSelfTestPhase(BaseModel):
    """One phase of the RZC self-test harness script.

    Drives Swc_RzcSelfTest_Init / Startup against the real
    Swc_RzcSelfTest.c production code (8 injected hardware diagnostic
    callbacks: BTS7960 enable-pin toggle / ACS723 baseline cal / NTC range /
    Encoder connectivity / CAN loopback / MPU verify / stack canary / RAM
    pattern). Any single failure aborts the sequence with motor disable +
    DTC. Values: 1=pass (E_OK), 0=fail (E_NOT_OK), 2=NULL callback (guard).
    """
    skipInit: bool = False            # skip Swc_RzcSelfTest_Init (uninitialized guard)
    initNull: bool = False            # call Swc_RzcSelfTest_Init(NULL_PTR) (NULL-config guard)
    bts7960: int = 1                  # BTS7960 enable-pin toggle result
    acs723: int = 1                   # ACS723 baseline calibration result
    ntc: int = 1                      # NTC temperature range check result
    encoder: int = 1                  # Encoder connectivity result
    can: int = 1                      # CAN loopback result
    mpu: int = 1                      # MPU region verify result
    canary: int = 1                   # Stack canary plant result
    ram: int = 1                      # RAM pattern test result


class RzcSelfTestSetupBody(BaseModel):
    phases: list[RzcSelfTestPhase] = []


class RzcSelfTestRunBody(BaseModel):
    phases: list[RzcSelfTestPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcHeartbeatPhase(BaseModel):
    """One phase of the RZC heartbeat harness script.

    Drives Swc_Heartbeat_Init / MainFunction against the real
    Swc_Heartbeat.c production code (TX 50ms boundary schedule, alive
    counter 15 wrap, ECU ID write, vehicle state / fault-mask publication,
    CAN fault + SAFE_STOP TX suppression).
    """
    cycles: int = 1                # Swc_Heartbeat_MainFunction calls
    skipInit: bool = False         # skip Swc_Heartbeat_Init (uninitialized guard)
    vehicleState: int = 1          # RTE RZC_SIG_VEHICLE_STATE read at TX boundary
    faultMask: int = 0             # RTE RZC_SIG_FAULT_MASK read at TX boundary
                                   # (bit3=RZC_FAULT_CAN; suppress only when
                                   #  set AND vehicle_state == SAFE_STOP)


class RzcHeartbeatSetupBody(BaseModel):
    phases: list[RzcHeartbeatPhase] = []


class RzcHeartbeatRunBody(BaseModel):
    phases: list[RzcHeartbeatPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcMotorPhase(BaseModel):
    """One phase of the RZC motor harness script.

    Drives Swc_Motor_MainFunction() against injected RTE vehicle state,
    e-stop, torque command, thermal derating, and external fault flags,
    and reports PWM / Dio / RTE / DEM outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Motor_Init (uninitialized guard)
    vehicleState: int = 1          # 0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN
    estop: int = 0                 # RTE RZC_SIG_ESTOP_ACTIVE (immediate disable)
    torqueCmd: int = 0             # commanded torque % (sint16, negative = reverse)
    derating: int = 100            # RTE RZC_SIG_DERATING_PCT (clamped to 100)
    overcurrent: int = 0           # RTE RZC_SIG_OVERCURRENT external fault
    tempFault: int = 0             # RTE RZC_SIG_TEMP_FAULT external fault


class RzcMotorSetupBody(BaseModel):
    phases: list[RzcMotorPhase] = []


class RzcMotorRunBody(BaseModel):
    phases: list[RzcMotorPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcBatteryPhase(BaseModel):
    """One phase of the RZC battery harness script.

    Drives Swc_Battery_MainFunction() against an injected raw battery
    voltage (mV) via the IoHwAb_ReadBatteryVoltage mock, and reports the
    4-sample moving average / status / DEM outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_Battery_Init (uninitialized guard)
    voltageMv: int = 0             # raw battery voltage (mV) injected to IoHwAb


class RzcBatterySetupBody(BaseModel):
    phases: list[RzcBatteryPhase] = []


class RzcBatteryRunBody(BaseModel):
    phases: list[RzcBatteryPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcTempMonitorPhase(BaseModel):
    """One phase of the RZC temp-monitor harness script.

    Drives Swc_TempMonitor_MainFunction() against an injected NTC1/NTC2
    temperature (deci-degrees C) via the IoHwAb read mocks, and reports
    the selected temperature / derating / fault / DEM outputs.
    """
    cycles: int = 1                # MainFunction calls
    skipInit: bool = False         # skip Swc_TempMonitor_Init (uninitialized guard)
    tempDc: int = 0                # NTC1 temperature (deci-degrees C) injected to IoHwAb
    temp2Dc: int | None = None     # NTC2 temperature (deci-degrees C); None = agree with NTC1
    ioFault: bool = False          # IoHwAb_ReadMotorTemp returns E_NOT_OK
    temp2Fail: bool = False        # IoHwAb_ReadMotorTemp2 returns E_NOT_OK


class RzcTempMonitorSetupBody(BaseModel):
    phases: list[RzcTempMonitorPhase] = []


class RzcTempMonitorRunBody(BaseModel):
    phases: list[RzcTempMonitorPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcComPhase(BaseModel):
    """One phase of the RZC COM harness script.

    Drives the Swc_RzcCom APIs against injected E2E buffers / RTE signals,
    and reports E2E return codes, mutated buffers, RTE torque/estop state,
    DEM CAN-bus-off status, and TX Com signals.
    """
    op: str = "receive"            # init | e2eProtect | e2eCheck | receive | tx
    pduId: int = 0                 # E2E PDU index
    data: str = "0000000000000000" # E2E 8-byte payload as hex ("null" = NULL_PTR)
    length: int = 8                # E2E payload length
    repeats: int = 1               # repeat count for e2eProtect / e2eCheck
    cycles: int = 1                # cyclic call count (receive / tx)
    skipInit: bool = False         # skip Swc_RzcCom_Init (uninitialized guard)
    estop: int = 0                 # RTE RZC_SIG_ESTOP_ACTIVE (receive input)
    vehicleState: int = 1          # RTE RZC_SIG_VEHICLE_STATE
    torqueCmd: int = 0             # RTE RZC_SIG_TORQUE_CMD
    faultMask: int = 0             # RTE RZC_SIG_FAULT_MASK (tx input)
    torqueEcho: int = 0            # RTE RZC_SIG_TORQUE_ECHO
    speedRpm: int = 0              # RTE RZC_SIG_ENCODER_SPEED
    motorDir: int = 0              # RTE RZC_SIG_MOTOR_DIR
    motorEnable: int = 0           # RTE RZC_SIG_MOTOR_ENABLE
    motorFault: int = 0            # RTE RZC_SIG_MOTOR_FAULT
    currentMa: int = 0             # RTE RZC_SIG_CURRENT_MA
    overcurrent: int = 0           # RTE RZC_SIG_OVERCURRENT
    temp1Dc: int = 0               # RTE RZC_SIG_TEMP1_DC
    temp2Dc: int = 0               # RTE RZC_SIG_TEMP2_DC
    deratingPct: int = 100         # RTE RZC_SIG_DERATING_PCT
    batteryMv: int = 0             # RTE RZC_SIG_BATTERY_MV
    batteryStatus: int = 0         # RTE RZC_SIG_BATTERY_STATUS


class RzcComSetupBody(BaseModel):
    phases: list[RzcComPhase] = []


class RzcComRunBody(BaseModel):
    phases: list[RzcComPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcFzcComPhase(BaseModel):
    """One phase of the FZC COM harness script.

    Drives the Swc_FzcCom APIs against injected E2E buffers / RTE signals,
    and reports E2E return codes, mutated buffers, CAN-monitor notify
    counts, the g_dbg_* instrumentation counters, and TX Com signals.
    """
    op: str = "receive"            # init | e2eProtect | e2eCheck | receive | tx
    data: str = "0000000000000000" # E2E 8-byte payload as hex ("null" = NULL_PTR)
    dataId: int = 3                # FZC E2E Data ID (FZC-specific, from Fzc_Cfg.h)
    length: int = 8                # E2E payload length
    repeats: int = 1               # repeat count for e2eProtect / e2eCheck
    cycles: int = 1                # cyclic call count (receive / tx)
    skipInit: bool = False         # skip Swc_FzcCom_Init (uninitialized guard)
    vehicleState: int = 1          # RTE FZC_SIG_VEHICLE_STATE (tx input)
    faultMask: int = 0             # RTE FZC_SIG_FAULT_MASK (tx input)
    steerAngle: int = 0            # RTE FZC_SIG_STEER_ANGLE (tx input, sint16)
    steerFault: int = 0            # RTE FZC_SIG_STEER_FAULT (tx input)
    brakePos: int = 0              # RTE FZC_SIG_BRAKE_POS (tx input)
    brakeFault: int = 0            # RTE FZC_SIG_BRAKE_FAULT (tx input)
    motorCutoff: int = 0           # RTE FZC_SIG_MOTOR_CUTOFF (tx input)
    lidarZone: int = 0             # RTE FZC_SIG_LIDAR_ZONE (tx input)
    lidarDist: int = 0             # RTE FZC_SIG_LIDAR_DIST (tx input, uint16)
    lidarSignal: int = 0           # RTE FZC_SIG_LIDAR_SIGNAL (tx input, uint16)


class FzcFzcComSetupBody(BaseModel):
    phases: list[FzcFzcComPhase] = []


class FzcFzcComRunBody(BaseModel):
    phases: list[FzcFzcComPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcHeartbeatPhase(BaseModel):
    """One phase of the FZC heartbeat harness script.

    Drives Swc_Heartbeat_Init / MainFunction against the real
    Swc_Heartbeat.c production code (TX 50ms boundary schedule, alive
    counter 15 wrap, ECU ID write, vehicle state / fault-mask publication,
    CAN bus-off TX suppression).
    """
    cycles: int = 1                # Swc_Heartbeat_MainFunction calls
    skipInit: bool = False         # skip Swc_Heartbeat_Init (uninitialized guard)
    vehicleState: int = 1          # RTE FZC_SIG_VEHICLE_STATE read at TX boundary
    faultMask: int = 0             # RTE FZC_SIG_FAULT_MASK read at TX boundary (bit8=bus-off)


class FzcHeartbeatSetupBody(BaseModel):
    phases: list[FzcHeartbeatPhase] = []


class FzcHeartbeatRunBody(BaseModel):
    phases: list[FzcHeartbeatPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcCanMonitorPhase(BaseModel):
    """One phase of the FZC CAN monitor harness script.

    Drives Swc_FzcCanMonitor_Init / Check / GetStatus / NotifyRx against the
    real Swc_FzcCanMonitor.c production code (boot grace 500 cycles, bus-off
    immediate, 20-cycle silence, 50-cycle sustained error warning, safe-state
    latch with NO recovery, NotifyRx silence reset).
    """
    cycles: int = 1                # Swc_FzcCanMonitor_Check calls
    skipInit: bool = False         # skip Swc_FzcCanMonitor_Init (uninitialized guard)
    canMode: int = 2               # Can_GetControllerMode(0) return (2=STARTED, 1=STOPPED)
    tec: int = 0                   # Can_GetErrorCounters transmit error counter
    rec: int = 0                   # Can_GetErrorCounters receive error counter
    notifyRx: bool = False         # call Swc_FzcCanMonitor_NotifyRx before each Check


class FzcCanMonitorSetupBody(BaseModel):
    phases: list[FzcCanMonitorPhase] = []


class FzcCanMonitorRunBody(BaseModel):
    phases: list[FzcCanMonitorPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcSafetyPhase(BaseModel):
    """One phase of the FZC safety harness script.

    Drives Swc_FzcSafety_Init / MainFunction / GetStatus against the real
    Swc_FzcSafety.c production code (watchdog feed with 4-condition gate,
    fault aggregation into unified mask, self-test fault handling, RX-quality
    CAN-bus-off detection gated by the post-INIT boot grace period, motor
    cutoff assertion, safety status publication).
    """
    cycles: int = 1                # Swc_FzcSafety_MainFunction calls
    skipInit: bool = False         # skip Swc_FzcSafety_Init (uninitialized guard)
    reinit: bool = False           # call Swc_FzcSafety_Init again at phase start
    steerFault: int = 0            # RTE FZC_SIG_STEER_FAULT input (0 = no fault)
    brakeFault: int = 0            # RTE FZC_SIG_BRAKE_FAULT input (0 = no fault)
    lidarFault: int = 0            # RTE FZC_SIG_LIDAR_FAULT input (0 = no fault)
    vehicleState: int = 1          # RTE FZC_SIG_VEHICLE_STATE input (1=RUN 5=SHUTDOWN)
    selfTestResult: int = 1        # RTE FZC_SIG_SELF_TEST_RESULT input (1=PASS 0=FAIL)
    selfTestDone: bool = False     # Safety_SelfTestDone injection (self-test completed)
    steerCmdQuality: int = 0       # Com_GetRxPduQuality(STEER_CMD) (0=FRESH 2=TIMED_OUT)
    brakeCmdQuality: int = 0       # Com_GetRxPduQuality(BRAKE_CMD) (0=FRESH 2=TIMED_OUT)


class FzcSafetySetupBody(BaseModel):
    phases: list[FzcSafetyPhase] = []


class FzcSafetyRunBody(BaseModel):
    phases: list[FzcSafetyPhase] | None = None  # stimulus phases, appended after stored precondition


class FzcSchedulerPhase(BaseModel):
    """One phase of the FZC scheduler harness script.

    Drives Swc_FzcScheduler_Init / GetTable / GetCount against the real
    Swc_FzcScheduler.c production code (static const SWR-FZC-029 runnable
    table, uninitialized GetTable NULL guard, idempotent re-init).
    """
    skipInit: bool = False        # skip Swc_FzcScheduler_Init (uninitialized guard)
    reinit: bool = False          # call Swc_FzcScheduler_Init again at phase start


class FzcSchedulerSetupBody(BaseModel):
    phases: list[FzcSchedulerPhase] = []


class FzcSchedulerRunBody(BaseModel):
    phases: list[FzcSchedulerPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcSchedulerPhase(BaseModel):
    """One phase of the RZC scheduler harness script.

    Drives Swc_RzcScheduler_Init / Tick / GetTable / GetUtilPct against the
    real Swc_RzcScheduler.c production code (static const SWR-RZC-028
    runnable table, Tick dispatch with per-runnable elapsed counters,
    uninitialized guard, idempotent re-init, util computation).
    """
    skipInit: bool = False        # skip Swc_RzcScheduler_Init (uninitialized guard)
    reinit: bool = False          # call Swc_RzcScheduler_Init again at phase start
    ticks: int = 0                # Swc_RzcScheduler_Tick calls (dispatch simulation)


class RzcSchedulerSetupBody(BaseModel):
    phases: list[RzcSchedulerPhase] = []


class RzcSchedulerRunBody(BaseModel):
    phases: list[RzcSchedulerPhase] | None = None  # stimulus phases, appended after stored precondition


class RzcNvmPhase(BaseModel):
    """One phase of the RZC NVM harness script.

    Drives Swc_RzcNvm_Init / StoreDtc / LoadDtc / GetWriteIndex against the
    real Swc_RzcNvm.c production code (20-slot circular-buffer DTC persistence,
    CRC-16/CCITT per-entry integrity, freeze-frame storage, write-index wrap),
    with UNIT_TEST hooks to observe the initialization flag, corrupt stored
    DTC CRCs to drive the LoadDtc fail-closed path, and verify the static
    CRC-16 calculator on known vectors.
    """
    op: str = "init"                  # init|storeDtc|loadDtc|corruptDtcCrc|calcCrc
    skipInit: bool = False            # skip initial Swc_RzcNvm_Init on harness startup
    repeats: int = 1                  # storeDtc: repeat count
    dtcId: int = 0                    # storeDtc: DTC event ID
    status: int = 0                   # storeDtc: DTC status byte
    timestamp: int = 0                # storeDtc: system tick at storage
    motorCurrentMa: int = 0           # storeDtc: freeze-frame motor current (mA)
    motorTempDdc: int = 0             # storeDtc: freeze-frame motor temp (deci-deg C)
    motorSpeedRpm: int = 0            # storeDtc: freeze-frame motor speed (RPM)
    batteryMv: int = 0                # storeDtc: freeze-frame battery voltage (mV)
    torqueCmdPct: int = 0             # storeDtc: freeze-frame torque command (%)
    vehicleState: int = 0             # storeDtc: freeze-frame vehicle state
    slot: int = 0                     # loadDtc/corruptDtcCrc: slot index
    nullFreeze: bool = False          # storeDtc: pass NULL_PTR as freeze frame
    nullEntry: bool = False           # loadDtc: pass NULL_PTR as entry
    dataLen: int = 4                  # calcCrc: buffer length


class RzcNvmSetupBody(BaseModel):
    phases: list[RzcNvmPhase] = []


class RzcNvmRunBody(BaseModel):
    phases: list[RzcNvmPhase] | None = None  # stimulus phases, appended after stored precondition


class ScStatePhase(BaseModel):
    """One phase of the SC state harness script.

    Drives SC_State_Init / SC_State_Get / SC_State_Transition against the
    real sc_state.c production code (GAP-SC-006 authoritative runtime state
    machine: INIT/MONITORING/FAULT/KILL valid edges, invalid transitions
    rejected fail-closed, unknown state forces KILL), with a UNIT_TEST hook
    to inject an unknown internal state and drive the default fail-closed
    branch.
    """
    op: str = "init"                  # init|transition|setRaw
    skipInit: bool = False            # skip initial SC_State_Init on harness startup
    newState: int = 0                 # transition: target state (SC_STATE_*)
    state: int = 0                    # setRaw: raw state value to inject


class ScStateSetupBody(BaseModel):
    phases: list[ScStatePhase] = []


class ScStateRunBody(BaseModel):
    phases: list[ScStatePhase] | None = None  # stimulus phases, appended after stored precondition


class ScHeartbeatPhase(BaseModel):
    """One phase of the SC heartbeat harness script.

    Drives SC_Heartbeat_Init / NotifyRx / Monitor / ValidateContent against
    the real sc_heartbeat.c production code (per-ECU CVC/FZC/RZC heartbeat
    monitoring: independent 150ms timeout counters, 20-tick confirmation
    window with latch, 3-HB recovery debounce, startup grace, LED drive, and
    content validation SWR-SC-027/028), with UNIT_TEST hooks observing every
    internal counter/flag and a mock GIO tracking the fault LED state.
    """
    op: str = "init"                  # init|monitor|notifyRx|validate
    skipInit: bool = False            # skip initial SC_Heartbeat_Init on harness startup
    ticks: int = 1                    # monitor: SC_Heartbeat_Monitor call count
    ecu: int = 0                      # notifyRx/validate: SC_ECU_* index
    repeats: int = 1                  # notifyRx/validate: repeat count
    payload3: int = 0                 # validate: heartbeat byte 3 (mode|faults)
    notifyA: int = 255                # monitor: ECU to NotifyRx once per tick (none=255)
    notifyB: int = 255                # monitor: second ECU to NotifyRx once per tick (none=255)


class ScHeartbeatSetupBody(BaseModel):
    phases: list[ScHeartbeatPhase] = []


class ScHeartbeatRunBody(BaseModel):
    phases: list[ScHeartbeatPhase] | None = None  # stimulus phases, appended after stored precondition


class ScE2ePhase(BaseModel):
    """One phase of the SC E2E harness script.

    Drives SC_E2E_Init / SC_E2E_Check / SC_E2E_IsMsgFailed /
    SC_E2E_IsAnyCriticalFailed / SC_E2E_ComputeCRC8 against the real
    sc_e2e.c production code (SWR-SC-003: CRC-8 poly 0x1D validation over
    DataId + payload, byte-0 alive counter monotonicity, per-mailbox
    consecutive-failure latch SC_E2E_MAX_CONSEC_FAIL=3, boot grace window,
    and GAP-SC-002 critical-mailbox E-Stop/heartbeat relay-kill gating),
    with UNIT_TEST hooks observing every internal counter/flag and the
    internal sc_crc8(). Harness compiles the production TMS570 logic (no
    PLATFORM_POSIX / PLATFORM_HIL).
    """
    op: str = "init"                  # init|check|drainGrace|crc8|compute
    skipInit: bool = False            # skip initial SC_E2E_Init on harness startup
    dataId: int = 1                   # check: E2E Data ID
    msgIndex: int = 0                 # check: mailbox index (0-based, < SC_MB_COUNT)
    dlc: int = 8                      # check: data length code (2..8, >8 exercises cap)
    alive: int = 0                    # check: alive counter (0-15)
    crcCorrupt: int = 0               # check: flip CRC byte (byte 1)
    dataIdCorrupt: int = 0            # check: force byte0 lower nibble != dataId
    payloadCorrupt: int = 0           # check: flip payload byte data[4]
    nullData: int = 0                 # check/crc8/compute: pass NULL_PTR
    ticks: int = 1                    # drainGrace: SC_E2E_IsAnyCriticalFailed call count
    len: int = 3                      # crc8/compute: input byte length


class ScE2eSetupBody(BaseModel):
    phases: list[ScE2ePhase] = []


class ScE2eRunBody(BaseModel):
    phases: list[ScE2ePhase] | None = None  # stimulus phases, appended after stored precondition


class ScRelayPhase(BaseModel):
    """One phase of the SC relay harness script.

    Drives SC_Relay_Init / Energize / DeEnergize / CheckTriggers / IsKilled /
    GetKillReason against the real sc_relay.c production code (SWR-SC-010/011/
    012: kill relay GPIO control with permanent de-energize latch, and the
    10ms trigger cascade — E-Stop, heartbeat confirmed timeout, plausibility
    fault, creep guard, E2E critical failure, self-test failure, ESM lockstep
    error, CAN bus-off, bus silence, and 2-consecutive GPIO readback
    mismatch), with injected mocks for every external module getter and a
    mocked relay GIO pin whose readback can be overridden to drive the
    readback-mismatch branches. UNIT_TEST hooks observe the internal
    commanded/killed flags and the mismatch counter.
    """
    op: str = "init"                  # init|energize|deEnergize|checkTriggers|setMock|setReadback
    skipInit: bool = False            # skip initial SC_Relay_Init on harness startup
    repeats: int = 1                  # checkTriggers: SC_Relay_CheckTriggers call count
    estop: int = 0                    # setMock: SC_CAN_IsEStopActive
    hb: int = 0                       # setMock: SC_Heartbeat_IsAnyConfirmed
    plaus: int = 0                    # setMock: SC_Plausibility_IsFaulted
    creep: int = 0                    # setMock: SC_Plausibility_IsCreepFaulted
    e2e: int = 0                      # setMock: SC_E2E_IsAnyCriticalFailed
    selftest: int = 1                 # setMock: SC_SelfTest_IsHealthy (1=healthy)
    esm: int = 0                      # setMock: SC_ESM_IsErrorActive
    busoff: int = 0                   # setMock: SC_CAN_IsBusOff
    busSilent: int = 0                # setMock: SC_CAN_IsBusSilent
    value: int = 0                    # setReadback: GIO relay pin readback value


class ScRelaySetupBody(BaseModel):
    phases: list[ScRelayPhase] = []


class ScRelayRunBody(BaseModel):
    phases: list[ScRelayPhase] | None = None  # stimulus phases, appended after stored precondition


class ScWatchdogPhase(BaseModel):
    """One phase of the SC watchdog harness script.

    Drives SC_Watchdog_Init / SC_Watchdog_Feed against the real
    sc_watchdog.c production code (SWR-SC-022: TPS3823 external watchdog feed
    — Feed toggles the WDI pin only when allChecksOk==TRUE, otherwise the
    watchdog starves and TPS3823 asserts RESET after its timeout). The WDI
    GIO pin is mocked; every WDI write is counted so the toggle/starve
    behavior is observable. Harness compiles the production TMS570 logic (no
    PLATFORM_POSIX / PLATFORM_HIL).
    """
    op: str = "init"                  # init|feed
    ok: int = 1                       # feed: allChecksOk (1=TRUE toggle, 0=FALSE starve)
    repeats: int = 1                  # feed: SC_Watchdog_Feed call count


class ScWatchdogSetupBody(BaseModel):
    phases: list[ScWatchdogPhase] = []


class ScWatchdogRunBody(BaseModel):
    phases: list[ScWatchdogPhase] | None = None  # stimulus phases, appended after stored precondition


class BswComCfgPhase(BaseModel):
    """One phase of the BSW Com config endpoint (true end-to-end).

    bus-probe observes *real* CVC frames on the CAN bus (vcan0) and verifies
    DLC, period, E2E dataId/alive/CRC and decoded signal values against the
    DBC single source of truth. Requires the SIL Docker stack (instrumented
    CVC ECU on vcan0) to be running.
    """
    op: str = "bus-probe"            # bus-probe
    targets: list[str] | None = None # DBC message names to observe
    windowMs: int = 2000             # observation window
    minFrames: int = 5               # minimum frames per message
    periodTolerancePct: int = 30     # allowed cycle-time deviation


class BswComCfgRunBody(BaseModel):
    phases: list[BswComCfgPhase] | None = None


class BswRteTaskBodiesPhase(BaseModel):
    """One phase of the BSW RTE task-bodies endpoint (true end-to-end).

    cadence verifies the generated task bodies drive real periodic frames at
    the DBC cadence on the CAN bus (10ms Com TX via the 10ms task, 50ms
    heartbeat via the 50ms task). ftti injects the E-stop through the CVC UDP
    DIO pin and measures the latency until EStop_Broadcast (0x001, Active=1)
    reaches the bus — the observable consequence of the 10ms dispatch
    ordering. Both require the SIL Docker stack (instrumented CVC on vcan0).
    """
    op: str = "cadence"              # cadence|ftti
    targets: list[str] | None = None # cadence: DBC message names to observe
    windowMs: int = 2000             # cadence: observation window
    minFrames: int = 5               # cadence: minimum frames per message
    periodTolerancePct: int = 30     # cadence: allowed cycle-time deviation
    budgetMs: int = 200              # ftti: E-stop FTTI budget
    restartCvc: bool = True          # ftti: restart CVC to clear latch


class BswRteTaskBodiesRunBody(BaseModel):
    phases: list[BswRteTaskBodiesPhase] | None = None


class ScSelfTestPhase(BaseModel):
    """One phase of the SC self-test harness script.

    Drives SC_SelfTest_Init / SC_SelfTest_Startup / SC_SelfTest_Runtime /
    SC_SelfTest_StackCanaryOk / SC_SelfTest_IsHealthy against the real
    sc_selftest.c production code (SWR-SC-016..021: 7-step startup BIST —
    lockstep, RAM PBIST, flash CRC-32, DCAN loopback, GPIO readback, lamp
    test, watchdog test — and the 60s-period runtime incremental checks:
    flash CRC, RAM 32-byte pattern, DCAN error status, GPIO readback), with
    UNIT_TEST hooks observing the internal runtime tick and health flags and
    injecting canary / RAM-pattern corruption to drive the failure branches.
    The seven startup hardware diagnostics and two runtime hardware checks
    are mocked; every mock counts its invocations so a failing startup step
    provably blocks the later steps.
    """
    op: str = "startup"               # init|startup|runtime|canary
    b1: int = 1                       # startup: hw_lockstep_bist (0|1)
    b2: int = 1                       # startup: hw_ram_pbist (0|1)
    b3: int = 1                       # startup: hw_flash_crc_check (0|1)
    b4: int = 1                       # startup: hw_dcan_loopback_test (0|1)
    b5: int = 1                       # startup: hw_gpio_readback_test (0|1)
    b6: int = 1                       # startup: hw_lamp_test (0|1)
    b7: int = 1                       # startup: hw_watchdog_test (0|1)
    flashIncr: int = 1                # runtime: hw_flash_crc_incremental (0|1)
    dcanErr: int = 1                  # runtime: hw_dcan_error_check (0|1)
    readback: int = 0                 # runtime: GIO relay pin readback (0|1)
    corruptCanary: int = 0            # canary: corrupt stack canary before check
    corruptRam: int = 0               # runtime: corrupt RAM test area byte 0
    repeats: int = 1                  # runtime: SC_SelfTest_Runtime call count


class ScSelfTestSetupBody(BaseModel):
    phases: list[ScSelfTestPhase] = []


class ScSelfTestRunBody(BaseModel):
    phases: list[ScSelfTestPhase] | None = None  # stimulus phases, appended after stored precondition


class ScPlausibilityPhase(BaseModel):
    """One phase of the SC plausibility harness script.

    Drives SC_Plausibility_Init / SC_Plausibility_Check / IsFaulted /
    SC_CreepGuard_Check / IsCreepFaulted against the real sc_plausibility.c
    production code (SWR-SC-007/008/009/024 torque-vs-current cross-check
    with 16-entry LUT linear interpolation, 20% relative / 2000mA absolute
    threshold and debounce, fault latch + system LED, FZC-brake-fault backup
    cutoff, and the SSR-SC-018 standstill creep guard), with UNIT_TEST hooks
    observing every internal counter and the lookup/is_implausible statics.
    CAN data (torque / current), heartbeat brake-fault flag, and GIO system
    LED are injected mocks. Harness compiles the production TMS570 logic (no
    PLATFORM_POSIX / PLATFORM_HIL).
    """
    op: str = "init"                  # init|check|creep|drainGrace|lookup|implausible
    skipInit: bool = False            # skip initial SC_Plausibility_Init on harness startup
    torque: int = 0                   # check/creep/lookup: torque percentage (0-255)
    current: int = 0                  # check/creep: motor current in mA
    vehValid: int = 1                 # check/creep: vehicle-state mailbox valid
    curValid: int = 1                 # check/creep: motor-current mailbox valid
    brakeFault: int = 0               # check: FZC brake fault (backup cutoff)
    repeats: int = 1                  # check/creep: SC_Plausibility_Check/CreepGuard call count
    ticks: int = 1                    # drainGrace: SC_Plausibility_Check call count
    expected: int = 0                 # implausible: expected current in mA
    actual: int = 0                   # implausible: measured current in mA


class ScPlausibilitySetupBody(BaseModel):
    phases: list[ScPlausibilityPhase] = []


class ScPlausibilityRunBody(BaseModel):
    phases: list[ScPlausibilityPhase] | None = None  # stimulus phases, appended after stored precondition


class CvcEStopRunBody(BaseModel):
    phases: list[CvcEStopPhase] | None = None  # stimulus phases, appended after stored precondition


# Server-side state store for Given/When separation.
# Mutated by /pedal-torque/setup, read by /pedal-torque when fields are absent.
_stored_vehicle_state: int = 1          # default: RUN
_stored_cycles: int = 100               # default: 100 cycles
_stored_spi_fault_sensor: int | None = None  # default: no fault
_stored_dither_amplitude: int | None = None  # default: use harness built-in (16)
_stored_recover_cycles: int | None = None    # default: no recovery phase

# Stored VSM phase script for Given/When separation.
_stored_vehicle_state_phases: list[VehicleStatePhase] = []

# Stored E-stop phase script for Given/When separation.
_stored_estop_phases: list[CvcEStopPhase] = []

# Stored CvcCom phase script for Given/When separation.
_stored_cvccom_phases: list[CvcCvcComPhase] = []

# Stored CVC heartbeat phase script for Given/When separation.
_stored_cvc_heartbeat_phases: list[CvcHeartbeatPhase] = []

# Stored CVC CAN monitor phase script for Given/When separation.
_stored_cvc_canmonitor_phases: list[CvcCanMonitorPhase] = []

# Stored CVC watchdog phase script for Given/When separation.
_stored_cvc_watchdog_phases: list[CvcWatchdogPhase] = []

# Stored CVC self-test phase script for Given/When separation.
_stored_cvc_selftest_phases: list[CvcSelfTestPhase] = []

# Stored CVC scheduler phase script for Given/When separation.
_stored_cvc_scheduler_phases: list[CvcSchedulerPhase] = []

# Stored CVC NVM phase script for Given/When separation.
_stored_cvc_nvm_phases: list[CvcNvmPhase] = []

# Stored FZC NVM phase script for Given/When separation.
_stored_fzc_nvm_phases: list[FzcNvmPhase] = []

# Stored FZC steering phase script for Given/When separation.
_stored_fzc_steering_phases: list[FzcSteeringPhase] = []

# Stored FZC brake phase script for Given/When separation.
_stored_fzc_brake_phases: list[FzcBrakePhase] = []

# Stored FZC lidar phase script for Given/When separation.
_stored_fzc_lidar_phases: list[FzcLidarPhase] = []

# Stored RZC safety phase script for Given/When separation.
_stored_rzc_safety_phases: list[RzcSafetyPhase] = []

# Stored RZC self-test phase script for Given/When separation.
_stored_rzc_selftest_phases: list[RzcSelfTestPhase] = []

# Stored RZC encoder phase script for Given/When separation.
_stored_rzc_encoder_phases: list[RzcEncoderPhase] = []

# Stored RZC heartbeat phase script for Given/When separation.
_stored_rzc_heartbeat_phases: list[RzcHeartbeatPhase] = []

# Stored RZC current-monitor phase script for Given/When separation.
_stored_rzc_currentmonitor_phases: list[RzcCurrentMonitorPhase] = []

# Stored RZC motor phase script for Given/When separation.
_stored_rzc_motor_phases: list[RzcMotorPhase] = []

# Stored RZC battery phase script for Given/When separation.
_stored_rzc_battery_phases: list[RzcBatteryPhase] = []

# Stored RZC temp-monitor phase script for Given/When separation.
_stored_rzc_temponitor_phases: list[RzcTempMonitorPhase] = []

# Stored RZC COM phase script for Given/When separation.
_stored_rzc_rzccom_phases: list[RzcComPhase] = []

# Stored FZC COM phase script for Given/When separation.
_stored_fzc_fzccom_phases: list[FzcFzcComPhase] = []

# Stored FZC heartbeat phase script for Given/When separation.
_stored_fzc_heartbeat_phases: list[FzcHeartbeatPhase] = []

# Stored FZC CAN monitor phase script for Given/When separation.
_stored_fzc_canmonitor_phases: list[FzcCanMonitorPhase] = []

# Stored FZC safety phase script for Given/When separation.
_stored_fzc_safety_phases: list[FzcSafetyPhase] = []

# Stored FZC scheduler phase script for Given/When separation.
_stored_fzc_scheduler_phases: list[FzcSchedulerPhase] = []

# Stored RZC scheduler phase script for Given/When separation.
_stored_rzc_scheduler_phases: list[RzcSchedulerPhase] = []

# Stored RZC NVM phase script for Given/When separation.
_stored_rzc_nvm_phases: list[RzcNvmPhase] = []

# Stored SC state phase script for Given/When separation.
_stored_sc_state_phases: list[ScStatePhase] = []

# Stored SC heartbeat phase script for Given/When separation.
_stored_sc_heartbeat_phases: list[ScHeartbeatPhase] = []

# Stored SC E2E phase script for Given/When separation.
_stored_sc_e2e_phases: list[ScE2ePhase] = []

# Stored SC relay phase script for Given/When separation.
_stored_sc_relay_phases: list[ScRelayPhase] = []

# Stored SC plausibility phase script for Given/When separation.
_stored_sc_plausibility_phases: list[ScPlausibilityPhase] = []

# Stored SC watchdog phase script for Given/When separation.
_stored_sc_watchdog_phases: list[ScWatchdogPhase] = []


# Stored SC self-test phase script for Given/When separation.
_stored_sc_selftest_phases: list[ScSelfTestPhase] = []


# Test runner instance (initialized on startup)
_test_runner: DashboardTestRunner | None = None
_CVC_PEDAL_HARNESS = "/app/bin/cvc_pedal_harness"
_CVC_VSM_HARNESS = "/app/bin/cvc_vehiclestate_harness"
_CVC_ESTOP_HARNESS = "/app/bin/cvc_estop_harness"
_CVC_CVCCOM_HARNESS = "/app/bin/cvc_cvccom_harness"
_CVC_HEARTBEAT_HARNESS = "/app/bin/cvc_heartbeat_harness"
_CVC_CANMONITOR_HARNESS = "/app/bin/cvc_canmonitor_harness"
_CVC_WATCHDOG_HARNESS = "/app/bin/cvc_watchdog_harness"
_CVC_SELFTEST_HARNESS = "/app/bin/cvc_selftest_harness"
_CVC_SCHEDULER_HARNESS = "/app/bin/cvc_scheduler_harness"
_CVC_NVM_HARNESS = "/app/bin/cvc_nvm_harness"
_FZC_NVM_HARNESS = "/app/bin/fzc_nvm_harness"
_FZC_STEERING_HARNESS = "/app/bin/fzc_steering_harness"
_FZC_BRAKE_HARNESS = "/app/bin/fzc_brake_harness"
_FZC_LIDAR_HARNESS = "/app/bin/fzc_lidar_harness"
_RZC_SAFETY_HARNESS = "/app/bin/rzc_safety_harness"
_RZC_SELFTEST_HARNESS = "/app/bin/rzc_selftest_harness"
_RZC_ENCODER_HARNESS = "/app/bin/rzc_encoder_harness"
_RZC_HEARTBEAT_HARNESS = "/app/bin/rzc_heartbeat_harness"
_RZC_CURRENTMONITOR_HARNESS = "/app/bin/rzc_currentmonitor_harness"
_RZC_MOTOR_HARNESS = "/app/bin/rzc_motor_harness"
_RZC_BATTERY_HARNESS = "/app/bin/rzc_battery_harness"
_RZC_TEMPMONITOR_HARNESS = "/app/bin/rzc_temponitor_harness"
_RZC_RZCCOM_HARNESS = "/app/bin/rzc_rzccom_harness"
_FZC_FZCCOM_HARNESS = "/app/bin/fzc_fzccom_harness"
_FZC_HEARTBEAT_HARNESS = "/app/bin/fzc_heartbeat_harness"
_FZC_CANMONITOR_HARNESS = "/app/bin/fzc_canmonitor_harness"
_FZC_SAFETY_HARNESS = "/app/bin/fzc_safety_harness"
_FZC_SCHEDULER_HARNESS = "/app/bin/fzc_scheduler_harness"
_RZC_SCHEDULER_HARNESS = "/app/bin/rzc_scheduler_harness"
_RZC_NVM_HARNESS = "/app/bin/rzc_nvm_harness"
_SC_STATE_HARNESS = "/app/bin/sc_state_harness"
_SC_HEARTBEAT_HARNESS = "/app/bin/sc_heartbeat_harness"
_SC_E2E_HARNESS = "/app/bin/sc_e2e_harness"
_SC_RELAY_HARNESS = "/app/bin/sc_relay_harness"
_SC_PLAUSIBILITY_HARNESS = "/app/bin/sc_plausibility_harness"
_SC_WATCHDOG_HARNESS = "/app/bin/sc_watchdog_harness"

_SC_SELFTEST_HARNESS = "/app/bin/sc_selftest_harness"


def _vehicle_state_value(name: str) -> int:
    states = {
        "INIT": 0,
        "RUN": 1,
        "DEGRADED": 2,
        "LIMP": 3,
        "SAFE_STOP": 4,
        "SHUTDOWN": 5,
    }
    key = name.strip().upper()
    if key not in states:
        raise HTTPException(
            status_code=400,
            detail=f"Unsupported vehicleState '{name}'",
        )
    return states[key]


def _validate_percent(name: str, value: int) -> int:
    if value < 0 or value > 100:
        raise HTTPException(
            status_code=400,
            detail=f"{name} must be within 0..100",
        )
    return value


def _validate_cycles(cycles: int) -> int:
    if cycles <= 0 or cycles > 1000:
        raise HTTPException(
            status_code=400,
            detail="cycles must be within 1..1000",
        )
    return cycles


def _publish_lock_state() -> None:
    """Publish current lock state to MQTT with retain."""
    if _mqtt_client is None:
        return
    now = time.time()
    with _lock_mu:
        locked = (
            _control_lock["client_id"] is not None
            and now < _control_lock["expires_at"]
        )
        payload = {
            "locked": locked,
            "client_id": _control_lock["client_id"] or "",
            "remaining_sec": max(0, int(_control_lock["expires_at"] - now)) if locked else 0,
            "acquired_at": _control_lock["acquired_at"] if locked else 0.0,
        }
    _mqtt_client.publish(
        "taktflow/control/lock",
        json.dumps(payload),
        qos=0,
        retain=True,
    )


def _lock_watchdog() -> None:
    """Background thread: auto-expire lock and publish updated remaining_sec."""
    was_locked = False
    while True:
        time.sleep(1)
        with _lock_mu:
            is_locked = _control_lock["client_id"] is not None
            if is_locked and time.time() >= _control_lock["expires_at"]:
                log.info("Control lock expired for %s", _control_lock["client_id"])
                _control_lock["client_id"] = None
                _control_lock["expires_at"] = 0.0
                _control_lock["acquired_at"] = 0.0
                is_locked = False
        # Publish while locked, and one final time after expiry to clear retained msg
        if is_locked or was_locked:
            _publish_lock_state()
        was_locked = is_locked


def _idle_command_loop() -> None:
    """Background thread: keep CAN bus handle alive and drive an idle
    cruise profile so the demo shows the vehicle moving.

    Profile (paused while a scenario is active or when the control
    lock is held by a remote client):
      phase A (2.0s): ramp pedal 0% -> IDLE_CRUISE_PCT
      phase B (6.0s): hold at IDLE_CRUISE_PCT  (~15-20 km/h)
      phase C (2.0s): ramp back to 0%
      phase D (2.0s): idle — vehicle coasts to stop
      repeat

    Driving both sensors through the SPI UDP override also stabilises
    the otherwise jittering PedalPosition1 that was tripping CVC's
    plausibility check every few seconds in idle.
    """
    from .pedal_udp import (
        send_pedal_override,
        send_pedal_neutral,
        clear_pedal_override,
        pedal_pct_to_angle,
    )

    # 60% duty maps to ~2400 RPM -> ~17 km/h with the ws_bridge
    # wheel/gear factor (0.15 m radius, 8:1). Tuned by observation.
    IDLE_CRUISE_PCT = float(os.environ.get("IDLE_CRUISE_PCT", "60"))
    # Default OFF so test scenarios see a quiet pedal. Set
    # IDLE_CRUISE_ENABLE=1 in the compose env to run the demo cruise
    # loop in-between tests. If off, the loop holds a neutral pedal
    # override so the POSIX SPI fallback cannot jitter into plausibility
    # faults while the demo is idle.
    IDLE_CRUISE_ENABLE = os.environ.get("IDLE_CRUISE_ENABLE", "0") == "1"
    PHASE_RAMP_UP_SEC   = 2.0
    PHASE_HOLD_SEC      = 6.0
    PHASE_RAMP_DOWN_SEC = 2.0
    PHASE_COAST_SEC     = 2.0
    TICK = 0.1  # 10 Hz pedal updates

    bus = None
    t_phase = 0.0
    phase = "ramp_up"
    neutral_logged = False

    def _send_pct(pct: float) -> None:
        try:
            send_pedal_override(pedal_pct_to_angle(pct))
        except Exception as exc:  # noqa: BLE001
            log.debug("Idle pedal send failed: %s", exc)

    def _send_neutral() -> None:
        nonlocal neutral_logged
        try:
            send_pedal_neutral()
            if not neutral_logged:
                log.info("Idle command loop: holding neutral pedal override")
                neutral_logged = True
        except Exception as exc:  # noqa: BLE001
            log.debug("Neutral pedal send failed: %s", exc)

    while True:
        try:
            # Pause cruise while a scenario is running OR a remote user
            # holds control (so manual pedal inputs from the UI aren't
            # fought by this loop).
            now = time.time()
            with _lock_mu:
                remote_locked = (
                    _control_lock["client_id"] is not None
                    and now < _control_lock["expires_at"]
                    and _control_lock["client_id"] != "taktflow-fault-inject"
                )

            post_scenario_pause = now < _idle_paused_until
            if _idle_paused or post_scenario_pause:
                # Just stop sending pedal updates — do NOT clear the
                # override. Scenarios like runaway_accel set their own
                # SPI pedal value (100%) and the test then observes the
                # vehicle's reaction for several seconds. Calling
                # clear_pedal_override here wipes the scenario's pedal
                # on the very next tick, so the observation window sees
                # pedal=0 and the vehicle never leaves RUN.
                phase = "ramp_up"
                t_phase = 0.0
                neutral_logged = False
                time.sleep(TICK)
                continue

            if (not IDLE_CRUISE_ENABLE) or remote_locked:
                # The web dashboard control lock does not stream manual
                # pedal data. Keep CVC's POSIX SPI stub pinned at neutral
                # so its default dead-zone oscillation cannot trip the
                # pedal plausibility monitor while the demo is idle.
                _send_neutral()
                phase = "ramp_up"
                t_phase = 0.0
                time.sleep(TICK)
                continue

            if bus is None:
                bus = _get_bus()
                log.info("Idle command loop: cruise profile started "
                         "(cruise_pct=%.0f)", IDLE_CRUISE_PCT)

            if phase == "ramp_up":
                pct = IDLE_CRUISE_PCT * min(1.0, t_phase / PHASE_RAMP_UP_SEC)
                _send_pct(pct)
                if t_phase >= PHASE_RAMP_UP_SEC:
                    phase, t_phase = "hold", 0.0
                    continue
            elif phase == "hold":
                _send_pct(IDLE_CRUISE_PCT)
                if t_phase >= PHASE_HOLD_SEC:
                    phase, t_phase = "ramp_down", 0.0
                    continue
            elif phase == "ramp_down":
                pct = IDLE_CRUISE_PCT * max(0.0, 1.0 - (t_phase / PHASE_RAMP_DOWN_SEC))
                _send_pct(pct)
                if t_phase >= PHASE_RAMP_DOWN_SEC:
                    clear_pedal_override()
                    phase, t_phase = "coast", 0.0
                    continue
            else:  # coast
                # No UDP traffic — SPI stub leaves both sensors at 0.
                if t_phase >= PHASE_COAST_SEC:
                    phase, t_phase = "ramp_up", 0.0
                    continue

            t_phase += TICK
        except Exception as exc:
            log.warning("Idle command loop error: %s", exc)
            bus = None
        time.sleep(TICK)


def _init_mqtt() -> paho_mqtt.Client:
    """Initialize MQTT client for fault-inject command publishing."""
    host = os.environ.get("MQTT_HOST", "localhost")
    port = int(os.environ.get("MQTT_PORT", "1883"))
    client = paho_mqtt.Client(
        paho_mqtt.CallbackAPIVersion.VERSION2,
        client_id="taktflow-fault-inject",
    )

    def _on_connect(client, userdata, flags, rc, properties=None):
        rc_val = rc.value if hasattr(rc, 'value') else rc
        if rc_val == 0:
            log.info("MQTT connected to %s:%d", host, port)
            # Re-subscribe test runner topics on reconnect
            if _test_runner is not None:
                _test_runner.on_mqtt_connect(client, userdata, flags, rc, properties)
        else:
            log.error("MQTT connect failed: rc=%s", rc)

    def _on_disconnect(client, userdata, flags, rc, properties=None):
        log.warning("MQTT disconnected (rc=%s) — will auto-reconnect", rc)

    client.on_connect = _on_connect
    client.on_disconnect = _on_disconnect
    mqtt_user = os.environ.get("MQTT_USER", "")
    mqtt_pass = os.environ.get("MQTT_PASSWORD", "")
    if mqtt_user:
        client.username_pw_set(mqtt_user, mqtt_pass)
    client.connect_async(host, port, keepalive=30)
    client.loop_start()
    log.info("MQTT client connecting to %s:%d", host, port)
    return client


app = FastAPI(
    title="Taktflow Fault Injection API",
    description="Trigger CAN fault scenarios for the Taktflow embedded demo.",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "https://taktflow-systems.com",
        "https://www.taktflow-systems.com",
        "http://localhost:3000",
    ],
    allow_credentials=True,
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)

# --- Coverage report static files ---
# .gcda files are written to /app/build (where .o files were compiled).
# We copy .gcno there and point lcov at the same directory.
# HTML report goes to /app/coverage/html (bind-mounted to host).
_COVERAGE_DIR = "/app/build"
_COVERAGE_HTML_DIR = os.path.join("/app/coverage", "html")
os.makedirs(_COVERAGE_HTML_DIR, exist_ok=True)
# Placeholder index so the static mount doesn't 404 before first report
_index_path = os.path.join(_COVERAGE_HTML_DIR, "index.html")
if not os.path.exists(_index_path):
    with open(_index_path, "w") as _f:
        _f.write("""<!DOCTYPE html><html><body>
<h1>CVC Pedal Torque Request — Coverage Report</h1>
<p>No coverage data collected yet. Run a pedal-torque test first.</p>
</body></html>""")
app.mount("/coverage", StaticFiles(directory=_COVERAGE_HTML_DIR, html=True), name="coverage")


class PedalSetupBody(BaseModel):
    vehicleState: str | None = None
    cycles: int | None = None
    spiFaultSensor: int | None = None
    ditherAmplitude: int | None = None
    recoverCycles: int | None = None
    resetSpiFault: bool = False
    resetDither: bool = False
    resetRecover: bool = False


@app.post("/api/test/asw/cvc/pedal-torque/setup")
def setup_pedal_state(body: PedalSetupBody):
    """Store pedal test state for subsequent calls that omit fields."""
    global _stored_vehicle_state, _stored_cycles, _stored_spi_fault_sensor, _stored_dither_amplitude, _stored_recover_cycles
    if body.vehicleState is not None:
        _stored_vehicle_state = _vehicle_state_value(body.vehicleState)
    if body.cycles is not None:
        _stored_cycles = body.cycles
    if body.resetSpiFault:
        _stored_spi_fault_sensor = None
    if body.resetDither:
        _stored_dither_amplitude = None
    if body.resetRecover:
        _stored_recover_cycles = None
    if body.spiFaultSensor is not None:
        _stored_spi_fault_sensor = body.spiFaultSensor
    if body.ditherAmplitude is not None:
        _stored_dither_amplitude = body.ditherAmplitude
    if body.recoverCycles is not None:
        _stored_recover_cycles = body.recoverCycles
    return {"storedVehicleState": body.vehicleState, "storedCycles": _stored_cycles,
            "storedSpiFaultSensor": _stored_spi_fault_sensor, "storedDitherAmplitude": _stored_dither_amplitude,
            "storedRecoverCycles": _stored_recover_cycles}


def _trigger_scenario(name: str):
    """Trigger a scenario by name (used by the test runner).

    Mirrors the idle-cruise gating that the HTTP trigger does. Without
    this, the test runner fires scenarios without pausing cruise, so
    cruise keeps pushing its 60% pedal into the SPI stub during the
    scenario's observation window — the scenario's clear / 100% / zero
    pedal gets overwritten on the very next cruise tick.
    """
    global _idle_paused, _idle_paused_until
    entry = SCENARIOS.get(name)
    if not entry:
        return
    _idle_paused = True
    try:
        entry["fn"]()
    finally:
        _idle_paused_until = time.time() + SCENARIO_POST_PAUSE_SEC
        _idle_paused = False


@app.on_event("startup")
def _on_startup():
    global _mqtt_client, _test_runner
    _mqtt_client = _init_mqtt()
    set_mqtt_client(_mqtt_client)
    # Always publish initial unlocked state to clear any stale retained lock payload.
    _publish_lock_state()
    # Initialize test runner
    _test_runner = DashboardTestRunner(_mqtt_client, _trigger_scenario, reset_scenario)
    # Start lock watchdog (daemon thread — dies with process)
    t = threading.Thread(target=_lock_watchdog, daemon=True)
    t.start()
    log.info("Control lock watchdog started (duration=%ds)", LOCK_DURATION_SEC)
    # Start idle command loop (virtual pedal ECU — keeps FZC alive)
    t2 = threading.Thread(target=_idle_command_loop, daemon=True)
    t2.start()
    log.info("Idle command loop started (interval=%.0fms)", IDLE_CMD_INTERVAL * 1000)


# ---------------------------------------------------------------------------
# Control lock endpoints
# ---------------------------------------------------------------------------

@app.post("/api/fault/control/acquire")
def acquire_control(body: ClientIdBody, request: Request):
    """Acquire 5-min control lock. Returns 409 if already held by someone else."""
    _check_api_key(request)
    now = time.time()
    with _lock_mu:
        # Check if lock is already held (and not expired)
        if (
            _control_lock["client_id"] is not None
            and _control_lock["client_id"] != body.client_id
            and now < _control_lock["expires_at"]
        ):
            remaining = int(_control_lock["expires_at"] - now)
            raise HTTPException(
                status_code=409,
                detail=f"Another user has control",
                headers={"X-Remaining-Sec": str(remaining)},
            )
        _control_lock["client_id"] = body.client_id
        _control_lock["expires_at"] = now + LOCK_DURATION_SEC
        _control_lock["acquired_at"] = now
    log.info("Control acquired by %s for %ds", body.client_id, LOCK_DURATION_SEC)
    _publish_lock_state()
    return {
        "locked": True,
        "client_id": body.client_id,
        "remaining_sec": LOCK_DURATION_SEC,
    }


@app.post("/api/fault/control/release")
def release_control(body: ClientIdBody, request: Request):
    """Release control lock early. Returns 403 if not the holder."""
    _check_api_key(request)
    with _lock_mu:
        if _control_lock["client_id"] != body.client_id:
            raise HTTPException(status_code=403, detail="Not the lock holder")
        _control_lock["client_id"] = None
        _control_lock["expires_at"] = 0.0
        _control_lock["acquired_at"] = 0.0
    log.info("Control released by %s", body.client_id)
    _publish_lock_state()
    return {"locked": False}


@app.get("/api/fault/control/status")
def control_status():
    """Return current lock state."""
    now = time.time()
    with _lock_mu:
        locked = (
            _control_lock["client_id"] is not None
            and now < _control_lock["expires_at"]
        )
        return {
            "locked": locked,
            "client_id": _control_lock["client_id"] or "" if locked else "",
            "remaining_sec": max(0, int(_control_lock["expires_at"] - now)) if locked else 0,
        }


# ---------------------------------------------------------------------------
# API key guard
# ---------------------------------------------------------------------------

def _check_api_key(request: Request) -> None:
    """Reject mutating requests without a valid API key (if configured)."""
    if not FAULT_API_KEY:
        return  # No key configured — dev mode, allow all
    key = request.headers.get("X-Api-Key", "")
    if key != FAULT_API_KEY:
        raise HTTPException(status_code=401, detail="Invalid or missing API key")


# ---------------------------------------------------------------------------
# Lock guard helper
# ---------------------------------------------------------------------------

def _check_control_lock(request: Request) -> None:
    """If a lock is active and the requester is not the holder, reject with 403."""
    now = time.time()
    with _lock_mu:
        if _control_lock["client_id"] is None or now >= _control_lock["expires_at"]:
            return  # No active lock — allow
        caller = request.headers.get("X-Client-Id", "")
        if caller == _control_lock["client_id"]:
            return  # Caller is the lock holder — allow
        remaining = int(_control_lock["expires_at"] - now)
    raise HTTPException(
        status_code=403,
        detail="Another user has control",
        headers={"X-Remaining-Sec": str(remaining)},
    )


# ---------------------------------------------------------------------------
# Fault scenario endpoints
# ---------------------------------------------------------------------------

@app.post("/api/fault/scenario/{name}")
def trigger_scenario(name: str, request: Request):
    """Trigger a fault scenario by name."""
    _check_api_key(request)
    _check_control_lock(request)
    entry = SCENARIOS.get(name)
    if entry is None:
        raise HTTPException(
            status_code=404,
            detail=f"Unknown scenario '{name}'.  "
                   f"Available: {', '.join(SCENARIOS.keys())}",
        )
    global _idle_paused, _idle_paused_until
    _idle_paused = True
    log.info("Triggering scenario: %s (idle commands paused)", name)
    try:
        result = entry["fn"]()
    except Exception as exc:
        log.error("Scenario '%s' failed: %s", name, exc)
        raise HTTPException(
            status_code=500,
            detail=f"Scenario '{name}' failed: {exc}",
        ) from exc
    finally:
        # Keep cruise suppressed for the observation window after the
        # scenario returns. A scenario like runaway_accel does a single
        # SPI pedal override at 100% and returns immediately; without
        # this deferral, the cruise loop resumes on the next tick and
        # overwrites the 100% with its 60% cruise target, so the test
        # never observes the commanded torque -> no DEGRADED transition.
        _idle_paused_until = time.time() + SCENARIO_POST_PAUSE_SEC
        _idle_paused = False
    log.info("Scenario '%s' complete: %s (cruise resumes in %.1fs)",
             name, result, SCENARIO_POST_PAUSE_SEC)
    return {"scenario": name, "result": result}


@app.post("/api/fault/reset")
def reset_all(request: Request):
    """Power-cycle reset: restart ECU containers to clear all latched faults."""
    _check_api_key(request)
    _check_control_lock(request)
    global _idle_paused, _idle_paused_until
    log.info("Power-cycle reset initiated")
    try:
        result = reset_scenario()
    except Exception as exc:
        log.error("Reset failed: %s", exc)
        raise HTTPException(
            status_code=500,
            detail=f"Reset failed: {exc}",
        ) from exc
    _idle_paused = False
    _idle_paused_until = 0.0
    log.info("Reset complete: %s (idle commands resumed)", result)
    return {"result": result}


@app.get("/api/fault/scenarios")
def list_scenarios():
    """List all available fault scenarios with descriptions."""
    return {
        "scenarios": {
            name: entry["description"]
            for name, entry in SCENARIOS.items()
        }
    }


@app.get("/api/fault/health")
def health_check():
    """Health check endpoint."""
    return {
        "status": "ok",
        "service": "fault_inject",
        "can_channel": os.environ.get("CAN_CHANNEL", "vcan0"),
    }


# ---------------------------------------------------------------------------
# E2E test suite endpoints
# ---------------------------------------------------------------------------

@app.post("/api/test/run")
def start_test_run(body: TestRunBody, request: Request):
    """Start E2E test suite. Requires control lock."""
    _check_api_key(request)
    _check_control_lock(request)
    if _test_runner is None:
        raise HTTPException(status_code=503, detail="Test runner not initialized")
    try:
        run_id = _test_runner.start(body.tests)
    except RuntimeError:
        raise HTTPException(status_code=409, detail="Test run already in progress")
    return {"run_id": run_id, "state": "running"}


@app.post("/api/test/stop")
def stop_test_run(request: Request):
    """Stop the running test suite after the current scenario."""
    _check_api_key(request)
    _check_control_lock(request)
    if _test_runner is None:
        raise HTTPException(status_code=503, detail="Test runner not initialized")
    if not _test_runner.stop():
        raise HTTPException(status_code=409, detail="No test run in progress")
    return {"state": "stopping"}


@app.get("/api/test/specs")
def list_test_specs():
    """List all available E2E test specs for UI selection."""
    from .test_specs import TEST_SPECS
    return {
        "specs": [
            {
                "id": s.id,
                "label": s.label,
                "sg": s.sg,
                "asil": s.asil,
                "he": s.he,
                "description": s.description,
            }
            for s in TEST_SPECS
        ]
    }


@app.get("/api/test/status")
def test_status():
    """Current test run state."""
    if _test_runner is None:
        return {"state": "idle"}
    return {"state": _test_runner.status, "run_id": _test_runner._run_id or ""}


@app.get("/api/test/result")
def test_result():
    """Last completed test run result."""
    if _test_runner is None or _test_runner.last_result is None:
        return {"state": "idle", "results": []}
    return _test_runner.last_result


def _collect_ecu_coverage_info(profdata_bin: str, llvmcov_bin: str) -> str | None:
    """Export coverage from the instrumented *real* CVC ECU binary.

    The CVC SIL ECU is built with LLVM_COV=1 and periodically flushes
    .profraw into the shared /cov volume. The true-E2E vcan0 tests
    (bus-probe / cadence / ftti) exercise this real binary naturally, so
    this trace carries genuine BSW module coverage (Com/E2E/PduR/CanIf/
    MCAL…) — that is exactly the coverage the BSW features are about.

    Returns the generated .info path, or None when the ECU chain has no
    coverage data (fail-open so the rest of the report still generates).
    """
    cov_dir = os.environ.get("SIL_COV_DIR", "/cov")
    ecu_binary = "/app/ecu_bins/cvc"
    ecu_prefix = "ecucov_cvc"

    profraws = sorted(
        f for f in os.listdir(cov_dir)
        if f.startswith("cvc_") and f.endswith(".profraw")
    )
    if not profraws:
        log.warning("No CVC ECU .profraw in %s — ECU coverage chain skipped",
                    cov_dir)
        return None

    # Make the instrumented binary available to llvm-cov (its coverage
    # mapping lives in the binary itself).
    try:
        from .scenarios import _docker_client
        cvc = _docker_client().containers.get("docker-cvc-1")
        bits, _stat = cvc.get_archive("/usr/local/bin/cvc")
        os.makedirs("/app/ecu_bins", exist_ok=True)
        with open(ecu_binary, "wb") as fh:
            import io
            import tarfile
            stream = io.BytesIO(b"".join(bits))
            with tarfile.open(fileobj=stream) as tar:
                member = tar.next()
                if member is None:
                    raise RuntimeError("empty archive for cvc binary")
                fh.write(tar.extractfile(member).read())
    except Exception as exc:
        log.warning("Cannot fetch instrumented CVC binary: %s", exc)
        return None

    profdata_file = os.path.join(_COVERAGE_DIR, ecu_prefix + ".profdata")
    subprocess.run(
        [profdata_bin, "merge", "-sparse", "-o", profdata_file]
        + [os.path.join(cov_dir, p) for p in profraws],
        capture_output=True, text=True, timeout=60,
    )

    info_file = os.path.join(_COVERAGE_DIR, ecu_prefix + ".info")
    result = subprocess.run(
        [llvmcov_bin, "export", ecu_binary,
         "-instr-profile=" + profdata_file, "-format=lcov",
         "-ignore-filename-regex=/app/fault_inject/.*",
         "-ignore-filename-regex=/app/build/.*",
         "-ignore-filename-regex=/usr/.*"],
        capture_output=True, text=True, timeout=120,
    )
    with open(info_file, "w") as f:
        f.write(result.stdout)
    log.info("ECU coverage chain exported: %d profraw files", len(profraws))
    return info_file


def _generate_coverage_html():
    """Run LLVM coverage tools + genhtml to produce HTML coverage report."""
    info_file = os.path.join(_COVERAGE_DIR, "coverage.info")
    profdata_bin = "/usr/bin/llvm-profdata"
    llvmcov_bin = "/usr/bin/llvm-cov"
    genhtml_bin = "/usr/bin/genhtml"
    if not all(os.path.exists(b) for b in [profdata_bin, llvmcov_bin, genhtml_bin]):
        log.warning("llvm-profdata/llvm-cov/genhtml not found — skipping coverage")
        return

    # True-E2E coverage: collected with the harness chains in Step 1 so the
    # lcov merge below aggregates both the harness and ECU coverage traces.

    # One harness binary per instrumented chain. Profiles from different
    # binaries cannot be merged into a single .profdata (each binary has its
    # own instrumentation mapping), so we export per-binary lcov traces and
    # merge those with lcov (which correctly aggregates shared source files).
    #
    # The ignores exclude the harness test code (/app/fault_inject) and any
    # BSW dependencies linked into ASW harnesses (/app/firmware/bsw), so the
    # report only shows the production sources under test. BSW harnesses that
    # link coverable generated or BSW-module sources (e.g. Rte_TaskBodies)
    # appear automatically because their files live outside these ignored
    # paths.
    harnesses = [
        ("cvc_pedal", _CVC_PEDAL_HARNESS),
        ("cvc_vsm", _CVC_VSM_HARNESS),
        ("cvc_estop", _CVC_ESTOP_HARNESS),
        ("cvc_cvccom", _CVC_CVCCOM_HARNESS),
        ("cvc_heartbeat", _CVC_HEARTBEAT_HARNESS),
        ("cvc_canmonitor", _CVC_CANMONITOR_HARNESS),
        ("cvc_watchdog", _CVC_WATCHDOG_HARNESS),
        ("cvc_selftest", _CVC_SELFTEST_HARNESS),
        ("cvc_scheduler", _CVC_SCHEDULER_HARNESS),
        ("cvc_nvm", _CVC_NVM_HARNESS),
        ("fzc_nvm", _FZC_NVM_HARNESS),
        ("fzc_steering", _FZC_STEERING_HARNESS),
        ("fzc_brake", _FZC_BRAKE_HARNESS),
        ("fzc_lidar", _FZC_LIDAR_HARNESS),
        ("rzc_safety", _RZC_SAFETY_HARNESS),
        ("rzc_selftest", _RZC_SELFTEST_HARNESS),
        ("rzc_encoder", _RZC_ENCODER_HARNESS),
        ("rzc_heartbeat", _RZC_HEARTBEAT_HARNESS),
        ("rzc_currentmonitor", _RZC_CURRENTMONITOR_HARNESS),
        ("rzc_motor", _RZC_MOTOR_HARNESS),
        ("rzc_battery", _RZC_BATTERY_HARNESS),
        ("rzc_temponitor", _RZC_TEMPMONITOR_HARNESS),
        ("rzc_rzccom", _RZC_RZCCOM_HARNESS),
        ("fzc_fzccom", _FZC_FZCCOM_HARNESS),
        ("fzc_heartbeat", _FZC_HEARTBEAT_HARNESS),
        ("fzc_canmonitor", _FZC_CANMONITOR_HARNESS),
        ("fzc_safety", _FZC_SAFETY_HARNESS),
        ("fzc_scheduler", _FZC_SCHEDULER_HARNESS),
        ("rzc_scheduler", _RZC_SCHEDULER_HARNESS),
        ("rzc_nvm", _RZC_NVM_HARNESS),
        ("sc_state", _SC_STATE_HARNESS),
        ("sc_heartbeat", _SC_HEARTBEAT_HARNESS),
        ("sc_e2e", _SC_E2E_HARNESS),
        ("sc_relay", _SC_RELAY_HARNESS),
        ("sc_plausibility", _SC_PLAUSIBILITY_HARNESS),
        ("sc_watchdog", _SC_WATCHDOG_HARNESS),
        ("sc_selftest", _SC_SELFTEST_HARNESS),
        # NOTE: the BSW true-E2E features (bsw_comcfg_cvc / bsw_rtetaskbodies
        # cvc) no longer drive native harnesses; their coverage comes from
        # the instrumented real CVC ECU (see _collect_ecu_coverage_info).
    ]

    # Step 1: per-binary merge + export
    info_files = []

    # True-E2E coverage from the instrumented real CVC ECU (vcan0 bus tests).
    ecu_info = _collect_ecu_coverage_info(profdata_bin, llvmcov_bin)
    if ecu_info is not None:
        info_files.append(ecu_info)

    for prefix, harness in harnesses:
        profraw_files = sorted(
            os.path.join(_COVERAGE_DIR, f)
            for f in os.listdir(_COVERAGE_DIR)
            if f.startswith(prefix + "_") and f.endswith(".profraw")
        )
        if not profraw_files:
            log.warning("No .profraw files found for %s in %s", prefix, _COVERAGE_DIR)
            continue

        profdata_file = os.path.join(_COVERAGE_DIR, prefix + ".profdata")
        subprocess.run(
            [profdata_bin, "merge", "-sparse", "-o", profdata_file] + profraw_files,
            capture_output=True, text=True, timeout=30,
        )

        prefix_info = os.path.join(_COVERAGE_DIR, prefix + ".info")
        result = subprocess.run(
            [llvmcov_bin, "export", harness,
             "-instr-profile=" + profdata_file, "-format=lcov",
             "-ignore-filename-regex=/app/fault_inject/.*",
             "-ignore-filename-regex=/app/firmware/bsw/.*",
             "-ignore-filename-regex=/usr/.*"],
            capture_output=True, text=True, timeout=30,
        )
        with open(prefix_info, "w") as f:
            f.write(result.stdout)
        info_files.append(prefix_info)

    if not info_files:
        log.warning("No coverage data to report")
        return

    # Step 2: merge lcov traces (aggregates duplicate SF records, e.g.
    # Swc_VehicleState.c appears in both the pedal and VSM harness binaries).
    # NOTE: lcov's default lcovrc sets branch_coverage=0, which drops BRDA
    # records on --add-tracefile. Force branch_coverage=1 to keep branch data.
    if len(info_files) == 1:
        shutil.copyfile(info_files[0], info_file)
    else:
        lcov_bin = "/usr/bin/lcov"
        cmd = [lcov_bin, "--rc", "branch_coverage=1",
               "--ignore-errors", "inconsistent,corrupt,unsupported"]
        for f in info_files:
            cmd += ["--add-tracefile", f]
        cmd += ["--output-file", info_file]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            log.warning("lcov merge failed: %s", (result.stderr or result.stdout)[-500:])

    # Step 3: Generate HTML
    # genhtml does not remove stale HTML for files that disappeared from the
    # trace (e.g. a harness removed from the coverage list), so wipe the
    # output directory first to keep the report in sync with the data.
    if os.path.isdir(_COVERAGE_HTML_DIR):
        shutil.rmtree(_COVERAGE_HTML_DIR, ignore_errors=True)
    os.makedirs(_COVERAGE_HTML_DIR, exist_ok=True)
    subprocess.run(
        [genhtml_bin, info_file, "--output-directory", _COVERAGE_HTML_DIR,
         "--title", "Taktflow ASW Coverage Report",
         "--prefix", "/app",
         "--rc", "branch_coverage=1", "--legend",
         "--ignore-errors", "unsupported,deprecated,inconsistent,corrupt"],
        capture_output=True, text=True, timeout=30,
    )
    log.info("Coverage HTML report written to %s", _COVERAGE_HTML_DIR)


@app.post("/api/test/asw/cvc/pedal-torque")
def run_cvc_pedal_torque(body: CvcPedalTorqueBody):
    """Execute the real CVC pedal ASW chain in a native test harness.

    Coverage data (.gcda) is written to /app/coverage/ and accumulated
    across calls. The merged HTML report is generated on demand via the
    GET /coverage endpoint (triggered automatically by doLast in gradle).
    """
    sensor1 = _validate_percent("sensor1Pct", body.sensor1Pct)
    sensor2 = _validate_percent("sensor2Pct", body.sensor2Pct)
    cycles = _validate_cycles(body.cycles) if body.cycles is not None else _stored_cycles
    vehicle_state = _vehicle_state_value(body.vehicleState) if body.vehicleState is not None else _stored_vehicle_state
    spi_fault = body.spiFaultSensor if body.spiFaultSensor is not None else _stored_spi_fault_sensor
    dither = body.ditherAmplitude if body.ditherAmplitude is not None else _stored_dither_amplitude
    recover_cycles = body.recoverCycles if body.recoverCycles is not None else _stored_recover_cycles

    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_pedal_%p.profraw")
        if dither is not None:
            env["CVC_PEDAL_DITHER"] = str(dither)
        if body.bridgeRx:
            env["CVC_PEDAL_BRIDGE_RX"] = "1"
        if body.getPosition:
            env["CVC_PEDAL_GET_POS"] = "1"
        rx_env = {
            "CVC_PEDAL_RX_BRAKE_FAULT": body.rxBrakeFault,
            "CVC_PEDAL_RX_MOTOR_CUTOFF": body.rxMotorCutoff,
            "CVC_PEDAL_RX_BATTERY": body.rxBattery,
            "CVC_PEDAL_RX_STEER_FAULT": body.rxSteeringFault,
            "CVC_PEDAL_RX_MOTOR_FAULT": body.rxMotorFault,
            "CVC_PEDAL_RX_SC_RELAY": body.rxScRelay,
            "CVC_PEDAL_RX_FZC_ALIVE": body.rxFzcAlive,
            "CVC_PEDAL_RX_RZC_ALIVE": body.rxzAlive,
        }
        for k, v in rx_env.items():
            if v is not None:
                env[k] = str(v)
        harness_args = [
            _CVC_PEDAL_HARNESS,
            str(sensor1),
            str(sensor2),
            str(vehicle_state),
            str(cycles),
        ]
        if spi_fault is not None:
            harness_args.append(str(spi_fault))
        if recover_cycles is not None and recover_cycles > 0:
            harness_args.append(str(body.recoverSensor1Pct or 0))
            harness_args.append(str(body.recoverSensor2Pct or 0))
            harness_args.append(str(recover_cycles))
        completed = subprocess.run(
            harness_args,
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC pedal harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC pedal harness timed out") from exc
    except subprocess.CalledProcessError as exc:
        detail = exc.stderr.strip() if exc.stderr else exc.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC pedal harness failed") from exc

    # HTML coverage report is generated on demand via the GET endpoint,
    # not per-call — this lets .gcda data accumulate across all scenarios
    # before producing a single merged report.

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC pedal harness returned invalid JSON") from exc


def _phase_to_line(p: VehicleStatePhase) -> str:
    """Serialize one VSM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"selfTestPass={_b(p.selfTestPass)}",
        f"estop={_b(p.estop)}",
        f"scRelayEnergized={_b(p.scRelayEnergized)}",
        f"fzcComm={p.fzcComm}",
        f"rzcComm={p.rzcComm}",
        f"pedalFault={_b(p.pedalFault)}",
        f"motorCutoff={_b(p.motorCutoff)}",
        f"brakeFault={_b(p.brakeFault)}",
        f"steeringFault={_b(p.steeringFault)}",
        f"batteryStatus={p.batteryStatus}",
        f"motorFaultRzc={_b(p.motorFaultRzc)}",
        f"motorSpeed={p.motorSpeed}",
        f"torqueRequest={p.torqueRequest}",
        f"pedalPosition={p.pedalPosition}",
        f"pedalFaultDual={_b(p.pedalFaultDual)}",
        f"comBrakeFault={p.comBrakeFault}",
        f"comMotorCutoff={p.comMotorCutoff}",
        f"motorPduTimedOut={_b(p.motorPduTimedOut)}",
    ])


@app.post("/api/test/asw/cvc/vehicle-state/setup")
def setup_vehicle_state(body: CvcVehicleStateSetupBody):
    """Store the VSM phase script for subsequent run calls that omit phases."""
    global _stored_vehicle_state_phases
    _stored_vehicle_state_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/vehicle-state")
def run_cvc_vehicle_state(body: CvcVehicleStateRunBody):
    """Execute the real CVC vehicle state machine in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. reaching RUN). `body.phases` carries the stimulus phases — the final
    triggering action under test. The harness runs the concatenated
    precondition + stimulus script.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_vehicle_state_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_vsm_%p.profraw")
        completed = subprocess.run(
            [_CVC_VSM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC vehicle-state harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC vehicle-state harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC vehicle-state harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC vehicle-state harness returned invalid JSON") from exc


def _estop_phase_to_line(p: CvcEStopPhase) -> str:
    """Serialize one E-stop phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"pin={p.pin}",
        f"readFail={_b(p.readFail)}",
        f"skipInit={_b(p.skipInit)}",
    ])


@app.post("/api/test/asw/cvc/estop/setup")
def setup_estop(body: CvcEStopSetupBody):
    """Store the E-stop phase script for subsequent run calls that omit phases."""
    global _stored_estop_phases
    _stored_estop_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/estop")
def run_cvc_estop(body: CvcEStopRunBody):
    """Execute the real CVC E-stop ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. E-stop released). `body.phases` carries the stimulus phases — the
    final triggering action under test (e.g. pressing the button). The harness
    runs the concatenated precondition + stimulus script against the real
    Swc_EStop.c + Swc_CvcCom.c production code.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_estop_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_estop_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_estop_%p.profraw")
        completed = subprocess.run(
            [_CVC_ESTOP_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC estop harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC estop harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC estop harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC estop harness returned invalid JSON") from exc


def _cvccom_phase_to_line(p: CvcCvcComPhase) -> str:
    """Serialize one CvcCom phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"bridgeRx={_b(p.bridgeRx)}",
        f"vehicleState={p.vehicleState}",
        f"estop={p.estop}",
        f"relayKill={p.relayKill}",
        f"motorCutoff={p.motorCutoff}",
        f"brakeFault={p.brakeFault}",
        f"steerFault={p.steerFault}",
        f"pedalFault={p.pedalFault}",
        f"fzcComm={p.fzcComm}",
        f"rzcComm={p.rzcComm}",
        f"torque={p.torque}",
        f"rxBrakeEvent={p.rxBrakeEvent}",
        f"rxBrakeStatus={p.rxBrakeStatus}",
        f"rxMotorCutoff={p.rxMotorCutoff}",
        f"rxScRelay={p.rxScRelay}",
        f"rxBattery={p.rxBattery}",
        f"rxSteerFault={p.rxSteerFault}",
        f"rxMotorFault={p.rxMotorFault}",
        f"rxFzcAlive={p.rxFzcAlive}",
        f"rxRzcAlive={p.rxRzcAlive}",
    ])


@app.post("/api/test/asw/cvc/cvccom/setup")
def setup_cvccom(body: CvcCvcComSetupBody):
    """Store the CvcCom phase script for subsequent run calls that omit phases."""
    global _stored_cvccom_phases
    _stored_cvccom_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/cvccom")
def run_cvc_cvccom(body: CvcCvcComRunBody):
    """Execute the real CVC CAN-communication ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a vehicle state + RTE fault baseline). `body.phases` carries the
    stimulus phases — the final TX/RX action under test. The harness runs the
    concatenated precondition + stimulus script against the real Swc_CvcCom.c
    production code (TransmitSchedule + optional BridgeRxToRte).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvccom_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_cvccom_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_cvccom_%p.profraw")
        completed = subprocess.run(
            [_CVC_CVCCOM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC cvccom harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC cvccom harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC cvccom harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC cvccom harness returned invalid JSON") from exc


def _heartbeat_phase_to_line(p: CvcHeartbeatPhase) -> str:
    """Serialize one heartbeat phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"vehicleState={p.vehicleState}",
        f"rxEcu={p.rxEcu}",
        f"resetComm={_b(p.resetComm)}",
    ])


@app.post("/api/test/asw/cvc/heartbeat/setup")
def setup_cvc_heartbeat(body: CvcHeartbeatSetupBody):
    """Store the heartbeat phase script for subsequent run calls that omit phases."""
    global _stored_cvc_heartbeat_phases
    _stored_cvc_heartbeat_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/heartbeat")
def run_cvc_heartbeat(body: CvcHeartbeatRunBody):
    """Execute the real CVC heartbeat ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. an RX indication baseline). `body.phases` carries the stimulus phases.
    The harness runs the concatenated precondition + stimulus script against
    the real Swc_Heartbeat.c production code (Init + MainFunction +
    RxIndication + ResetCommStatus).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_heartbeat_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_heartbeat_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_heartbeat_%p.profraw")
        completed = subprocess.run(
            [_CVC_HEARTBEAT_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC heartbeat harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC heartbeat harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC heartbeat harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC heartbeat harness returned invalid JSON") from exc


def _canmonitor_phase_to_line(p: CvcCanMonitorPhase) -> str:
    """Serialize one CAN monitor phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    toks = [
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"isBusOff={_b(p.isBusOff)}",
    ]
    if p.rxMsgCount is not None:
        toks.append(f"rxMsgCount={p.rxMsgCount}")
    toks += [
        f"rxInc={_b(p.rxInc)}",
        f"errorWarning={_b(p.errorWarning)}",
        f"timeStartMs={p.timeStartMs}",
        f"timeStepMs={p.timeStepMs}",
        f"recovery={_b(p.recovery)}",
        f"recoveryTimeMs={p.recoveryTimeMs}",
    ]
    return " ".join(toks)


@app.post("/api/test/asw/cvc/canmonitor/setup")
def setup_cvc_canmonitor(body: CvcCanMonitorSetupBody):
    """Store the CAN monitor phase script for subsequent run calls that omit phases."""
    global _stored_cvc_canmonitor_phases
    _stored_cvc_canmonitor_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/canmonitor")
def run_cvc_canmonitor(body: CvcCanMonitorRunBody):
    """Execute the real CVC CAN monitor ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a bus-off baseline or error-warning activation). `body.phases`
    carries the stimulus phases. The harness runs the concatenated
    precondition + stimulus script against the real Swc_CanMonitor.c
    production code (Init + Check + Recovery + GetStatus).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_canmonitor_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_canmonitor_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_canmonitor_%p.profraw")
        completed = subprocess.run(
            [_CVC_CANMONITOR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC CAN monitor harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC CAN monitor harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC CAN monitor harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC CAN monitor harness returned invalid JSON") from exc


def _watchdog_phase_to_line(p: CvcWatchdogPhase) -> str:
    """Serialize one watchdog phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"loopComplete={_b(p.loopComplete)}",
        f"canaryOk={_b(p.canaryOk)}",
        f"ramOk={_b(p.ramOk)}",
        f"canOk={_b(p.canOk)}",
        f"feedCount={p.feedCount}",
    ])


@app.post("/api/test/asw/cvc/watchdog/setup")
def setup_cvc_watchdog(body: CvcWatchdogSetupBody):
    """Store the watchdog phase script for subsequent run calls that omit phases."""
    global _stored_cvc_watchdog_phases
    _stored_cvc_watchdog_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/watchdog")
def run_cvc_watchdog(body: CvcWatchdogRunBody):
    """Execute the real CVC watchdog ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a successful-feed baseline). `body.phases` carries the stimulus
    phases. The harness runs the concatenated precondition + stimulus script
    against the real Swc_Watchdog.c production code (Init + Feed, with a
    mocked Dio_FlipChannel counting toggles).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_watchdog_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_watchdog_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_watchdog_%p.profraw")
        completed = subprocess.run(
            [_CVC_WATCHDOG_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC watchdog harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC watchdog harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC watchdog harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC watchdog harness returned invalid JSON") from exc


def _selftest_phase_to_line(p: CvcSelfTestPhase) -> str:
    """Serialize one self-test phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"spi={_b(p.spi)}",
        f"can={_b(p.can)}",
        f"nvm={_b(p.nvm)}",
        f"oled={_b(p.oled)}",
        f"mpu={_b(p.mpu)}",
        f"canary={_b(p.canary)}",
        f"ram={_b(p.ram)}",
    ])


@app.post("/api/test/asw/cvc/selftest/setup")
def setup_cvc_selftest(body: CvcSelfTestSetupBody):
    """Store the self-test phase script for subsequent run calls that omit phases."""
    global _stored_cvc_selftest_phases
    _stored_cvc_selftest_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/selftest")
def run_cvc_selftest(body: CvcSelfTestRunBody):
    """Execute the real CVC self-test ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a failed first run). `body.phases` carries the stimulus phases. The
    harness runs the concatenated precondition + stimulus script against the
    real Swc_SelfTest.c production code (Startup + GetResults), pinning the
    pass/fail result of each of the seven hardware diagnostic checks and
    counting the reported Dem DTC events.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_selftest_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_selftest_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_selftest_%p.profraw")
        completed = subprocess.run(
            [_CVC_SELFTEST_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC self-test harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC self-test harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC self-test harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC self-test harness returned invalid JSON") from exc


def _scheduler_phase_to_line(p: CvcSchedulerPhase) -> str:
    """Serialize one scheduler phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"nullRunnables={_b(p.nullRunnables)}",
        f"zeroCount={_b(p.zeroCount)}",
        f"tableIndex={p.tableIndex or 0}",
    ])


@app.post("/api/test/asw/cvc/scheduler/setup")
def setup_cvc_scheduler(body: CvcSchedulerSetupBody):
    """Store the scheduler phase script for subsequent run calls that omit phases."""
    global _stored_cvc_scheduler_phases
    _stored_cvc_scheduler_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/scheduler")
def run_cvc_scheduler(body: CvcSchedulerRunBody):
    """Execute the real CVC scheduler ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. an earlier valid initialization). `body.phases` carries the stimulus
    phases. The harness runs the concatenated precondition + stimulus script
    against the real Swc_Scheduler.c production code (Init + GetConfig +
    GetRunnableCount), exercising the NULL-config / null-runnables / zero-count
    guards, config replacement on re-init, and table data checks.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_scheduler_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_scheduler_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_scheduler_%p.profraw")
        completed = subprocess.run(
            [_CVC_SCHEDULER_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC scheduler harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC scheduler harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC scheduler harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC scheduler harness returned invalid JSON") from exc


def _nvm_phase_to_line(p: CvcNvmPhase) -> str:
    """Serialize one NVM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"repeats={p.repeats}",
        f"dtcId={p.dtcId}",
        f"status={p.status}",
        f"ffMode={p.ffMode}",
        f"slot={p.slot}",
        f"nullEntry={_b(p.nullEntry)}",
        f"nullCal={_b(p.nullCal)}",
        f"pThreshold={p.pThreshold}",
        f"pDebounce={p.pDebounce}",
        f"stuckThreshold={p.stuckThreshold}",
        f"stuckCycles={p.stuckCycles}",
        f"lut0={p.lut0}",
        f"dataLen={p.dataLen}",
        f"nullCrc={_b(p.nullCrc)}",
    ])


def _fzc_nvm_phase_to_line(p: FzcNvmPhase) -> str:
    """Serialize one FZC NVM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"repeats={p.repeats}",
        f"dtcId={p.dtcId}",
        f"steerAngle={p.steerAngle}",
        f"brakePos={p.brakePos}",
        f"lidarDist={p.lidarDist}",
        f"slot={p.slot}",
        f"nullRecord={_b(p.nullRecord)}",
        f"nullCal={_b(p.nullCal)}",
        f"steerCenterOffset={p.steerCenterOffset}",
        f"steerGain={p.steerGain}",
        f"brakePosOffset={p.brakePosOffset}",
        f"brakeGain={p.brakeGain}",
        f"lidarWarnCm={p.lidarWarnCm}",
        f"lidarBrakeCm={p.lidarBrakeCm}",
        f"lidarEmergencyCm={p.lidarEmergencyCm}",
        f"dataLen={p.dataLen}",
        f"nullCrc={_b(p.nullCrc)}",
    ])


@app.post("/api/test/asw/cvc/nvm/setup")
def setup_cvc_nvm(body: CvcNvmSetupBody):
    """Store the NVM phase script for subsequent run calls that omit phases."""
    global _stored_cvc_nvm_phases
    _stored_cvc_nvm_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/cvc/nvm")
def run_cvc_nvm(body: CvcNvmRunBody):
    """Execute the real CVC NVM ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a stored DTC baseline). `body.phases` carries the stimulus phases.
    The harness runs the concatenated precondition + stimulus script against
    the real Swc_Nvm.c production code (Init / StoreDtc / LoadDtc / ReadCal /
    WriteCal / CalcCrc16), with test-only hooks observing the internal
    circular-buffer state and corrupting stored CRCs to drive the
    corruption-detection paths.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_cvc_nvm_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_nvm_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "cvc_nvm_%p.profraw")
        completed = subprocess.run(
            [_CVC_NVM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="CVC NVM harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="CVC NVM harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "CVC NVM harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="CVC NVM harness returned invalid JSON") from exc


@app.post("/api/test/asw/fzc/nvm/setup")
def setup_fzc_nvm(body: FzcNvmSetupBody):
    """Store the FZC NVM phase script for subsequent run calls that omit phases."""
    global _stored_fzc_nvm_phases
    _stored_fzc_nvm_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/nvm")
def run_fzc_nvm(body: FzcNvmRunBody):
    """Execute the real FZC NVM ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real
    Swc_FzcNvm.c production code (Init / StoreDtc / LoadDtc / LoadCal /
    StoreCal / Crc16), with a mock NvM backend to verify re-init persistence
    and Init-time fallback, plus test-only hooks observing initialization
    state and corrupting in-RAM CRCs to drive fail-closed paths.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_nvm_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_nvm_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_nvm_%p.profraw")
        completed = subprocess.run(
            [_FZC_NVM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC NVM harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC NVM harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC NVM harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC NVM harness returned invalid JSON") from exc


def _fzc_steering_phase_to_line(p: FzcSteeringPhase) -> str:
    """Serialize one FZC steering phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"cmdAngle={p.cmdAngle}",
        f"rteReadFail={_b(p.rteReadFail)}",
        f"actualAngle={p.actualAngle}",
        f"actualTrack={_b(p.actualTrack)}",
        f"spiFail={_b(p.spiFail)}",
        f"getAngle={_b(p.getAngle)}",
        f"getAngleNull={_b(p.getAngleNull)}",
    ])


@app.post("/api/test/asw/fzc/steering/setup")
def setup_fzc_steering(body: FzcSteeringSetupBody):
    """Store the FZC steering phase script for subsequent run calls that omit phases."""
    global _stored_fzc_steering_phases
    _stored_fzc_steering_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/steering")
def run_fzc_steering(body: FzcSteeringRunBody):
    """Execute the real FZC steering ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy command baseline). `body.phases` carries the stimulus
    phases — the final steering action under test. The harness runs the
    concatenated precondition + stimulus script against the real Swc_Steering.c
    production code (MainFunction, RTC, fault latch, PWM/Dio/DEM).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_steering_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_steering_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_steering_%p.profraw")
        completed = subprocess.run(
            [_FZC_STEERING_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC steering harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC steering harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC steering harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC steering harness returned invalid JSON") from exc


def _fzc_brake_phase_to_line(p: FzcBrakePhase) -> str:
    """Serialize one FZC brake phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"cmdBrake={p.cmdBrake}",
        f"rteReadFail={_b(p.rteReadFail)}",
        f"estop={p.estop}",
        f"actualPos={p.actualPos}",
        f"actualTrack={_b(p.actualTrack)}",
        f"posReadFail={_b(p.posReadFail)}",
        f"getPos={_b(p.getPos)}",
        f"getPosNull={_b(p.getPosNull)}",
    ])


@app.post("/api/test/asw/fzc/brake/setup")
def setup_fzc_brake(body: FzcBrakeSetupBody):
    """Store the FZC brake phase script for subsequent run calls that omit phases."""
    global _stored_fzc_brake_phases
    _stored_fzc_brake_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/brake")
def run_fzc_brake(body: FzcBrakeRunBody):
    """Execute the real FZC brake ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy command baseline). `body.phases` carries the stimulus
    phases — the final brake action under test. The harness runs the
    concatenated precondition + stimulus script against the real Swc_Brake.c
    production code (MainFunction, clamp, E-stop, timeout/oscillation
    detection, PWM deviation, fault latch, motor cutoff, DEM).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_brake_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_brake_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_brake_%p.profraw")
        completed = subprocess.run(
            [_FZC_BRAKE_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC brake harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC brake harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC brake harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC brake harness returned invalid JSON") from exc


def _fzc_lidar_phase_to_line(p: FzcLidarPhase) -> str:
    """Serialize one FZC lidar phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"distCm={p.distCm}",
        f"signal={p.signal}",
        f"noFrame={_b(p.noFrame)}",
        f"badChecksum={_b(p.badChecksum)}",
        f"garbageHeader={_b(p.garbageHeader)}",
        f"partialFrame={_b(p.partialFrame)}",
        f"uartFailAt={p.uartFailAt}",
        f"getDist={_b(p.getDist)}",
        f"getDistNull={_b(p.getDistNull)}",
    ])


@app.post("/api/test/asw/fzc/lidar/setup")
def setup_fzc_lidar(body: FzcLidarSetupBody):
    """Store the FZC lidar phase script for subsequent run calls that omit phases."""
    global _stored_fzc_lidar_phases
    _stored_fzc_lidar_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/lidar")
def run_fzc_lidar(body: FzcLidarRunBody):
    """Execute the real FZC lidar ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy frame baseline). `body.phases` carries the stimulus
    phases — the final lidar action under test. The harness runs the
    concatenated precondition + stimulus script against the real Swc_Lidar.c
    production code (MainFunction, frame parse, zone classification, stuck /
    timeout / checksum / signal-low faults, GetDistance, DEM).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_lidar_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_lidar_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_lidar_%p.profraw")
        completed = subprocess.run(
            [_FZC_LIDAR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC lidar harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC lidar harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC lidar harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC lidar harness returned invalid JSON") from exc


def _rzc_motor_phase_to_line(p: RzcMotorPhase) -> str:
    """Serialize one RZC motor phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"vehicleState={p.vehicleState}",
        f"estop={p.estop}",
        f"torqueCmd={p.torqueCmd}",
        f"derating={p.derating}",
        f"overcurrent={p.overcurrent}",
        f"tempFault={p.tempFault}",
    ])


def _rzc_safety_phase_to_line(p: RzcSafetyPhase) -> str:
    """Serialize one RZC safety phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"reinit={_b(p.reinit)}",
        f"overcurrent={p.overcurrent}",
        f"overtemp={p.overtemp}",
        f"directionFault={p.directionFault}",
        f"stallFault={p.stallFault}",
        f"batteryFault={p.batteryFault}",
        f"selfTestResult={p.selfTestResult}",
        f"estopActive={p.estopActive}",
        f"vehicleState={p.vehicleState}",
        f"canErrorState={p.canErrorState}",
        f"notifyCanRx={_b(p.notifyCanRx)}",
    ])


def _rzc_currentmonitor_phase_to_line(p: RzcCurrentMonitorPhase) -> str:
    """Serialize one RZC current-monitor phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"currentMa={p.currentMa}",
    ])


def _rzc_encoder_phase_to_line(p: RzcEncoderPhase) -> str:
    """Serialize one RZC encoder phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    parts = [
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"deltaPerCycle={p.deltaPerCycle}",
        f"encoderDir={p.encoderDir}",
        f"commandedDir={p.commandedDir}",
        f"torqueEcho={p.torqueEcho}",
    ]
    if p.count is not None:
        parts.insert(2, f"count={p.count}")
    return " ".join(parts)


def _rzc_heartbeat_phase_to_line(p: RzcHeartbeatPhase) -> str:
    """Serialize one RZC heartbeat phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"vehicleState={p.vehicleState}",
        f"faultMask={p.faultMask}",
    ])


@app.post("/api/test/asw/rzc/encoder/setup")
def setup_rzc_encoder(body: RzcEncoderSetupBody):
    """Store the RZC encoder phase script for subsequent run calls that omit phases."""
    global _stored_rzc_encoder_phases
    _stored_rzc_encoder_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/encoder")
def run_rzc_encoder(body: RzcEncoderRunBody):
    """Execute the real RZC encoder ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a baseline count / direction history). `body.phases` carries the
    stimulus phases — the final speed, stall, wrap-around, and direction
    plausibility actions under test. The harness runs the concatenated
    precondition + stimulus script against the real Swc_Encoder.c production
    code (Init, RPM computation, reversal grace windows, stall detection,
    direction mismatch detection, Dio disable, DEM DTC, RTE outputs).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_encoder_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_encoder_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_encoder_%p.profraw")
        completed = subprocess.run(
            [_RZC_ENCODER_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC encoder harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC encoder harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC encoder harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC encoder harness returned invalid JSON") from exc


@app.post("/api/test/asw/rzc/heartbeat/setup")
def setup_rzc_heartbeat(body: RzcHeartbeatSetupBody):
    """Store the RZC heartbeat phase script for subsequent run calls that omit phases."""
    global _stored_rzc_heartbeat_phases
    _stored_rzc_heartbeat_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/heartbeat")
def run_rzc_heartbeat(body: RzcHeartbeatRunBody):
    """Execute the real RZC heartbeat ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a vehicle-state / fault-mask baseline). `body.phases` carries the
    stimulus phases. The harness runs the concatenated precondition + stimulus
    script against the real Swc_Heartbeat.c production code (Init +
    MainFunction with 50ms TX boundary schedule).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_heartbeat_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_heartbeat_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_heartbeat_%p.profraw")
        completed = subprocess.run(
            [_RZC_HEARTBEAT_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC heartbeat harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC heartbeat harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC heartbeat harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC heartbeat harness returned invalid JSON") from exc


@app.post("/api/test/asw/rzc/currentmonitor/setup")
def setup_rzc_currentmonitor(body: RzcCurrentMonitorSetupBody):
    """Store the RZC current-monitor phase script for subsequent run calls that omit phases."""
    global _stored_rzc_currentmonitor_phases
    _stored_rzc_currentmonitor_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/currentmonitor")
def run_rzc_currentmonitor(body: RzcCurrentMonitorRunBody):
    """Execute the real RZC current-monitor ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy 2048mA zero-cal baseline). `body.phases` carries the
    stimulus phases — the final raw current profile under test. The harness
    runs the concatenated precondition + stimulus script against the real
    Swc_CurrentMonitor.c production code (Init zero-cal, 4-sample moving
    average, overcurrent debounce, DIO disable, DEM DTC, recovery timing).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_currentmonitor_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_currentmonitor_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_currentmonitor_%p.profraw")
        completed = subprocess.run(
            [_RZC_CURRENTMONITOR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC current-monitor harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC current-monitor harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC current-monitor harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC current-monitor harness returned invalid JSON") from exc


@app.post("/api/test/asw/rzc/safety/setup")
def setup_rzc_safety(body: RzcSafetySetupBody):
    """Store the RZC safety phase script for subsequent run calls that omit phases."""
    global _stored_rzc_safety_phases
    _stored_rzc_safety_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/safety")
def run_rzc_safety(body: RzcSafetyRunBody):
    """Execute the real RZC safety ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy baseline). `body.phases` carries the stimulus phases — the
    final fault / CAN-state profile under test. The harness runs the
    concatenated precondition + stimulus script against the real
    Swc_RzcSafety.c production code (watchdog feed with 4-condition gate,
    fault aggregation, CAN bus-loss detection with silence / error-warning /
    bus-off / latch, motor disable on CAN loss, safety status publication,
    WATCHDOG_FAIL edge DTC report).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_safety_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_safety_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_safety_%p.profraw")
        completed = subprocess.run(
            [_RZC_SAFETY_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC safety harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC safety harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC safety harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC safety harness returned invalid JSON") from exc


def _rzc_selftest_phase_to_line(p: RzcSelfTestPhase) -> str:
    """Serialize one RZC self-test phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"skipInit={_b(p.skipInit)}",
        f"initNull={_b(p.initNull)}",
        f"bts7960={p.bts7960}",
        f"acs723={p.acs723}",
        f"ntc={p.ntc}",
        f"encoder={p.encoder}",
        f"can={p.can}",
        f"mpu={p.mpu}",
        f"canary={p.canary}",
        f"ram={p.ram}",
    ])


@app.post("/api/test/asw/rzc/selftest/setup")
def setup_rzc_selftest(body: RzcSelfTestSetupBody):
    """Store the RZC self-test phase script for subsequent run calls that omit phases."""
    global _stored_rzc_selftest_phases
    _stored_rzc_selftest_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/selftest")
def run_rzc_selftest(body: RzcSelfTestRunBody):
    """Execute the real RZC self-test ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a failed first run). `body.phases` carries the stimulus phases. The
    harness runs the concatenated precondition + stimulus script against the
    real Swc_RzcSelfTest.c production code (Init + Startup + GetResultMask),
    pinning the pass/fail result of each of the eight hardware diagnostic
    callbacks (value 2 pins a NULL callback pointer to exercise the guard)
    and counting the reported Dem DTC events plus motor-disable outputs.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_selftest_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_selftest_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_selftest_%p.profraw")
        completed = subprocess.run(
            [_RZC_SELFTEST_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC self-test harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC self-test harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC self-test harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC self-test harness returned invalid JSON") from exc


@app.post("/api/test/asw/rzc/motor/setup")
def setup_rzc_motor(body: RzcMotorSetupBody):
    """Store the RZC motor phase script for subsequent run calls that omit phases."""
    global _stored_rzc_motor_phases
    _stored_rzc_motor_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/motor")
def run_rzc_motor(body: RzcMotorRunBody):
    """Execute the real RZC motor ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a healthy torque baseline). `body.phases` carries the stimulus
    phases — the final motor action under test. The harness runs the
    concatenated precondition + stimulus script against the real Swc_Motor.c
    production code (MainFunction, mode torque limiting, derating, dead-time
    sequencing, command timeout/recovery, shoot-through, Dio/PWM/DEM).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_motor_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_motor_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_motor_%p.profraw")
        completed = subprocess.run(
            [_RZC_MOTOR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC motor harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC motor harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC motor harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC motor harness returned invalid JSON") from exc


def _rzc_battery_phase_to_line(p: RzcBatteryPhase) -> str:
    """Serialize one RZC battery phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"voltageMv={p.voltageMv}",
    ])


@app.post("/api/test/asw/rzc/battery/setup")
def setup_rzc_battery(body: RzcBatterySetupBody):
    """Store the RZC battery phase script for subsequent run calls that omit phases."""
    global _stored_rzc_battery_phases
    _stored_rzc_battery_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/battery")
def run_rzc_battery(body: RzcBatteryRunBody):
    """Execute the real RZC battery ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a low-battery baseline for hysteresis). `body.phases` carries the
    stimulus phases — the final battery voltage under test. The harness runs
    the concatenated precondition + stimulus script against the real
    Swc_Battery.c production code (MainFunction, 4-sample moving average,
    5-state thresholds, hysteresis recovery, DEM DTC, RTE signals).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_battery_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_battery_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_battery_%p.profraw")
        completed = subprocess.run(
            [_RZC_BATTERY_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC battery harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC battery harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC battery harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC battery harness returned invalid JSON") from exc


def _rzc_temponitor_phase_to_line(p: RzcTempMonitorPhase) -> str:
    """Serialize one RZC temp-monitor phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    parts = [
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"tempDc={p.tempDc}",
        f"ioFault={_b(p.ioFault)}",
        f"temp2Fail={_b(p.temp2Fail)}",
    ]
    if p.temp2Dc is not None:
        parts.insert(3, f"temp2Dc={p.temp2Dc}")
    return " ".join(parts)


@app.post("/api/test/asw/rzc/temponitor/setup")
def setup_rzc_temponitor(body: RzcTempMonitorSetupBody):
    """Store the RZC temp-monitor phase script for subsequent run calls that omit phases."""
    global _stored_rzc_temponitor_phases
    _stored_rzc_temponitor_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/temponitor")
def run_rzc_temponitor(body: RzcTempMonitorRunBody):
    """Execute the real RZC temp-monitor ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a hot-motor baseline for hysteresis). `body.phases` carries the
    stimulus phases — the final NTC temperatures under test. The harness runs
    the concatenated precondition + stimulus script against the real
    Swc_TempMonitor.c production code (MainFunction, NTC readout, plausible
    range gating, dual-sensor cross-check, stepped derating curve, hysteresis
    recovery, DEM DTC, RTE signals).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_temponitor_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_temponitor_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_temponitor_%p.profraw")
        completed = subprocess.run(
            [_RZC_TEMPMONITOR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC temp-monitor harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC temp-monitor harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC temp-monitor harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC temp-monitor harness returned invalid JSON") from exc


def _rzc_rzccom_phase_to_line(p: RzcComPhase) -> str:
    """Serialize one RZC COM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    parts = [
        f"op={p.op}",
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"pduId={p.pduId}",
        f"data={p.data}",
        f"len={p.length}",
        f"repeats={p.repeats}",
        f"estop={p.estop}",
        f"vehicleState={p.vehicleState}",
        f"torqueCmd={p.torqueCmd}",
        f"faultMask={p.faultMask}",
        f"torqueEcho={p.torqueEcho}",
        f"speedRpm={p.speedRpm}",
        f"motorDir={p.motorDir}",
        f"motorEnable={p.motorEnable}",
        f"motorFault={p.motorFault}",
        f"currentMa={p.currentMa}",
        f"overcurrent={p.overcurrent}",
        f"temp1Dc={p.temp1Dc}",
        f"temp2Dc={p.temp2Dc}",
        f"deratingPct={p.deratingPct}",
        f"batteryMv={p.batteryMv}",
        f"batteryStatus={p.batteryStatus}",
    ]
    return " ".join(parts)


@app.post("/api/test/asw/rzc/rzccom/setup")
def setup_rzc_rzccom(body: RzcComSetupBody):
    """Store the RZC COM phase script for subsequent run calls that omit phases."""
    global _stored_rzc_rzccom_phases
    _stored_rzc_rzccom_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/rzccom")
def run_rzc_rzccom(body: RzcComRunBody):
    """Execute the real RZC COM ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. E2E alive-counter setup or an E-stop baseline). `body.phases` carries
    the stimulus phases — the E2E protect/check / receive / tx actions under
    test. The harness runs the concatenated precondition + stimulus script
    against the real Swc_RzcCom.c production code (E2E CRC-8 + alive counter
    protect/check, RX receive with E-stop / E2E-fail / command-timeout
    handling, TX scheduling for heartbeat / motor status / motor current /
    motor temp / battery, DEM DTC, RTE signals).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_rzccom_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_rzccom_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_rzccom_%p.profraw")
        completed = subprocess.run(
            [_RZC_RZCCOM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC COM harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC COM harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC COM harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC COM harness returned invalid JSON") from exc


def _fzc_fzccom_phase_to_line(p: FzcFzcComPhase) -> str:
    """Serialize one FZC COM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    parts = [
        f"op={p.op}",
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"dataId={p.dataId}",
        f"data={p.data}",
        f"len={p.length}",
        f"repeats={p.repeats}",
        f"vehicleState={p.vehicleState}",
        f"faultMask={p.faultMask}",
        f"steerAngle={p.steerAngle}",
        f"steerFault={p.steerFault}",
        f"brakePos={p.brakePos}",
        f"brakeFault={p.brakeFault}",
        f"motorCutoff={p.motorCutoff}",
        f"lidarZone={p.lidarZone}",
        f"lidarDist={p.lidarDist}",
        f"lidarSignal={p.lidarSignal}",
    ]
    return " ".join(parts)


@app.post("/api/test/asw/fzc/fzccom/setup")
def setup_fzc_fzccom(body: FzcFzcComSetupBody):
    """Store the FZC COM phase script for subsequent run calls that omit phases."""
    global _stored_fzc_fzccom_phases
    _stored_fzc_fzccom_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/fzccom")
def run_fzc_fzccom(body: FzcFzcComRunBody):
    """Execute the real FZC COM ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. E2E alive-counter baseline). `body.phases` carries the stimulus
    phases — the E2E protect/check / receive / tx actions under test. The
    harness runs the concatenated precondition + stimulus script against the
    real Swc_FzcCom.c production code (E2E CRC-8 0x1D + 4-bit alive counter
    protect/check, RX receive with CAN-monitor notification, TX scheduling
    for heartbeat / steering status / brake status / brake fault / motor
    cutoff / lidar distance, Com shadow signals, RTE signals).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_fzccom_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_fzccom_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_fzccom_%p.profraw")
        completed = subprocess.run(
            [_FZC_FZCCOM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC COM harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC COM harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC COM harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC COM harness returned invalid JSON") from exc


def _fzc_heartbeat_phase_to_line(p: FzcHeartbeatPhase) -> str:
    """Serialize one FZC heartbeat phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"vehicleState={p.vehicleState}",
        f"faultMask={p.faultMask}",
    ])


@app.post("/api/test/asw/fzc/heartbeat/setup")
def setup_fzc_heartbeat(body: FzcHeartbeatSetupBody):
    """Store the FZC heartbeat phase script for subsequent run calls that omit phases."""
    global _stored_fzc_heartbeat_phases
    _stored_fzc_heartbeat_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/heartbeat")
def run_fzc_heartbeat(body: FzcHeartbeatRunBody):
    """Execute the real FZC heartbeat ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a vehicle-state / fault-mask baseline). `body.phases` carries the
    stimulus phases. The harness runs the concatenated precondition + stimulus
    script against the real Swc_Heartbeat.c production code (Init +
    MainFunction with 50ms TX boundary schedule).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_heartbeat_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_heartbeat_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_heartbeat_%p.profraw")
        completed = subprocess.run(
            [_FZC_HEARTBEAT_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC heartbeat harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC heartbeat harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC heartbeat harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC heartbeat harness returned invalid JSON") from exc


def _fzc_canmonitor_phase_to_line(p: FzcCanMonitorPhase) -> str:
    """Serialize one FZC CAN monitor phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"canMode={p.canMode}",
        f"tec={p.tec}",
        f"rec={p.rec}",
        f"notifyRx={_b(p.notifyRx)}",
    ])


@app.post("/api/test/asw/fzc/canmonitor/setup")
def setup_fzc_canmonitor(body: FzcCanMonitorSetupBody):
    """Store the FZC CAN monitor phase script for subsequent run calls that omit phases."""
    global _stored_fzc_canmonitor_phases
    _stored_fzc_canmonitor_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/canmonitor")
def run_fzc_canmonitor(body: FzcCanMonitorRunBody):
    """Execute the real FZC CAN monitor ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a grace-period or fault baseline). `body.phases` carries the stimulus
    phases. The harness runs the concatenated precondition + stimulus script
    against the real Swc_FzcCanMonitor.c production code (Init + Check +
    GetStatus + NotifyRx).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_canmonitor_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_canmonitor_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_canmonitor_%p.profraw")
        completed = subprocess.run(
            [_FZC_CANMONITOR_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC CAN monitor harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC CAN monitor harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC CAN monitor harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC CAN monitor harness returned invalid JSON") from exc


def _fzc_safety_phase_to_line(p: FzcSafetyPhase) -> str:
    """Serialize one FZC safety phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"cycles={p.cycles}",
        f"skipInit={_b(p.skipInit)}",
        f"reinit={_b(p.reinit)}",
        f"steerFault={p.steerFault}",
        f"brakeFault={p.brakeFault}",
        f"lidarFault={p.lidarFault}",
        f"vehicleState={p.vehicleState}",
        f"selfTestResult={p.selfTestResult}",
        f"selfTestDone={_b(p.selfTestDone)}",
        f"steerCmdQuality={p.steerCmdQuality}",
        f"brakeCmdQuality={p.brakeCmdQuality}",
    ])


@app.post("/api/test/asw/fzc/safety/setup")
def setup_fzc_safety(body: FzcSafetySetupBody):
    """Store the FZC safety phase script for subsequent run calls that omit phases."""
    global _stored_fzc_safety_phases
    _stored_fzc_safety_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/safety")
def run_fzc_safety(body: FzcSafetyRunBody):
    """Execute the real FZC safety ASW chain in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context,
    e.g. a post-boot-grace baseline). `body.phases` carries the stimulus phases.
    The harness runs the concatenated precondition + stimulus script against
    the real Swc_FzcSafety.c production code (Init + MainFunction + GetStatus).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_safety_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_safety_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_safety_%p.profraw")
        completed = subprocess.run(
            [_FZC_SAFETY_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC safety harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC safety harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC safety harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC safety harness returned invalid JSON") from exc


def _fzc_scheduler_phase_to_line(p: FzcSchedulerPhase) -> str:
    """Serialize one FZC scheduler phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"skipInit={_b(p.skipInit)}",
        f"reinit={_b(p.reinit)}",
    ])


@app.post("/api/test/asw/fzc/scheduler/setup")
def setup_fzc_scheduler(body: FzcSchedulerSetupBody):
    """Store the FZC scheduler phase script for subsequent run calls that omit phases."""
    global _stored_fzc_scheduler_phases
    _stored_fzc_scheduler_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/fzc/scheduler")
def run_fzc_scheduler(body: FzcSchedulerRunBody):
    """Execute the real FZC scheduler ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real
    Swc_FzcScheduler.c production code (Init + GetTable + GetCount),
    exercising the uninitialized NULL guard, idempotent re-init, and
    SWR-FZC-029 table data checks.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_fzc_scheduler_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_fzc_scheduler_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "fzc_scheduler_%p.profraw")
        completed = subprocess.run(
            [_FZC_SCHEDULER_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="FZC scheduler harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="FZC scheduler harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "FZC scheduler harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="FZC scheduler harness returned invalid JSON") from exc


def _rzc_scheduler_phase_to_line(p: RzcSchedulerPhase) -> str:
    """Serialize one RZC scheduler phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"skipInit={_b(p.skipInit)}",
        f"reinit={_b(p.reinit)}",
        f"ticks={p.ticks}",
    ])


@app.post("/api/test/asw/rzc/scheduler/setup")
def setup_rzc_scheduler(body: RzcSchedulerSetupBody):
    """Store the RZC scheduler phase script for subsequent run calls that omit phases."""
    global _stored_rzc_scheduler_phases
    _stored_rzc_scheduler_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/scheduler")
def run_rzc_scheduler(body: RzcSchedulerRunBody):
    """Execute the real RZC scheduler ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real
    Swc_RzcScheduler.c production code (Init + Tick + GetTable + GetUtilPct),
    exercising the uninitialized guard, idempotent re-init, per-runnable Tick
    dispatch counters, and SWR-RZC-028 table / util data checks.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_scheduler_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_scheduler_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_scheduler_%p.profraw")
        completed = subprocess.run(
            [_RZC_SCHEDULER_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC scheduler harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC scheduler harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC scheduler harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC scheduler harness returned invalid JSON") from exc


def _rzc_nvm_phase_to_line(p: RzcNvmPhase) -> str:
    """Serialize one RZC NVM phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"repeats={p.repeats}",
        f"dtcId={p.dtcId}",
        f"status={p.status}",
        f"timestamp={p.timestamp}",
        f"motorCurrentMa={p.motorCurrentMa}",
        f"motorTempDdc={p.motorTempDdc}",
        f"motorSpeedRpm={p.motorSpeedRpm}",
        f"batteryMv={p.batteryMv}",
        f"torqueCmdPct={p.torqueCmdPct}",
        f"vehicleState={p.vehicleState}",
        f"slot={p.slot}",
        f"nullFreeze={_b(p.nullFreeze)}",
        f"nullEntry={_b(p.nullEntry)}",
        f"dataLen={p.dataLen}",
    ])


@app.post("/api/test/asw/rzc/nvm/setup")
def setup_rzc_nvm(body: RzcNvmSetupBody):
    """Store the RZC NVM phase script for subsequent run calls that omit phases."""
    global _stored_rzc_nvm_phases
    _stored_rzc_nvm_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/rzc/nvm")
def run_rzc_nvm(body: RzcNvmRunBody):
    """Execute the real RZC NVM ASW in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real
    Swc_RzcNvm.c production code (Init / StoreDtc / LoadDtc / GetWriteIndex),
    exercising the 20-slot circular-buffer DTC persistence, per-entry CRC-16
    integrity, freeze-frame storage, write-index wrap, and fail-closed LoadDtc
    on CRC corruption, with UNIT_TEST hooks observing the initialization flag,
    corrupting stored CRCs, and verifying the static CRC-16 calculator.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_rzc_nvm_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_rzc_nvm_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "rzc_nvm_%p.profraw")
        completed = subprocess.run(
            [_RZC_NVM_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="RZC NVM harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="RZC NVM harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "RZC NVM harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="RZC NVM harness returned invalid JSON") from exc


def _sc_state_phase_to_line(p: ScStatePhase) -> str:
    """Serialize one SC state phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"newState={p.newState}",
        f"state={p.state}",
    ])


@app.post("/api/test/asw/sc/state/setup")
def setup_sc_state(body: ScStateSetupBody):
    """Store the SC state phase script for subsequent run calls that omit phases."""
    global _stored_sc_state_phases
    _stored_sc_state_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/state")
def run_sc_state(body: ScStateRunBody):
    """Execute the real SC state machine in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_state.c
    production code (SC_State_Init / SC_State_Get / SC_State_Transition),
    exercising the GAP-SC-006 authoritative runtime state machine: valid
    INIT→MONITORING→FAULT/KILL and FAULT→KILL edges, all invalid transitions
    rejected fail-closed with state unchanged, the KILL terminal state, and
    the unknown-state default branch that forces KILL (driven by the UNIT_TEST
    `setRaw` injection hook).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_state_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_state_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_state_%p.profraw")
        completed = subprocess.run(
            [_SC_STATE_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC state harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC state harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC state harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC state harness returned invalid JSON") from exc


def _sc_heartbeat_phase_to_line(p: ScHeartbeatPhase) -> str:
    """Serialize one SC heartbeat phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"ticks={p.ticks}",
        f"ecu={p.ecu}",
        f"repeats={p.repeats}",
        f"payload3={p.payload3}",
        f"notifyA={p.notifyA}",
        f"notifyB={p.notifyB}",
    ])


@app.post("/api/test/asw/sc/heartbeat/setup")
def setup_sc_heartbeat(body: ScHeartbeatSetupBody):
    """Store the SC heartbeat phase script for subsequent run calls that omit phases."""
    global _stored_sc_heartbeat_phases
    _stored_sc_heartbeat_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/heartbeat")
def run_sc_heartbeat(body: ScHeartbeatRunBody):
    """Execute the real SC heartbeat monitoring in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_heartbeat.c
    production code (SC_Heartbeat_Init / NotifyRx / Monitor / ValidateContent /
    IsTimedOut / IsAnyConfirmed / IsContentFault / IsFzcBrakeFault),
    exercising the per-ECU independent timeout counters, 150-tick timeout
    detection, 20-tick confirmation latch, 3-HB recovery debounce, startup
    grace, LED drive, and content validation thresholds (SWR-SC-027/028),
    with UNIT_TEST hooks observing every internal counter/flag.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_heartbeat_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_heartbeat_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_heartbeat_%p.profraw")
        completed = subprocess.run(
            [_SC_HEARTBEAT_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC heartbeat harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC heartbeat harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC heartbeat harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC heartbeat harness returned invalid JSON") from exc


def _sc_e2e_phase_to_line(p: ScE2ePhase) -> str:
    """Serialize one SC E2E phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"dataId={p.dataId}",
        f"msgIndex={p.msgIndex}",
        f"dlc={p.dlc}",
        f"alive={p.alive}",
        f"crcCorrupt={p.crcCorrupt}",
        f"dataIdCorrupt={p.dataIdCorrupt}",
        f"payloadCorrupt={p.payloadCorrupt}",
        f"nullData={p.nullData}",
        f"ticks={p.ticks}",
        f"len={p.len}",
    ])


@app.post("/api/test/asw/sc/e2e/setup")
def setup_sc_e2e(body: ScE2eSetupBody):
    """Store the SC E2E phase script for subsequent run calls that omit phases."""
    global _stored_sc_e2e_phases
    _stored_sc_e2e_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/e2e")
def run_sc_e2e(body: ScE2eRunBody):
    """Execute the real SC E2E CRC-8/alive validation in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_e2e.c
    production code (SC_E2E_Init / SC_E2E_Check / SC_E2E_IsMsgFailed /
    SC_E2E_IsAnyCriticalFailed / SC_E2E_ComputeCRC8), exercising the
    SWR-SC-003 CRC-8 (poly 0x1D) + DataId + alive-counter validation, the
    3-consecutive-failure persistent latch, the boot-grace window with
    failure-state reset, and the GAP-SC-002 critical-mailbox relay-kill
    gating (E-Stop + CVC/FZC/RZC heartbeats), with UNIT_TEST hooks observing
    every internal counter/flag and the internal sc_crc8(). The harness is
    compiled with the production TMS570 logic (no PLATFORM_POSIX/HIL), so
    the strict 3-failure threshold and 5-tick grace apply.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_e2e_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_e2e_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_e2e_%p.profraw")
        completed = subprocess.run(
            [_SC_E2E_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC E2E harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC E2E harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC E2E harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC E2E harness returned invalid JSON") from exc


def _sc_relay_phase_to_line(p: ScRelayPhase) -> str:
    """Serialize one SC relay phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"repeats={p.repeats}",
        f"estop={p.estop}",
        f"hb={p.hb}",
        f"plaus={p.plaus}",
        f"creep={p.creep}",
        f"e2e={p.e2e}",
        f"selftest={p.selftest}",
        f"esm={p.esm}",
        f"busoff={p.busoff}",
        f"busSilent={p.busSilent}",
        f"value={p.value}",
    ])


@app.post("/api/test/asw/sc/relay/setup")
def setup_sc_relay(body: ScRelaySetupBody):
    """Store the SC relay phase script for subsequent run calls that omit phases."""
    global _stored_sc_relay_phases
    _stored_sc_relay_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/relay")
def run_sc_relay(body: ScRelayRunBody):
    """Execute the real SC kill relay control in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_relay.c
    production code (SC_Relay_Init / Energize / DeEnergize / CheckTriggers /
    IsKilled / GetKillReason), exercising the SWR-SC-010/011/012 kill relay
    GPIO control: Init LOW safe-state without clearing the kill latch,
    Energize gated by the latch, DeEnergize latching de-energized state, and
    the 10ms CheckTriggers cascade (E-Stop, heartbeat confirmed timeout,
    plausibility fault, creep guard, E2E critical failure, self-test failure,
    ESM lockstep error, CAN bus-off, bus silence, and 2-consecutive GPIO
    readback mismatch), with injected mocks for every external module getter
    and a mocked relay GIO pin whose readback can be overridden to drive the
    readback-mismatch branches. The harness is compiled with the production
    TMS570 logic (no PLATFORM_POSIX/HIL), so the latch and all triggers apply.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_relay_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_relay_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_relay_%p.profraw")
        completed = subprocess.run(
            [_SC_RELAY_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC relay harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC relay harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC relay harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC relay harness returned invalid JSON") from exc


def _sc_plausibility_phase_to_line(p: ScPlausibilityPhase) -> str:
    """Serialize one SC plausibility phase to a single key=value script line."""
    def _b(v: bool) -> int:
        return 1 if v else 0
    return " ".join([
        f"op={p.op}",
        f"skipInit={_b(p.skipInit)}",
        f"torque={p.torque}",
        f"current={p.current}",
        f"vehValid={p.vehValid}",
        f"curValid={p.curValid}",
        f"brakeFault={p.brakeFault}",
        f"repeats={p.repeats}",
        f"ticks={p.ticks}",
        f"expected={p.expected}",
        f"actual={p.actual}",
    ])


@app.post("/api/test/asw/sc/plausibility/setup")
def setup_sc_plausibility(body: ScPlausibilitySetupBody):
    """Store the SC plausibility phase script for subsequent run calls that omit phases."""
    global _stored_sc_plausibility_phases
    _stored_sc_plausibility_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/plausibility")
def run_sc_plausibility(body: ScPlausibilityRunBody):
    """Execute the real SC torque-vs-current cross-check in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real
    sc_plausibility.c production code (SC_Plausibility_Init / Check /
    IsFaulted / CreepGuard_Check / IsCreepFaulted), exercising the SWR-SC-007
    torque-to-current LUT (16 entries, linear interpolation), the SWR-SC-008
    plausibility comparator with 20% relative / 2000mA absolute threshold and
    debounce, the SWR-SC-009 fault latch + system LED, the SWR-SC-024
    FZC-brake-fault backup cutoff, and the SSR-SC-018 standstill creep guard
    (torque==0 with current>500mA for 2 cycles → non-clearable latch), with
    UNIT_TEST hooks observing every internal counter and the lookup/
    is_implausible statics. CAN data / heartbeat brake-fault / GIO system LED
    are injected mocks. The harness is compiled with the production TMS570
    logic (no PLATFORM_POSIX/HIL), so the strict 10-tick debounce and
    1500-tick startup grace apply.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_plausibility_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_plausibility_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_plausibility_%p.profraw")
        completed = subprocess.run(
            [_SC_PLAUSIBILITY_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC plausibility harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC plausibility harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC plausibility harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC plausibility harness returned invalid JSON") from exc


def _sc_watchdog_phase_to_line(p: ScWatchdogPhase) -> str:
    """Serialize one SC watchdog phase to a single key=value script line."""
    return " ".join([
        f"op={p.op}",
        f"ok={p.ok}",
        f"repeats={p.repeats}",
    ])


@app.post("/api/test/asw/sc/watchdog/setup")
def setup_sc_watchdog(body: ScWatchdogSetupBody):
    """Store the SC watchdog phase script for subsequent run calls that omit phases."""
    global _stored_sc_watchdog_phases
    _stored_sc_watchdog_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/watchdog")
def run_sc_watchdog(body: ScWatchdogRunBody):
    """Execute the real SC external watchdog feed control in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_watchdog.c
    production code (SC_Watchdog_Init / SC_Watchdog_Feed), exercising the
    SWR-SC-022 TPS3823 feed semantics: Init drives the WDI pin LOW, Feed
    toggles the WDI pin only when allChecksOk==TRUE (alternating
    0→1→0→1...), and every FALSE feed starves the watchdog (pin unchanged,
    no WDI write). The WDI GIO pin is mocked in-harness and every WDI write
    is counted for observation. The harness is compiled with the production
    TMS570 logic (no PLATFORM_POSIX/HIL), so the feed gate semantics are the
    production ones.
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_watchdog_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_watchdog_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_watchdog_%p.profraw")
        completed = subprocess.run(
            [_SC_WATCHDOG_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC watchdog harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC watchdog harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC watchdog harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC watchdog harness returned invalid JSON") from exc


@app.post("/api/test/bsw/comcfg/cvc")
def run_bsw_comcfg_cvc(body: BswComCfgRunBody):
    """True end-to-end check of the running CVC Com layer against the DBC.

    bus-probe observes *real* CVC frames on vcan0 and verifies DLC / period /
    E2E dataId+alive+CRC / decoded signals against the DBC single source of
    truth — the running ECU's Com layer must match. Requires the SIL Docker
    stack (instrumented CVC on vcan0).
    """
    phases = list(body.phases or [])
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided")
    invalid = sorted({p.op for p in phases if p.op not in ("bus-probe",)})
    if invalid:
        raise HTTPException(status_code=400,
                            detail=f"Unsupported op(s): {invalid}")
    return _run_bsw_comcfg_bus_probe(phases)


def _run_bsw_comcfg_bus_probe(phases: list[BswComCfgPhase]):
    """Observe the real CVC TX frames on vcan0.

    Reports per-message DLC / period / E2E dataId+alive+CRC / decoded signals
    vs the DBC. Fail-closed: unknown messages or a down bus degrade to
    found=false rather than crashing.
    """
    try:
        encoder = CanEncoder()
    except Exception as exc:
        raise HTTPException(status_code=500,
                            detail=f"Cannot load DBC encoder: {exc}") from exc

    results = []
    for p in phases:
        targets = p.targets or list(DEFAULT_TX_TARGETS)
        states = probe_live_messages(
            encoder, targets, p.windowMs, p.minFrames, p.periodTolerancePct,
        )
        for state in states:
            item = dict(state)
            item["ecu"] = "cvc"
            results.append({"op": "bus-probe", "state": item})
    return {"results": results, "state": {"phaseCount": len(phases)}}


@app.post("/api/test/bsw/rtetaskbodies/cvc")
def run_bsw_rtetaskbodies_cvc(body: BswRteTaskBodiesRunBody):
    """True end-to-end verification of task-body-driven bus behavior.

    cadence measures whether the real periodic frames reach the bus at the
    DBC cadence (10ms Com TX / 50ms heartbeat). ftti injects the E-stop via
    the UDP DIO pin and measures the 0x001 broadcast latency against the FTTI
    budget, validating the observable consequences of the task-body dispatch.
    Requires the SIL Docker stack (instrumented CVC on vcan0).
    """
    phases = list(body.phases or [])
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided")
    invalid = sorted({p.op for p in phases
                      if p.op not in ("cadence", "ftti")})
    if invalid:
        raise HTTPException(status_code=400,
                            detail=f"Unsupported op(s): {invalid}")
    return _run_bsw_rtetaskbodies_bus(phases)


def _run_bsw_rtetaskbodies_bus(phases: list[BswRteTaskBodiesPhase]):
    """Observe task-body-driven bus behavior over the live CAN bus.

    cadence: periodic frame cadence vs the DBC cycle times (observable
    consequence of the 10ms/50ms task bodies driving Com TX).
    ftti: E-stop injected via UDP DIO pin, latency to 0x001(Active=1)
    on the bus measured against the FTTI budget.
    """
    try:
        encoder = CanEncoder()
    except Exception as exc:
        raise HTTPException(status_code=500,
                            detail=f"Cannot load DBC encoder: {exc}") from exc

    results = []
    for p in phases:
        if p.op == "cadence":
            targets = p.targets or ["CVC_Heartbeat", "Vehicle_State",
                                    "Torque_Request"]
            states = probe_live_messages(
                encoder, targets, p.windowMs, p.minFrames,
                p.periodTolerancePct,
            )
            for state in states:
                item = dict(state)
                item["ecu"] = "cvc"
                results.append({"op": "cadence", "state": item})
        elif p.op == "ftti":
            state = ftti_estop(encoder, budget_ms=p.budgetMs,
                               restart_cvc=p.restartCvc,
                               min_frames=p.minFrames)
            item = dict(state)
            item["ecu"] = "cvc"
            results.append({"op": "ftti", "state": item})
    return {"results": results, "state": {"phaseCount": len(phases)}}


def _sc_selftest_phase_to_line(p: ScSelfTestPhase) -> str:
    """Serialize one SC self-test phase to a single key=value script line."""
    return " ".join([
        f"op={p.op}",
        f"b1={p.b1}",
        f"b2={p.b2}",
        f"b3={p.b3}",
        f"b4={p.b4}",
        f"b5={p.b5}",
        f"b6={p.b6}",
        f"b7={p.b7}",
        f"flashIncr={p.flashIncr}",
        f"dcanErr={p.dcanErr}",
        f"readback={p.readback}",
        f"corruptCanary={p.corruptCanary}",
        f"corruptRam={p.corruptRam}",
        f"repeats={p.repeats}",
    ])


@app.post("/api/test/asw/sc/selftest/setup")
def setup_sc_selftest(body: ScSelfTestSetupBody):
    """Store the SC self-test phase script for subsequent run calls that omit phases."""
    global _stored_sc_selftest_phases
    _stored_sc_selftest_phases = body.phases
    return {"phaseCount": len(body.phases)}


@app.post("/api/test/asw/sc/selftest")
def run_sc_selftest(body: ScSelfTestRunBody):
    """Execute the real SC startup/runtime self-test in a native test harness.

    The `/setup` endpoint stores the precondition phase script (Given context).
    `body.phases` carries the stimulus phases. The harness runs the
    concatenated precondition + stimulus script against the real sc_selftest.c
    production code (SC_SelfTest_Init / SC_SelfTest_Startup /
    SC_SelfTest_Runtime / SC_SelfTest_StackCanaryOk / SC_SelfTest_IsHealthy),
    exercising the SWR-SC-016..021 7-step startup BIST (lockstep, RAM PBIST,
    flash CRC-32, DCAN loopback, GPIO readback, lamp test, watchdog test —
    each failure returns its step number and blocks the remaining steps) and
    the 60s-period runtime checks (flash CRC incremental at tick 1, RAM
    32-byte pattern at tick 1500, DCAN error status at tick 3000, GIO relay
    readback at tick 4500, wrap at tick 6000). The hardware checks are
    mocked in-harness with per-call counters; UNIT_TEST hooks observe the
    internal tick / health flags and inject canary / RAM corruption. The
    harness is compiled with the production TMS570 logic (no
    PLATFORM_POSIX/HIL).
    """
    stimulus = body.phases if body.phases is not None else []
    phases = list(_stored_sc_selftest_phases) + list(stimulus)
    if not phases:
        raise HTTPException(status_code=400, detail="No phases provided (run /setup first)")

    script = "\n".join(_sc_selftest_phase_to_line(p) for p in phases) + "\n"
    try:
        env = os.environ.copy()
        env["LLVM_PROFILE_FILE"] = os.path.join(_COVERAGE_DIR, "sc_selftest_%p.profraw")
        completed = subprocess.run(
            [_SC_SELFTEST_HARNESS],
            input=script,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_COVERAGE_DIR,
            env=env,
        )
    except FileNotFoundError as exc:
        raise HTTPException(status_code=500, detail="SC self-test harness not found") from exc
    except subprocess.TimeoutExpired as exc:
        raise HTTPException(status_code=504, detail="SC self-test harness timed out") from exc
    if completed.returncode != 0:
        detail = completed.stderr.strip() if completed.stderr else completed.stdout.strip()
        raise HTTPException(status_code=500, detail=detail or "SC self-test harness failed")

    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise HTTPException(status_code=500, detail="SC self-test harness returned invalid JSON") from exc


@app.get("/api/test/asw/cvc/pedal-torque/coverage")
def get_coverage_html():
    """Generate and serve the merged HTML coverage report.

    Regenerates from all accumulated .gcda data so the report reflects
    every scenario that has run since the container started.
    """
    _generate_coverage_html()
    index_path = os.path.join(_COVERAGE_HTML_DIR, "index.html")
    if not os.path.exists(index_path):
        raise HTTPException(
            status_code=503,
            detail="Coverage report generation failed. Check container logs.",
        )
    return FileResponse(index_path, media_type="text/html")


def main():
    port = int(os.environ.get("FAULT_PORT", "8091"))
    log.info("Starting fault injection API on port %d", port)
    uvicorn.run(
        "fault_inject.app:app",
        host="127.0.0.1",
        port=port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
