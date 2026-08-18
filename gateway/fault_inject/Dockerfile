FROM python:3.11-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
      clang lld llvm lcov libclang-rt-dev \
    && rm -rf /var/lib/apt/lists/*

COPY gateway/fault_inject/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY gateway/fault_inject/ /app/fault_inject/
COPY gateway/fault_inject/native/ /app/fault_inject/native/
COPY gateway/lib/ /app/lib/
COPY gateway/taktflow_vehicle.dbc /app/taktflow_vehicle.dbc
COPY firmware/ /app/firmware/
RUN touch /app/__init__.py

# --- Compile harness with coverage instrumentation ---
# Build per-ECU subdirectories to avoid .o name conflicts (e.g., Swc_Heartbeat.c
# exists in CVC, FZC, and RZC).
# Two-step compile: -c first to force .gcno generation, then link into final binary.
RUN mkdir -p /app/bin /app/coverage && \
    for ecu in cvc fzc rzc sc bcm icu tcu; do mkdir -p /app/build/$ecu; done && \
    cd /app/build && \
    COMMON_INC="-I/app/firmware/bsw/include \
      -I/app/firmware/bsw/services/Com/include \
      -I/app/firmware/bsw/rte/include \
      -I/app/firmware/bsw/ecual/IoHwAb/include \
      -I/app/firmware/bsw/services/Dem/include \
      -I/app/firmware/bsw/services/WdgM/include \
      -I/app/firmware/bsw/services/E2E/include \
      -I/app/firmware/bsw/ecual/PduR/include \
      -I/app/firmware/bsw/services/BswM/include \
      -I/app/firmware/bsw/mcal/Dio/include \
      -I/app/firmware/bsw/services/NvM/include \
      -I/app/firmware/bsw/mcal/Spi/include \
      -I/app/firmware/bsw/mcal/Can/include \
      -I/app/firmware/bsw/mcal/Uart/include \
      -I/app/firmware/bsw/ecual/CanIf/include \
      -I/app/firmware/bsw/services/CanTp/include \
      -I/app/firmware/bsw/services/Dcm/include \
      -I/app/firmware/bsw/services/Det/include \
      -I/app/firmware/bsw/os/bootstrap/include \
      -I/app/firmware/bsw/os/bootstrap/port/include \
      -I/app/firmware/bsw/os/bootstrap/src \
      -I/app/firmware/bsw/mcal/Adc/include \
      -I/app/firmware/bsw/mcal/Pwm/include \
      -I/app/firmware/bsw/services/CanSM/include \
      -I/app/firmware/bsw/services/FiM/include \
      -I/app/firmware/bsw/services/SchM/include \
      -I/app/firmware/bsw/services/Sil/include \
      -I/app/firmware/bsw/services/Xcp/include \
      -I/app/firmware/platform/posix/include \
      -I/app/firmware/platform/stm32/include" && \
    CFLAGS="-std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE" && \
    LDFLAGS="-fprofile-instr-generate -fcoverage-mapping" && \
    # Harness + CVC
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_pedal_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Pedal.c \
      /app/firmware/ecu/cvc/src/Swc_CvcCom.c \
      /app/firmware/ecu/cvc/src/Swc_CanMonitor.c \
      /app/firmware/ecu/cvc/src/Swc_CvcDcm.c \
      /app/firmware/ecu/cvc/src/Swc_Dashboard.c \
      /app/firmware/ecu/cvc/src/Swc_EStop.c \
      /app/firmware/ecu/cvc/src/Swc_Heartbeat.c \
      /app/firmware/ecu/cvc/src/Swc_Nvm.c \
      /app/firmware/ecu/cvc/src/Swc_Scheduler.c \
      /app/firmware/ecu/cvc/src/Swc_SelfTest.c \
      /app/firmware/ecu/cvc/src/Swc_Watchdog.c \
      /app/firmware/ecu/cvc/src/Ssd1306.c && \
    # NOTE: Swc_VehicleState.c intentionally excluded from the pedal harness
    # so that Swc_VehicleState_GetState resolves to the harness stub (which
    # reads the injected vehicle_state RTE signal) rather than the real VSM
    # internal current_state (which stays INIT unless VSM MainFunction runs).
    # FZC
    cd /app/build/fzc && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/firmware/ecu/fzc/src/Swc_Brake.c \
      /app/firmware/ecu/fzc/src/Swc_Buzzer.c \
      /app/firmware/ecu/fzc/src/Swc_FzcCanMonitor.c \
      /app/firmware/ecu/fzc/src/Swc_FzcCom.c \
      /app/firmware/ecu/fzc/src/Swc_FzcDcm.c \
      /app/firmware/ecu/fzc/src/Swc_FzcNvm.c \
      /app/firmware/ecu/fzc/src/Swc_FzcSafety.c \
      /app/firmware/ecu/fzc/src/Swc_FzcScheduler.c \
      /app/firmware/ecu/fzc/src/Swc_FzcSensorFeeder.c \
      /app/firmware/ecu/fzc/src/Swc_Heartbeat.c \
      /app/firmware/ecu/fzc/src/Swc_Lidar.c \
      /app/firmware/ecu/fzc/src/Swc_Steering.c && \
    # RZC
    cd /app/build/rzc && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/firmware/ecu/rzc/src/Swc_Battery.c \
      /app/firmware/ecu/rzc/src/Swc_CurrentMonitor.c \
      /app/firmware/ecu/rzc/src/Swc_Encoder.c \
      /app/firmware/ecu/rzc/src/Swc_Heartbeat.c \
      /app/firmware/ecu/rzc/src/Swc_Motor.c \
      /app/firmware/ecu/rzc/src/Swc_RzcCom.c \
      /app/firmware/ecu/rzc/src/Swc_RzcDcm.c \
      /app/firmware/ecu/rzc/src/Swc_RzcNvm.c \
      /app/firmware/ecu/rzc/src/Swc_RzcSafety.c \
      /app/firmware/ecu/rzc/src/Swc_RzcScheduler.c \
      /app/firmware/ecu/rzc/src/Swc_RzcSelfTest.c \
      /app/firmware/ecu/rzc/src/Swc_RzcSensorFeeder.c \
      /app/firmware/ecu/rzc/src/Swc_TempMonitor.c && \
    # SC (exclude TMS570 hardware-specific files)
    cd /app/build/sc && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/sc/include -I/app/firmware/ecu/sc/cfg \
      /app/firmware/ecu/sc/src/sc_e2e.c \
      /app/firmware/ecu/sc/src/sc_eth.c \
      /app/firmware/ecu/sc/src/sc_eth_rx_dispatch.c \
      /app/firmware/ecu/sc/src/sc_eth_telemetry.c \
      /app/firmware/ecu/sc/src/sc_eth_udp.c \
      /app/firmware/ecu/sc/src/sc_monitoring.c \
      /app/firmware/ecu/sc/src/sc_state.c \
      /app/firmware/ecu/sc/src/sc_uds_shim.c \
      /app/firmware/ecu/sc/src/sc_xcp_eth.c && \
    # BCM
    cd /app/build/bcm && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/bcm/include -I/app/firmware/ecu/bcm/cfg \
      /app/firmware/ecu/bcm/src/Swc_BcmCan.c \
      /app/firmware/ecu/bcm/src/Swc_BcmMain.c \
      /app/firmware/ecu/bcm/src/Swc_DoorLock.c \
      /app/firmware/ecu/bcm/src/Swc_Indicators.c \
      /app/firmware/ecu/bcm/src/Swc_Lights.c && \
    # ICU
    cd /app/build/icu && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/icu/include -I/app/firmware/ecu/icu/cfg \
      /app/firmware/ecu/icu/src/Swc_Dashboard.c \
      /app/firmware/ecu/icu/src/Swc_DtcDisplay.c && \
    # TCU
    cd /app/build/tcu && \
    clang $CFLAGS -c $COMMON_INC \
      -I/app/firmware/ecu/tcu/include -I/app/firmware/ecu/tcu/cfg \
      /app/firmware/ecu/tcu/src/Swc_DataAggregator.c \
      /app/firmware/ecu/tcu/src/Swc_DtcStore.c \
      /app/firmware/ecu/tcu/src/Swc_Obd2Pids.c \
      /app/firmware/ecu/tcu/src/Swc_UdsServer.c && \
    clang $LDFLAGS -Wl,--allow-multiple-definition -o /app/bin/cvc_pedal_harness \
      /app/build/*.o /app/build/*/*.o && \
    # Vehicle-state machine harness (links Swc_VehicleState.c only, with coverage)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_vehiclestate_harness.c \
      /app/firmware/ecu/cvc/src/Swc_VehicleState.c \
      -o /app/bin/cvc_vehiclestate_harness && \
    # E-stop harness (links Swc_EStop.c + Swc_CvcCom.c for the broadcast chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_estop_harness.c \
      /app/firmware/ecu/cvc/src/Swc_EStop.c \
      /app/firmware/ecu/cvc/src/Swc_CvcCom.c \
      -o /app/bin/cvc_estop_harness && \
    # CVC COM harness (links Swc_CvcCom.c only — TX scheduling + RX bridge)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_cvccom_harness.c \
      /app/firmware/ecu/cvc/src/Swc_CvcCom.c \
      -o /app/bin/cvc_cvccom_harness && \
    # CVC heartbeat harness (links Swc_Heartbeat.c only — TX timing, alive
    # counter, RX indication flags, post-INIT grace comm status reset)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_heartbeat_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Heartbeat.c \
      -o /app/bin/cvc_heartbeat_harness && \
    # CVC CAN monitor harness (links Swc_CanMonitor.c only — bus-off / 200ms
    # silence / 500ms error-warning detection + recovery with 3-attempt window)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_canmonitor_harness.c \
      /app/firmware/ecu/cvc/src/Swc_CanMonitor.c \
      -o /app/bin/cvc_canmonitor_harness && \
    # CVC watchdog harness (links Swc_Watchdog.c only — TPS3823 WDI feed with
    # 4-condition gate: loop complete / canary intact / RAM ok / CAN not bus-off)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_watchdog_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Watchdog.c \
      -o /app/bin/cvc_watchdog_harness && \
    # CVC self-test harness (links Swc_SelfTest.c only — 7-step startup
    # self-test sequence with early-abort on critical failures, non-critical
    # OLED handling, and DTC reporting)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_selftest_harness.c \
      /app/firmware/ecu/cvc/src/Swc_SelfTest.c \
      -o /app/bin/cvc_selftest_harness && \
    # CVC scheduler harness (links Swc_Scheduler.c only — runnable table
    # Init/GetConfig/GetRunnableCount with NULL / null-runnables / zero-count
    # guards, config replacement, and table data checks)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_scheduler_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Scheduler.c \
      -o /app/bin/cvc_scheduler_harness && \
    # CVC NVM harness (links Swc_Nvm.c only — DTC persistence 20-slot circular
    # buffer with CRC-16 corruption detection, calibration read/write with
    # CRC-16 protection and fallback to compiled defaults on corruption)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/cvc/include -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_nvm_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Nvm.c \
      -o /app/bin/cvc_nvm_harness && \
    # FZC NVM harness (links Swc_FzcNvm.c only — DTC first-empty-slot
    # persistence, full-slot rejection, CRC-16 corruption detection, and
    # calibration read/write with NvM re-init reload + default fallback)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_nvm_harness.c \
      /app/firmware/ecu/fzc/src/Swc_FzcNvm.c \
      -o /app/bin/fzc_nvm_harness && \
    # FZC steering harness (links Swc_Steering.c only — servo control chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_steering_harness.c \
      /app/firmware/ecu/fzc/src/Swc_Steering.c \
      -o /app/bin/fzc_steering_harness && \
    # FZC brake harness (links Swc_Brake.c only — brake servo control chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_brake_harness.c \
      /app/firmware/ecu/fzc/src/Swc_Brake.c \
      -o /app/bin/fzc_brake_harness && \
    # FZC lidar harness (links Swc_Lidar.c only — TFMini-S frame parse + zone/fault chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_lidar_harness.c \
      /app/firmware/ecu/fzc/src/Swc_Lidar.c \
      -o /app/bin/fzc_lidar_harness && \
    # RZC encoder harness (links Swc_Encoder.c only — RPM calculation,
    # stall detection, direction mismatch, reversal grace, and wrap-around)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_encoder_harness.c \
      /app/firmware/ecu/rzc/src/Swc_Encoder.c \
      -o /app/bin/rzc_encoder_harness && \
    # RZC motor harness (links Swc_Motor.c only — BTS7960 H-bridge control chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_motor_harness.c \
      /app/firmware/ecu/rzc/src/Swc_Motor.c \
      -o /app/bin/rzc_motor_harness && \
    # RZC current monitor harness (links Swc_CurrentMonitor.c only — zero-cal,
    # 4-sample moving average, overcurrent debounce and recovery window)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_currentmonitor_harness.c \
      /app/firmware/ecu/rzc/src/Swc_CurrentMonitor.c \
      -o /app/bin/rzc_currentmonitor_harness && \
    # RZC safety harness (links Swc_RzcSafety.c only — watchdog feed with
    # 4-condition gate, fault aggregation, CAN bus loss detection with silence /
    # error-warning / bus-off / latch, motor disable on CAN loss, safety status
    # publication, WATCHDOG_FAIL edge DTC report)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_safety_harness.c \
      /app/firmware/ecu/rzc/src/Swc_RzcSafety.c \
      -o /app/bin/rzc_safety_harness && \
    # RZC self-test harness (links Swc_RzcSelfTest.c only — 8 injected
    # hardware diagnostic callbacks with early-abort on any failure, motor
    # disable, per-item DTC reporting, uninitialized / NULL-config guards,
    # and result-bitmask readback)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_selftest_harness.c \
      /app/firmware/ecu/rzc/src/Swc_RzcSelfTest.c \
      -o /app/bin/rzc_selftest_harness && \
    # RZC battery harness (links Swc_Battery.c only — battery voltage monitor
    # with 4-sample moving average, 5-state thresholds and hysteresis)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_battery_harness.c \
      /app/firmware/ecu/rzc/src/Swc_Battery.c \
      -o /app/bin/rzc_battery_harness && \
    # RZC temp monitor harness (links Swc_TempMonitor.c only — NTC readout,
    # dual-sensor plausibility, stepped derating curve and hysteresis recovery)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_temponitor_harness.c \
      /app/firmware/ecu/rzc/src/Swc_TempMonitor.c \
      -o /app/bin/rzc_temponitor_harness && \
    # RZC COM harness (links Swc_RzcCom.c only — E2E protect/check, RX receive
    # with E-stop/E2E-fail/command-timeout handling, and TX transmit scheduling
    # for heartbeat/motor status/motor current/motor temp/battery)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_rzccom_harness.c \
      /app/firmware/ecu/rzc/src/Swc_RzcCom.c \
      -o /app/bin/rzc_rzccom_harness && \
    # RZC heartbeat harness (links Swc_Heartbeat.c only — TX 50ms boundary
    # schedule, alive counter 15 wrap, ECU ID write, vehicle state /
    # fault-mask publication, CAN fault + SAFE_STOP TX suppression)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_heartbeat_harness.c \
      /app/firmware/ecu/rzc/src/Swc_Heartbeat.c \
      -o /app/bin/rzc_heartbeat_harness && \
    # RZC NVM harness (links Swc_RzcNvm.c only — 20-slot circular-buffer DTC
    # persistence with CRC-16/CCITT per-entry integrity, freeze-frame storage,
    # write-index wrap, fail-closed LoadDtc on CRC corruption)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_nvm_harness.c \
      /app/firmware/ecu/rzc/src/Swc_RzcNvm.c \
      -o /app/bin/rzc_nvm_harness && \
    # FZC COM harness (links Swc_FzcCom.c only — E2E protect/check, RX receive
    # with CAN-monitor notification, and TX transmit scheduling for heartbeat /
    # steering status / brake status / brake fault / motor cutoff / lidar distance)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_fzccom_harness.c \
      /app/firmware/ecu/fzc/src/Swc_FzcCom.c \
      -o /app/bin/fzc_fzccom_harness && \
    # FZC heartbeat harness (links Swc_Heartbeat.c only — TX 50ms boundary
    # schedule, alive counter 15 wrap, ECU ID write, vehicle state /
    # fault-mask publication, CAN bus-off TX suppression)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_heartbeat_harness.c \
      /app/firmware/ecu/fzc/src/Swc_Heartbeat.c \
      -o /app/bin/fzc_heartbeat_harness && \
    # FZC CAN monitor harness (links Swc_FzcCanMonitor.c only — bus-off /
    # silence / error-warning detection, boot grace period, safe-state latch)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_canmonitor_harness.c \
      /app/firmware/ecu/fzc/src/Swc_FzcCanMonitor.c \
      -o /app/bin/fzc_canmonitor_harness && \
    # FZC safety harness (links Swc_FzcSafety.c only — watchdog feed /
    # fault aggregation / self-test / RX-quality bus-off detection / motor
    # cutoff with post-INIT boot grace period)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_safety_harness.c \
      /app/firmware/ecu/fzc/src/Swc_FzcSafety.c \
      -o /app/bin/fzc_safety_harness && \
    # FZC scheduler harness (links Swc_FzcScheduler.c only — static const
    # SWR-FZC-029 runnable table, uninitialized GetTable NULL guard, idempotent
    # re-init, and table data checks)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/fzc/include -I/app/firmware/ecu/fzc/cfg \
      -I/app/firmware/ecu/fzc/cfg/platform_posix \
      /app/fault_inject/native/fzc_scheduler_harness.c \
      /app/firmware/ecu/fzc/src/Swc_FzcScheduler.c \
      -o /app/bin/fzc_scheduler_harness && \
    # RZC scheduler harness (links Swc_RzcScheduler.c only — SWR-RZC-028
    # static const runnable table, Tick dispatch with per-runnable elapsed
    # counters, uninitialized guard, idempotent re-init, util computation)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_scheduler_harness.c \
      /app/firmware/ecu/rzc/src/Swc_RzcScheduler.c \
      -o /app/bin/rzc_scheduler_harness && \
    # SC state harness (links sc_state.c only — GAP-SC-006 authoritative
    # runtime state machine: INIT/MONITORING/FAULT/KILL valid edges, invalid
    # transitions rejected fail-closed, unknown state forces KILL)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/sc/include -I/app/firmware/ecu/sc/cfg \
      -I/app/firmware/platform/posix/include \
      /app/fault_inject/native/sc_state_harness.c \
      /app/firmware/ecu/sc/src/sc_state.c \
      -o /app/bin/sc_state_harness

RUN useradd -r -s /usr/sbin/nologin appuser && chown -R appuser /app

ENV CAN_CHANNEL=vcan0
ENV FAULT_PORT=8091
ENV DBC_PATH=/app/taktflow_vehicle.dbc

USER appuser
CMD ["python", "-m", "fault_inject.app"]
