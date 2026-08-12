# ASW Existing Test Analysis

## Scope

This document summarizes the current test landscape around the AUTOSAR-style ASW layer in this repository, with emphasis on:

1. what test layers already exist,
2. how each test layer works,
3. what inputs/outputs each layer validates,
4. which ASW functions already have direct tests,
5. which ASW functions are only covered indirectly by integration/SIL/HIL/PIL tests,
6. where the main gaps are if we want to add `panda/e2e-tests`-style ASW end-to-end tests.

The analysis is based on the current repository structure under:

- `firmware/ecu/*/src`
- `firmware/ecu/*/test`
- `test/unit/bsw`
- `test/framework`
- `test/integration`
- `test/sil`
- `test/hil`
- `test/pil`
- `test/mil`

---

## Executive summary

The repository already has **broad test coverage**, but the coverage is distributed across several layers:

- **ASW/SWC unit tests**: strong coverage for most ECU application components
- **BSW unit tests**: substantial coverage for Com/E2E/Det/WdgM/IoHwAb/Rte and related services
- **BSW integration tests**: real module-chain tests through Com/PduR/CanIf/E2E
- **POSIX/vCAN integration tests**: multi-ECU process-level tests
- **SIL/HIL/PIL tests**: scenario and bench tests that validate CAN-visible behavior, timing, fault reaction, and diagnostics

What is **missing today** is not test volume, but a **`panda/e2e-tests`-style ASW-facing test adapter layer**:

- no `.feature` + step-definition style BDD layer,
- no JNA/native test shim for ASW internals,
- no ASW-oriented DSL that expresses functional behavior as `Given/When/Then`.

In other words, the repo already validates many **effects** of ASW behavior, but it does not yet offer a unified **readable ASW E2E harness** comparable to `panda/e2e-tests`.

---

## Current test inventory

| Layer | Location | Count | Primary purpose |
|---|---|---:|---|
| ECU ASW/SWC unit tests | `firmware/ecu/*/test/` | 69 files | Directly validate application components with Unity + mocks |
| BSW unit tests | `test/unit/bsw/` | 40 files | Validate individual BSW modules and generated negative/full-path cases |
| BSW integration tests | `test/framework/test_int_*.c` | 11 files | Validate real module chains such as E2E -> Com -> PduR -> CanIf |
| POSIX multi-ECU integration | `test/integration/` | 8 files | Run POSIX ECU binaries on `vcan0` and validate bus-visible integration behavior |
| SIL scenario tests | `test/sil/scenarios/` | 16 YAML scenarios | Multi-ECU Docker/software-in-the-loop system scenarios |
| SIL hop-by-hop tests | `test/sil/*.py` | 4 Python tests | Follow specific signal/fault chains step by step |
| HIL scenario/tests | `test/hil/scenarios/`, `test/hil/test_*.py` | 37 YAML + 11 Python | Validate physical ECU behavior on real CAN and mixed benches |
| PIL scenarios | `test/pil/scenarios/` | 5 YAML scenarios | Validate one real DUT with injected peer heartbeats/environment |
| MIL | `test/mil/` | skeleton only | Directory exists, but no comparable executable MIL suite was found |

---

## Comparison with `panda/e2e-tests`

The reference project `panda/e2e-tests` is structurally different:

| Aspect | `panda/e2e-tests` | Current repository |
|---|---|---|
| Test expression | Java + Cucumber `.feature` + step definitions | C/Unity, Python scripts, YAML scenarios |
| ASW/internal access | JNA/native library adapter (`BodyPandaClient`, `PandaClient`) | Mostly black-box or mock-based access; no equivalent ASW adapter layer |
| Scenario style | BDD business-readable steps | CAN/system/fault oriented test scripts and YAML |
| Internal assertions | Easy to assert internal state via native shim | Usually inferred via mocks, CAN traffic, DTCs, states, or process behavior |
| Current scale | 47 feature files / 392 scenarios | More infrastructure variety, less ASW-readable BDD presentation |

**Implication:** this repo already has strong validation infrastructure, but if the goal is “ASW end-to-end tests similar to `panda/e2e-tests`”, the missing piece is a **new harness style**, not a lack of lower-level tests.

---

## Detailed explanation of each current test layer

### 1. ECU ASW/SWC unit tests

These are the closest existing tests to the AUTOSAR ASW layer. They typically:

- compile a single SWC or application module,
- replace RTE/IoHwAb/Com/Dem/BswM/Dio/Pwm/etc. with mocks,
- feed the SWC controlled inputs,
- validate output writes, state transitions, faults, or actuator commands.

