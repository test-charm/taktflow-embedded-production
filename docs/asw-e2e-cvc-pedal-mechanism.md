# ASW E2E Mechanism — CVC Pedal to Torque_Request

## Goal

This document explains how the current end-to-end test for the CVC ASW layer works.

The target is not the dashboard test runner or a system-level SIL scenario.  
The target is the **CVC application-layer signal path**:

```text
Pedal Sensor 1/2
  → Swc_Pedal_MainFunction
  → RTE torque / fault signals
  → Swc_CvcCom_TransmitSchedule
  → Torque_Request command signal
```

This is intentionally closer to the `panda` style:

- test from a readable feature file,
- drive a dedicated test adapter,
- execute real production C code,
- assert ASW-facing outputs directly.

---

## Files

### Feature and design

- `e2e-tests/src/test/resources/features/cvc_pedal_torque_request.feature`
- `e2e-tests/src/test/resources/test-design/cvc-pedal-torque-request-e2e.md`

### API / harness

- `gateway/fault_inject/app.py`
- `gateway/fault_inject/native/cvc_pedal_harness.c`
- `gateway/fault_inject/Dockerfile`

### Production C code executed by the harness

- `firmware/ecu/cvc/src/Swc_Pedal.c`
- `firmware/ecu/cvc/src/Swc_CvcCom.c`

---

## High-level architecture

```text
[Cucumber feature]
  → [RESTful-cucumber POST]
  → [/api/test/asw/cvc/pedal-torque]
  → [native c harness]
  → [real CVC production C files]
  → [JSON result]
  → [DAL assertion]
```

More concretely:

```text
e2e-tests
  └─ POST /api/test/asw/cvc/pedal-torque
       └─ fault_inject FastAPI endpoint
            └─ subprocess.run("/app/bin/cvc_pedal_harness ...")
                 └─ Swc_Pedal.c
                 └─ Swc_CvcCom.c
                      └─ JSON stdout
```

---

## Why this is ASW E2E instead of system E2E

The current mechanism does **not** rely on:

- dashboard `run-sync` wrappers,
- full SIL scenario orchestration,
- multi-ECU state transitions,
- MQTT verdict monitoring,
- CAN bus observation to infer internal behavior.

Instead it directly exercises:

1. **Pedal input handling**
2. **Plausibility logic**
3. **Ramp / torque mapping**
4. **Vehicle-state mode limiting**
5. **Torque_Request signal bridging into Com**

So the test subject is the **ASW behavior itself**, not the dashboard API.

---

## Request/response contract

### Test endpoint

`POST /api/test/asw/cvc/pedal-torque`

### Request body

```json
{
  "sensor1Pct": 40,
  "sensor2Pct": 40,
  "vehicleState": "RUN",
  "cycles": 100
}
```

### Response body

```json
{
  "inputs": {
    "sensor1Pct": 40,
    "sensor2Pct": 40,
    "vehicleState": 1,
    "cycles": 100
  },
  "outputs": {
    "pedalPosition": 400,
    "pedalFaultCode": 0,
    "pedalFaultName": "NONE",
    "torqueRequestPct": 40,
    "torqueDirection": 1,
    "comSignals": {
      "torqueRequestCommandPct": 40
    }
  }
}
```

---

## Native harness implementation

The native harness is `gateway/fault_inject/native/cvc_pedal_harness.c`.

It links directly against the real production sources:

- `Swc_Pedal.c`
- `Swc_CvcCom.c`

and provides minimal test doubles for:

- `IoHwAb_ReadPedalAngle`
- `Rte_Read`
- `Rte_Write`
- `Com_SendSignal`
- `Com_ReceiveSignal`
- `Dem_ReportErrorStatus`
- `Swc_VehicleState_GetState`

### Important point

This is **not** a hand-reimplemented pedal algorithm.  
The pedal logic is still executed by the real `Swc_Pedal_MainFunction`.

The harness only provides:

1. input injection,
2. required surrounding interfaces,
3. result extraction.

---

## How pedal inputs are represented

