FROM python:3.11-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*

COPY gateway/fault_inject/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY gateway/fault_inject/ /app/fault_inject/
COPY gateway/fault_inject/native/ /app/fault_inject/native/
COPY gateway/lib/ /app/lib/
COPY gateway/taktflow_vehicle.dbc /app/taktflow_vehicle.dbc
COPY firmware/ /app/firmware/
RUN touch /app/__init__.py

RUN mkdir -p /app/bin && \
    gcc -std=c99 -Wall -Wextra -Werror -O2 -DUNIT_TEST \
      -I/app/firmware/bsw/include \
      -I/app/firmware/bsw/services/Com/include \
      -I/app/firmware/bsw/rte/include \
      -I/app/firmware/bsw/ecual/IoHwAb/include \
      -I/app/firmware/bsw/services/Dem/include \
      -I/app/firmware/bsw/services/WdgM/include \
      -I/app/firmware/ecu/cvc/include \
      -I/app/firmware/ecu/cvc/cfg \
      -I/app/firmware/ecu/cvc/cfg/platform_posix \
      /app/fault_inject/native/cvc_pedal_harness.c \
      /app/firmware/ecu/cvc/src/Swc_Pedal.c \
      /app/firmware/ecu/cvc/src/Swc_CvcCom.c \
      -o /app/bin/cvc_pedal_harness

RUN useradd -r -s /usr/sbin/nologin appuser && chown -R appuser /app

ENV CAN_CHANNEL=vcan0
ENV FAULT_PORT=8091
ENV DBC_PATH=/app/taktflow_vehicle.dbc

USER appuser
CMD ["python", "-m", "fault_inject.app"]