| Item | Details |
|---|---|
| **Where** | `firmware/ecu/*/test/test_*.c` |
| **Examples** | `firmware/ecu/cvc/test/test_Swc_VehicleState_asild.c`, `firmware/ecu/fzc/test/test_Swc_Steering_asild.c`, `firmware/ecu/rzc/test/test_Swc_Motor_asild.c` |
| **Typical inputs** | mocked `Rte_Read()` values, fake sensor values, fault bits, heartbeat status, command timeouts, CAN-related shadow data, simulated hardware readbacks |
| **Typical outputs** | `Rte_Write()` values, `Com_SendSignal()` payloads, `Dem_ReportErrorStatus()` calls, `BswM_RequestMode()` calls, `Pwm_SetDutyCycle()`/`Dio_WriteChannel()` actuator outputs |
| **Validation points** | state machine transitions, plausibility checks, output clamping, timeout handling, derating, watchdog gating, heartbeat handling, DTC escalation, scheduler tables |

**Concrete examples**

1. `test_Swc_VehicleState_asild.c`
   - **Input**: pedal fault, CAN timeout, E-stop, SC kill, motor/brake/steering faults, battery state
   - **Output**: vehicle state, heartbeat mode mirror, BswM mode requests, DEM reports
   - **Checks**: INIT/RUN/DEGRADED/LIMP/SAFE_STOP/SHUTDOWN transitions and latching behavior

2. `test_Swc_Steering_asild.c`
   - **Input**: steering command, measured steering angle, timeout, SPI read failure, vehicle mode
   - **Output**: PWM duty, disable pins, steering fault signal, DEM event
   - **Checks**: angle-to-PWM mapping, range checks, rate limiting, return-to-center, fault latch clearing

3. `test_Swc_Motor_asild.c`
   - **Input**: torque command, estop, vehicle state, overcurrent/temp flags, timeout
   - **Output**: H-bridge PWM duty, motor direction, enable pins, torque echo, motor fault code
   - **Checks**: torque limiting, shoot-through prevention, dead-time, command timeout recovery, safe-state behavior

### 2. BSW unit tests

These tests target standalone BSW modules and generated negative/full-path tests.

| Item | Details |
|---|---|
| **Where** | `test/unit/bsw/` |
| **Examples** | `test_E2E_asild.c`, `test_Com_asild.c`, `test_CanIf_asild.c`, `test_WdgM_asild.c`, `test_XCP_security_generated.c` |
| **Typical inputs** | API calls, PDUs, signal IDs, timers/counters, invalid parameters, generated corner-case vectors |
| **Typical outputs** | return codes, updated internal state, protected/check results, routed PDU contents, DET/DEM notifications |
| **Validation points** | AUTOSAR service behavior, error handling, negative cases, timeout handling, E2E CRC/state-machine behavior, generated boundary cases |

**Concrete examples**

1. `test_E2E_asild.c`
   - **Input**: payload bytes, DataId, alive counters, intentionally corrupted frames
   - **Output**: E2E protect/check status
   - **Checks**: CRC validity, alive counter delta rules, error detection behavior

2. `test_Com_asild.c`
   - **Input**: signal write/read requests and PDU timing behavior
   - **Output**: signal shadow values, TX/RX handling results
   - **Checks**: packing/unpacking, periodic behavior, timeout/quality handling

3. `test_WdgM_asild.c`
   - **Input**: checkpoint progress / timeout conditions
   - **Output**: supervision state and reaction
   - **Checks**: alive/deadline supervision and fault escalation

### 3. BSW integration tests

These tests link multiple real BSW modules together and only mock the minimum hardware edge.

| Item | Details |
|---|---|
| **Where** | `test/framework/test_int_*.c` |
| **Examples** | `test_int_e2e_chain_asild.c`, `test_int_dem_to_dcm_asilc.c`, `test_int_wdgm_supervision_asild.c`, `test_int_safe_state_asild.c` |
| **Typical inputs** | protected payloads, simulated CAN loopback, fault injection, mode requests, watchdog misses |
| **Typical outputs** | routed RX signals, safe-state mode changes, DCM-visible DTC behavior, bus-off handling |
| **Validation points** | module-to-module interfaces, real dataflow across BSW layers, Dem/DCM linkage, WdgM/BswM reaction chains |

**Concrete example: `test_int_e2e_chain_asild.c`**

- **Input**: payload protected by E2E, sent through Com -> CanIf, captured by mocked `Can_Write()`, then looped back via `CanIf_RxIndication()`
- **Output**: RX signal available in Com receive side
- **Checks**: full E2E -> Com -> PduR -> CanIf roundtrip and receive-side validation

### 4. POSIX/vCAN integration tests