The feature sends percentages (`0..100`).  
The harness converts them to the raw 14-bit values expected by the production pedal SWC.

```text
sensor percent
  → 14-bit raw AS5048A-style value
  → IoHwAb_ReadPedalAngle
  → Swc_Pedal_MainFunction
```

This keeps the test readable while still entering the production code at the real abstraction level.

---

## Why the harness adds a tiny input dither

`Swc_Pedal.c` contains a real **stuck sensor detector**.  
If the harness feeds a perfectly constant raw value for too many cycles, the production code correctly classifies it as `STUCK`.

Real sensors usually have tiny natural jitter, so the harness adds a very small deterministic dither between cycles:

```text
base raw value
  → base + 0
  → base + 16
  → base + 0
  → base + 16
  ...
```

This is not changing the production logic.  
It only prevents the test fixture from accidentally modeling an unrealistically perfect frozen sensor.

---

## Execution model

The request executes these steps:

```text
[Validate JSON inputs]
  → [Map vehicleState string to enum]
  → [Launch native harness binary]
  → [Run N pedal cycles]
  → [Collect RTE + Com outputs]
  → [Return JSON]
```

Inside the harness:

```text
[Init config/state]
  → [Swc_Pedal_Init]
  → [Swc_CvcCom_Init]
  → repeat cycles:
       [set pedal raw values]
       [Swc_Pedal_MainFunction]
       [Swc_CvcCom_TransmitSchedule]
  → [serialize outputs]
```

---

## Assertions currently covered

The feature currently validates 3 representative behaviors:

| Scenario | What it proves |
|---|---|
| matching inputs in `RUN` | nominal pedal-to-torque generation |
| mismatched inputs in `RUN` | plausibility fault zeros torque |
| full pedal in `DEGRADED` | vehicle-state limit caps torque to 75% |

This gives first-pass coverage over:

- normal path
- safety fault path
- degraded mode limiting path

---

## Build mechanism

The harness binary is compiled into the `fault-inject` image during Docker build.

The relevant Dockerfile steps:

1. install `gcc` and `libc6-dev`
2. copy `firmware/`
3. compile `cvc_pedal_harness.c` together with `Swc_Pedal.c` and `Swc_CvcCom.c`
4. expose the binary as:

```text
/app/bin/cvc_pedal_harness
```

At runtime, the FastAPI endpoint invokes that binary with `subprocess.run(...)`.

---

## Relationship to SIL

This test does **not** require the full dashboard verdict runner, but it still reuses the `fault-inject` service as the stable HTTP host for test-only APIs.

So the layering is:

```text
SIL infrastructure
  └─ fault_inject service
       └─ ASW test-only API
            └─ native harness
                 └─ production CVC ASW code
```

That means:

- we keep the existing SIL environment,
- but the actual assertion target is much narrower and more ASW-specific.

---

## Why this approach was chosen

Compared with wrapping existing system tests:

| Option | Result |
|---|---|
| wrap dashboard runner | tests API/system orchestration |
| assert CAN/MQTT only | black-box, less ASW-specific |
| native ASW harness behind test API | direct, readable, production-code focused |

The current approach was chosen because it is the best match for the requested:

- **simple but representative**
- **ASW-focused**
- **panda-like E2E style**

---

## Current limitations

1. The harness currently covers only the **CVC pedal -> Torque_Request** chain.
2. It does not yet verify:
   - full CAN frame packing,
   - E2E CRC fields,
   - multi-ECU reactions,
   - plant response.
3. Those belong to higher layers and should stay in SIL/HIL tests.

This split is intentional:

- **ASW E2E** checks logic and signal outputs,
- **SIL/HIL** check networked/system behavior.

---

## Recommended next extensions

After this first ASW E2E, the next good candidates are:

1. `Pedal fault latch clear`
2. `VehicleState mode = SAFE_STOP -> torque = 0`
3. `CVC EStop -> VehicleState transition`
4. `Battery / overtemp -> CVC mode limiting interaction`

These can reuse the same pattern:

```text
feature
  → test-only API
  → native harness
  → production C modules
```
