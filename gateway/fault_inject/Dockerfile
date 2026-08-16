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
    # RZC motor harness (links Swc_Motor.c only — BTS7960 H-bridge control chain)
    clang -std=c99 -Wall -Wextra -Werror -fprofile-instr-generate -fcoverage-mapping \
      -DUNIT_TEST -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $COMMON_INC \
      -I/app/firmware/ecu/rzc/include -I/app/firmware/ecu/rzc/cfg \
      /app/fault_inject/native/rzc_motor_harness.c \
      /app/firmware/ecu/rzc/src/Swc_Motor.c \
      -o /app/bin/rzc_motor_harness && \
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
      -o /app/bin/rzc_temponitor_harness

RUN useradd -r -s /usr/sbin/nologin appuser && chown -R appuser /app

ENV CAN_CHANNEL=vcan0
ENV FAULT_PORT=8091
ENV DBC_PATH=/app/taktflow_vehicle.dbc

USER appuser
CMD ["python", "-m", "fault_inject.app"]