These tests run ECU binaries as POSIX processes and observe integration behavior on `vcan0`.

| Item | Details |
|---|---|
| **Where** | `test/integration/` |
| **Examples** | `layer4/test_cvc_full.py`, `layer5/test_cvc_fzc_dual.py`, `layer5/test_cvc_fzc_full.py`, `layer6/test_sc_integration.py` |
| **Typical inputs** | ECU process start/stop, raw CAN traffic on `vcan0`, process kill/restart, timing collection |
| **Typical outputs** | CAN IDs on the bus, E2E headers, alive counters, periodic message rates, process survival after peer loss |
| **Validation points** | TX presence, DLC correctness, E2E/DataId presence, bus timing, inter-ECU behavior after peer failure |

**Concrete examples**

1. `test_cvc_full.py`
   - **Input**: run only `cvc_posix`
   - **Output**: CVC heartbeat, vehicle state, torque, steer, brake, body command, virtual sensor frames
   - **Checks**: TX presence, E2E DataId, alive increment, message rates, standalone degraded behavior

2. `test_cvc_fzc_dual.py`
   - **Input**: run `cvc_posix` + `fzc_posix`, then kill CVC
   - **Output**: shared heartbeats, steering command/status traffic, FZC continued survival
   - **Checks**: bidirectional communication, FZC heartbeat persistence after CVC death

3. `test_sc_integration.py`
   - **Input**: run SC with other ECU processes, then kill one peer
   - **Output**: `SC_Status` 0x013, heartbeat monitoring reaction
   - **Checks**: SC E2E, peer heartbeat visibility, SC fault observation

### 5. SIL scenario tests

These are the current highest-value fully software-based system tests.

| Item | Details |
|---|---|
| **Where** | `test/sil/scenarios/*.yaml`, `test/sil/run_sil.sh`, `test/sil/verdict_checker.py` |
| **Examples** | `sil_003_emergency_stop.yaml`, `sil_009_e2e_corruption.yaml`, `sil_006_battery_undervoltage.yaml` |
| **Typical inputs** | YAML `setup`/`steps`: state wait, scenario injection, raw CAN injection, Docker stop/start, fault API / MQTT actions |
| **Typical outputs** | CAN messages, vehicle state transitions, motor RPM, DTC broadcasts, MQTT-visible effects, result logs |
| **Validation points** | end-to-end safety chains, fault reaction latency, safe-state transitions, ECU recovery, E2E rejection, DTC confirmation |

**Concrete examples**

1. `sil_003_emergency_stop.yaml`
   - **Input**: normal drive setup + E-stop injection
   - **Output**: SAFE_STOP state, E-stop broadcast 0x001, zero torque, steering center, motor shutdown, ongoing heartbeats
   - **Checks**: full ASIL-D safety chain from CVC detection through multi-ECU reaction

2. `sil_009_e2e_corruption.yaml`
   - **Input**: stop CVC and inject corrupted 0x100 frames
   - **Output**: RZC rejects frames, motor does not move, DTC 0xE601 broadcast, vehicle returns to RUN after restart
   - **Checks**: Com-layer E2E rejection and Dem escalation

3. `sil_006_battery_undervoltage.yaml`
   - **Input**: sustained low-voltage simulation
   - **Output**: battery state change, CVC mode reaction, safe handling
   - **Checks**: end-to-end undervoltage handling

### 6. SIL hop-by-hop tests

These Python tests are narrower than the YAML scenarios and focus on one signal chain at a time.

| Item | Details |
|---|---|
| **Where** | `test/sil/test_battery_chain.py`, `test/sil/test_overtemp_hops.py`, `test/sil/test_vsm_fault_transitions.py` |
| **Examples** | battery, overtemperature, vehicle-state-machine fault transitions |
| **Typical inputs** | MQTT injections, bus polling, state reset/recovery |
| **Typical outputs** | decoded CAN signal values, DTCs, state transitions |
| **Validation points** | each hop in the signal path, intermediate observability, negative tests before fault injection |

### 7. HIL tests

These validate behavior on physical ECUs or mixed physical/vECU benches.

| Item | Details |
|---|---|
| **Where** | `test/hil/test_*.py`, `test/hil/scenarios/*.yaml`, `test/hil/hil_runner.py` |
| **Examples** | `test_hil_e2e.py`, `test_hil_uds.py`, `test_hil_scheduler.py`, `test_hil_body.py` |
| **Typical inputs** | real CAN bus traffic on `can0`, UDS requests, MQTT or test-bench injection, physical startup behavior |
| **Typical outputs** | live CAN frames, UDS responses, timing statistics, DTC broadcasts, ECU mode/state changes |
| **Validation points** | real bus timing, CRC correctness on actual frames, diagnostic stacks on hardware, mixed-bench interactions |

**Concrete examples**

1. `test_hil_e2e.py`
   - **Input**: observe physical `Vehicle_State` and heartbeat frames
   - **Output**: live frame bytes and alive counters
   - **Checks**: CRC-8 and alive counter progression on real hardware

2. `test_hil_uds.py`
   - **Input**: ISO-TP/UDS requests to physical CVC/FZC/RZC
   - **Output**: ECU responses on 0x7E8/0x7E9/0x7EA
   - **Checks**: tester present, session control, DID reads, ECU reset, diagnostic interoperability

3. `test_hil_scheduler.py`
   - **Input**: collect real frame timestamps
   - **Output**: timing statistics
   - **Checks**: mean period, jitter, missed-tick style gaps, cross-ECU phase diversity

### 8. PIL tests

PIL validates one real ECU as DUT while the harness simulates its peers.

| Item | Details |
|---|---|
| **Where** | `test/pil/scenarios/*.yaml`, `test/pil/pil_runner.py`, `test/pil/heartbeat_injector.py` |
| **Examples** | `pil_005_cvc_e2e_integrity.yaml` |
| **Typical inputs** | injected peer heartbeats, DUT selection, scenario steps, CAN observation |
| **Typical outputs** | DUT heartbeat/state/command frames, E2E correctness, state transitions |
| **Validation points** | one-ECU behavior under controlled network simulation, heartbeat timeout handling, E2E integrity |

**Concrete example: `pil_005_cvc_e2e_integrity.yaml`**

- **Input**: wait for CVC RUN state and observe 0x010/0x100/0x101/0x102/0x103
- **Output**: multiple consecutive TX frames from physical DUT
- **Checks**: E2E CRC validity and alive-counter increment across critical CVC messages

### 9. MIL

`test/mil/` currently contains a placeholder overview and folders, but the repository does not currently expose a comparable executable MIL test suite for ASW behavior.

**Practical meaning:** MIL is presently not a usable starting point for ASW E2E expansion.

---

## ASW coverage summary by ECU

| ECU | Source files in `src/` | Direct ASW test files | Coverage note |
|---|---:|---:|---|
| BCM | 6 | 5 | Strong direct SWC coverage; `bcm_main.c` is only indirect |
| CVC | 14 | 13 | Strong direct SWC coverage; `main.c` is mainly integration/SIL/HIL covered |
| FZC | 13 | 11 | Strong direct coverage; `Swc_FzcSensorFeeder.c` and `main.c` are indirect |
| ICU | 3 | 4 | Good practical coverage; some tests target CAN/main helpers not split into separate source files |
| RZC | 14 | 13 | Strong direct coverage; `Swc_RzcSensorFeeder.c` and `main.c` are indirect |
| SC | 19 | 17 | Good direct coverage but several runtime glue files are only indirectly covered |
| TCU | 5 | 6 | Good direct coverage; CAN/main helpers are tested even when not split into standalone files |

---

## ASW function-to-test mapping

The tables below focus on **application-layer functions/components**, not generated cfg files.

### BCM

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Swc_BcmCan.c` | BCM CAN init, state RX, command RX, status TX | `test_Swc_BcmCan_qm.c` | `test_hil_body.py` | vehicle/body CAN frames, init parameters | RX parsing, TX heartbeat/body status, init behavior |
| `Swc_BcmMain.c` | BCM 10ms main loop | `test_Swc_BcmMain_qm.c` | `test_hil_body.py` | periodic loop invocation, pending CAN data | loop scheduling, process/transmit order |
| `Swc_DoorLock.c` | manual/auto door locking | `test_Swc_DoorLock_qm.c` | indirect via BCM main/body tests | lock/unlock requests, vehicle state | lock state changes and auto-lock logic |
| `Swc_Indicators.c` | turn/hazard logic | `test_Swc_Indicators_qm.c` | indirect via BCM main/body tests | turn/hazard requests, timing | flash pattern and hazard precedence |
| `Swc_Lights.c` | headlamp/tail-light control | `test_Swc_Lights_qm.c` | indirect via BCM main/body tests | lighting commands and state | lamp output selection |
| `bcm_main.c` | BCM entry point | none | `test_hil_body.py`, SIL startup scenarios | process startup and main-loop lifecycle | bring-up and bench-visible interaction only |

### CVC

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Ssd1306.c` | OLED driver | `test_Ssd1306_qm.c` | indirect via `test_Swc_Dashboard_qm.c` | init/render/clear calls | display buffer/I2C-oriented behavior |
| `Swc_CanMonitor.c` | CAN loss detection and recovery | `test_Swc_CanMonitor_asilc.c` | `sil_004_can_busoff_fzc.yaml`, heartbeat/integration tests | timeout/bus-loss conditions | fault detection and recovery path |
| `Swc_CvcCom.c` | CVC RX/TX + E2E bridge | `test_Swc_CvcCom_asild.c` | `test_cvc_full.py`, `test_cvc_fzc_full.py`, SIL startup/E2E scenarios | RX frames, scheduling ticks, RTE values | signal routing, E2E-protected TX, periodic transmit |
| `Swc_CvcDcm.c` | UDS/DID/DTC routing | `test_Swc_CvcDcm_qm.c` | `test_hil_uds.py` | UDS service requests | DID responses, DTC exposure, service dispatch |
| `Swc_Dashboard.c` | OLED dashboard rendering | `test_Swc_Dashboard_qm.c` | indirect via startup/display paths | vehicle state, speed, faults | rendered state/fault presentation |
| `Swc_EStop.c` | E-stop debounce, latch, broadcast | `test_Swc_EStop_asilb.c` | `sil_003_emergency_stop.yaml` | GPIO/button-like E-stop signal | latch, CAN 0x001, safe-state trigger |
| `Swc_Heartbeat.c` | heartbeat TX/RX monitor | `test_Swc_Heartbeat_asilc.c` | `test_cvc_full.py`, `test_hil_heartbeat.py`, `pil_005_cvc_e2e_integrity.yaml` | peer heartbeat status, periodic tick | heartbeat payload, timeout detection, alive counter |
| `Swc_Nvm.c` | DTC persistence/calibration NVM | `test_Swc_Nvm_asild.c` | indirect via DCM/fault scenarios | stored fault/calibration records | persistence/restore behavior |
| `Swc_Pedal.c` | dual pedal processing and torque map | `test_Swc_Pedal_asild.c` | `sil_002_pedal_ramp.yaml` | pedal sensor values, plausibility faults, mode limits | torque request, fault detection, clamping |
| `Swc_Scheduler.c` | runnable table and timing config | `test_Swc_Scheduler_asild.c` | `test_hil_scheduler.py` | scheduler table contents / periods | runnable configuration correctness |
| `Swc_SelfTest.c` | startup self-test sequence | `test_Swc_SelfTest_asild.c` | `test_hil_selftest.py`, startup SIL/HIL flows | self-test prereqs and failures | pass/fail sequencing and gating |
| `Swc_VehicleState.c` | authoritative CVC VSM | `test_Swc_VehicleState_asild.c` | `test_vsm_fault_transitions.py`, `test_hil_vsm.py`, SIL battery/overtemp/E-stop scenarios | faults, comm loss, E-stop, battery, peer state | state transitions, latching, mode outputs |
| `Swc_Watchdog.c` | external watchdog feed gate | `test_Swc_Watchdog_asild.c` | `sil_005_watchdog_timeout_cvc.yaml`, `test_hil_wdgm.py` | alive conditions / missing conditions | WDI feed enable/disable and fault gating |
| `main.c` | CVC entry point and periodic loop | none | `test_cvc_full.py`, SIL startup/power-cycle scenarios, HIL/PIL startup paths | process/board startup | end-to-end bring-up and periodic behavior |

### FZC

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Swc_Brake.c` | brake servo control | `test_Swc_Brake_asild.c` | `sil_003_emergency_stop.yaml`, `test_cvc_fzc_full.py` | brake command, mode, motor-cutoff conditions | PWM/servo behavior, clamp/safe-state handling |
| `Swc_Buzzer.c` | warning buzzer patterns | `test_Swc_Buzzer_qm.c` | indirect via FZC main | zone/state warning conditions | tone/pattern behavior |
| `Swc_FzcCanMonitor.c` | FZC CAN loss detection | `test_Swc_FzcCanMonitor_asilc.c` | `sil_004_can_busoff_fzc.yaml`, integration heartbeat tests | bus-off/silence/error conditions | fault detection / degraded path |
| `Swc_FzcCom.c` | FZC RX/TX + E2E | `test_Swc_FzcCom_asild.c` | `test_cvc_fzc_dual.py`, `test_cvc_fzc_full.py`, HIL body/heartbeat | peer command frames, local status signals | routing, E2E DataId, TX status frames |
| `Swc_FzcDcm.c` | FZC diagnostics | `test_Swc_FzcDcm_qm.c` | `test_hil_uds.py` | UDS requests | service handling and DID behavior |
| `Swc_FzcNvm.c` | FZC DTC/calibration persistence | `test_Swc_FzcNvm_asild.c` | indirect via diag/safety flows | stored calibration and DTC records | persistence behavior |
| `Swc_FzcSafety.c` | local safety aggregation/watchdog | `test_Swc_FzcSafety_asild.c` | HIL watchdog/self-test flows | local faults, watchdog, self-test status | aggregated fault behavior and safety reaction |
| `Swc_FzcScheduler.c` | runnable timing configuration | `test_Swc_FzcScheduler_asild.c` | `test_hil_scheduler.py`, `hil_061_scheduler_cross_ecu.yaml` | schedule table definitions | period/priority/WCET correctness |
| `Swc_FzcSensorFeeder.c` | plant-sim virtual sensors -> IoHwAb | none | `sil_008_sensor_disagreement.yaml`, `sil_011_steering_sensor_failure.yaml` | injected virtual sensor data | indirect steering/lidar behavior only |
| `Swc_Heartbeat.c` | FZC heartbeat | `test_Swc_Heartbeat_asilc.c` | `test_cvc_fzc_dual.py`, `test_hil_heartbeat.py`, scheduler HIL tests | periodic tick, fault bitmask | 50ms heartbeat payload and cadence |
| `Swc_Lidar.c` | TFMini obstacle detection | `test_Swc_Lidar_asilc.c` | `test_cvc_fzc_full.py`, SIL sensor scenarios | lidar frames / obstacle distances | frame parsing, zones, fault handling, CAN status |
| `Swc_Steering.c` | steering servo control | `test_Swc_Steering_asild.c` | `sil_008_sensor_disagreement.yaml`, `sil_011_steering_sensor_failure.yaml`, `test_cvc_fzc_dual.py` | steering command, measured angle, timeout, SPI fault | PWM mapping, rate limit, RTC, fault latching |
| `main.c` | FZC entry point | none | `test_cvc_fzc_dual.py`, `test_cvc_fzc_full.py`, SIL/HIL startup flows | process startup | bring-up and integrated operation |

### ICU

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Swc_Dashboard.c` | instrument cluster display/gauges | `test_Swc_Dashboard_qm.c` | `test_hil_body.py` | vehicle state, battery/current, fault summaries | dashboard text/gauge presentation |
| `Swc_DtcDisplay.c` | DTC circular buffer display logic | `test_Swc_DtcDisplay_qm.c` | indirect via ICU main/body flows | incoming DTC data | buffering and display list behavior |
| `icu_main.c` | ICU main entry point, CAN, 50ms loop | `test_Swc_IcuMain_qm.c`, `test_Swc_IcuCan_qm.c` | `test_hil_body.py` | startup, CAN init, loop tick | bench-visible startup and gauge update loop |

### RZC

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Swc_Battery.c` | battery voltage monitoring | `test_Swc_Battery_qm.c` | `test_battery_chain.py`, `test_hil_battery.py`, `sil_006_battery_undervoltage.yaml` | battery voltage samples / injected low-voltage conditions | averaged voltage, CAN 0x303, state reaction |
| `Swc_CurrentMonitor.c` | motor current sampling/filtering | `test_Swc_CurrentMonitor_asila.c` | `sil_007_overcurrent_motor.yaml`, `test_hil_overtemp.py` | current sensor samples / fault thresholds | averaged current, overcurrent indication |
| `Swc_Encoder.c` | speed/RPM and stall logic | `test_Swc_Encoder_asilc.c` | indirect via motor chain tests | encoder pulses / direction | RPM, direction, stall detection |
| `Swc_Heartbeat.c` | RZC heartbeat | `test_Swc_Heartbeat_asilc.c` | `test_hil_heartbeat.py`, integration/scheduler tests | periodic tick and fault mask | 50ms heartbeat payload and timing |
| `Swc_Motor.c` | H-bridge motor control | `test_Swc_Motor_asild.c` | `sil_007_overcurrent_motor.yaml`, `sil_003_emergency_stop.yaml`, `test_hil_overtemp.py` | torque command, estop, overcurrent/temp, vehicle state | PWM duty, direction, enable, safe-state shutdown |
| `Swc_RzcCom.c` | RZC RX/TX + E2E | `test_Swc_RzcCom_asild.c` | `sil_009_e2e_corruption.yaml`, startup/system bus tests | peer command frames, local status values | routing, E2E check/protect, timeout handling |
| `Swc_RzcDcm.c` | RZC diagnostics | `test_Swc_RzcDcm_qm.c` | `test_hil_uds.py` | UDS requests | DID and diagnostic responses |
| `Swc_RzcNvm.c` | DTC persistence/freeze-frame | `test_Swc_RzcNvm_asild.c` | indirect via DTC flows | stored DTC and freeze-frame records | CRC/persistence behavior |
| `Swc_RzcSafety.c` | local safety/watchdog/CAN-loss monitor | `test_Swc_RzcSafety_asild.c` | `test_hil_wdgm.py`, heartbeat/loss scenarios | watchdog and local faults | safe reaction and fault aggregation |
| `Swc_RzcScheduler.c` | scheduler table | `test_Swc_RzcScheduler_asild.c` | `test_hil_scheduler.py` | runnable definitions | timing/priority correctness |
| `Swc_RzcSelfTest.c` | startup self-test | `test_Swc_RzcSelfTest_asild.c` | `test_hil_selftest.py` | startup check conditions | self-test gate and failure handling |
| `Swc_RzcSensorFeeder.c` | virtual sensor feeder for SIL | none | `test_battery_chain.py`, `test_overtemp_hops.py`, `sil_006_battery_undervoltage.yaml`, `sil_010_overtemp_motor.yaml` | injected virtual battery/temp/current values | indirect downstream SWC behavior only |
| `Swc_TempMonitor.c` | NTC temperature monitoring / derating | `test_Swc_TempMonitor_asila.c` | `test_overtemp_hops.py`, `test_hil_overtemp.py`, `sil_010_overtemp_motor.yaml` | temperature samples and thresholds | stepped derating, overtemp fault/output |
| `main.c` | RZC entry point | none | SIL/HIL startup flows and integration tests | startup/periodic loop | bring-up and integrated operation |

### SC

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `sc_can.c` | listen-only CAN driver | `test_sc_can_asild.c` | `test_sc_integration.py`, HIL heartbeat/E2E | CAN frames and driver states | RX path, listen-only behavior |
| `sc_e2e.c` | SC-side E2E CRC validation | `test_sc_e2e_asild.c` | `test_hil_e2e.py`, `sil_009_e2e_corruption.yaml` | frame bytes / corrupted E2E data | CRC check validity |
| `sc_esm.c` | ESM lockstep error handler | `test_sc_esm_asilc.c` | indirect via SC runtime | ESM fault conditions | lockstep error response |
| `sc_eth.c` | bench Ethernet driver | `test_sc_eth.c` | bench telemetry flows | descriptor/frame input | descriptor parsing and bounds |
| `sc_eth_rx_dispatch.c` | UDP RX dispatch | none | `test_sc_xcp_eth.c` | UDP packet classification | indirect dispatch/XCP path |
| `sc_eth_telemetry.c` | UDP telemetry producer | `test_sc_eth_telemetry.c` | bench telemetry flows | runtime telemetry state | telemetry frame contents |
| `sc_eth_udp.c` | IPv4/UDP encoder | `test_sc_eth_udp.c` | bench telemetry/XCP flows | payload and endpoint data | Ethernet/IPv4/UDP encoding |
| `sc_heartbeat.c` | peer heartbeat supervision | `test_sc_heartbeat_asilc.c` | `test_sc_integration.py`, `test_hil_heartbeat.py` | peer heartbeat presence/loss | timeout detection and supervision state |
| `sc_led.c` | fault LED panel | `test_sc_led_qm.c` | indirect via SC fault flows | SC fault state | LED output pattern |
| `sc_main.c` | SC cooperative main loop | `test_sc_main_asild.c` | `test_sc_integration.py`, `sil_005_watchdog_timeout_cvc.yaml` | startup sequence and main-loop hooks | initialization order and loop behavior |
| `sc_monitoring.c` | SC_Status broadcast | none | `test_sc_integration.py` | internal SC monitoring state | 0x013 payload visibility and E2E on system bus |
| `sc_os_cfg.c` | OSEK task/alarm config | none | indirect via SC startup/timing | periodic task/alarm config | only indirectly exercised |
| `sc_plausibility.c` | torque-vs-current cross-check | `test_sc_plausibility_asilc.c` | indirect via safety scenarios | torque/current combinations | plausibility failure handling |
| `sc_relay.c` | kill-relay control | `test_sc_relay_asild.c` | `sil_005_watchdog_timeout_cvc.yaml`, `test_sc_integration.py` | relay request / fault state | relay energize/de-energize logic |
| `sc_selftest.c` | startup/runtime self-test | `test_sc_selftest_asild.c` | SC startup sequences | self-test conditions | startup/runtime self-test handling |
| `sc_startup.S` | TMS570 startup assembly | none | no clear dedicated test found | boot/startup context | currently not directly unit tested |
| `sc_state.c` | SC runtime state machine | `test_sc_state_asild.c` | `test_sc_integration.py` | peer failure / fault combinations | SC mode transitions |
| `sc_uds_shim.c` | HIL-only UDS shim | none | no clear dedicated HIL test found | diagnostic alias traffic | currently not directly covered |
| `sc_watchdog.c` | external watchdog feed control | `test_sc_watchdog_asild.c` | `test_hil_wdgm.py` | watchdog conditions | feed enable/disable behavior |
| `sc_xcp_eth.c` | XCP-over-Ethernet slave | `test_sc_xcp_eth.c` | bench telemetry/XCP flows | UDP/XCP commands | minimal XCP service path |

### TCU

| Component | Function | Direct tests | Indirect/system tests | Inputs under test | Outputs / validation points |
|---|---|---|---|---|---|
| `Swc_DataAggregator.c` | cache latest CAN values with timeout | `test_Swc_DataAggregator_qm.c` | indirect via TCU main/body flows | incoming CAN samples and timeout gaps | cache freshness and timeout behavior |
| `Swc_DtcStore.c` | in-memory DTC store | `test_Swc_DtcStore_qm.c` | indirect via diagnostics flows | DTC insert/query/update operations | DTC management correctness |
| `Swc_Obd2Pids.c` | OBD-II PID handler | `test_Swc_Obd2Pids_qm.c` | indirect via TCU diagnostic flows | PID requests | correct OBD-II response building |
| `Swc_UdsServer.c` | UDS server dispatch | `test_Swc_UdsServer_qm.c` | indirect via TCU main/diagnostic paths | UDS service requests | ISO 14229 service dispatch |
| `tcu_main.c` | TCU entry point and 10ms loop | `test_Swc_TcuMain_qm.c`, `test_Swc_TcuCan_qm.c` | `test_hil_body.py` | startup, CAN init, main loop | periodic loop and CAN-facing startup behavior |

---

## Main ASW coverage gaps

### 1. No `panda`-style ASW adapter layer

Current tests are either:

- mock-heavy unit tests, or
- black-box CAN/system tests.

What is missing is a middle layer that can:

- call ASW entry points in a scenario-friendly way,
- inject internal state without re-implementing a full ECU harness,
- assert internal ASW-visible outputs with readable BDD steps.

### 2. Components covered only indirectly

The main indirect-only ASW/runtime pieces are:

- `bcm_main.c`
- `firmware/ecu/*/src/main.c` entrypoints for CVC/FZC/RZC
- `Swc_FzcSensorFeeder.c`
- `Swc_RzcSensorFeeder.c`
- `sc_eth_rx_dispatch.c`
- `sc_monitoring.c`
- `sc_os_cfg.c`
- `sc_startup.S`
- `sc_uds_shim.c`

These are exercised mainly through integration/SIL/HIL, not through a direct ASW-facing harness.

### 3. MIL is not currently usable as an ASW-E2E base

`test/mil/` exists, but it does not currently provide a runnable model-level verification layer comparable to the rest of the stack.

### 4. Current end-to-end validation is mostly CAN-visible, not ASW-readable

The existing SIL/HIL/PIL layers are strong at validating:

- CAN frames,
- timing,
- fault response,
- DTCs,
- system mode transitions.

They are weaker at expressing:

- fine-grained ASW behavior in readable business/functional steps,
- internal SWC state evolution,
- reusable per-feature scenario vocabulary.

---

## Practical conclusion for future ASW E2E work

If the next goal is to add **ASW end-to-end tests similar to `panda/e2e-tests`**, the best way to think about the current baseline is:

1. **Do not replace the current test stack.**  
   It already covers unit, integration, SIL, HIL, and PIL concerns well.

2. **Add a new ASW-oriented harness on top of it.**  
   The missing layer is a readable scenario/adapter layer, not lower-level verification.

3. **Start from the best already-covered ASW domains.**  
   Good first candidates are:
   - CVC: `Swc_Pedal`, `Swc_VehicleState`, `Swc_EStop`, `Swc_CvcCom`
   - FZC: `Swc_Steering`, `Swc_Brake`, `Swc_Lidar`
   - RZC: `Swc_Motor`, `Swc_Battery`, `Swc_TempMonitor`, `Swc_RzcCom`

4. **Use existing SIL/HIL scenarios as behavioral references.**  
   Especially:
   - `sil_003_emergency_stop.yaml`
   - `sil_009_e2e_corruption.yaml`
   - `sil_006_battery_undervoltage.yaml`
   - `sil_010_overtemp_motor.yaml`
   - `test_hil_e2e.py`
   - `test_hil_uds.py`
   - `test_hil_scheduler.py`

These already define the system-level acceptance behavior that an ASW-readable E2E layer should reuse rather than duplicate.
