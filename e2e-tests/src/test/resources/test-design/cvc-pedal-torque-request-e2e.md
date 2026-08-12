# CVC Pedal -> Torque_Request E2E Test Design

## Tested function

**CVC ASW pedal processing to Torque_Request output**

Covered chain:

```text
pedal sensor inputs
  → Swc_Pedal_MainFunction
  → RTE torque/fault outputs
  → Swc_CvcCom_TransmitSchedule
  → Torque_Request command signal
```

This is the first ASW-facing E2E case because it is:

1. simpler than E-stop / multi-ECU safe-state chains,
2. still representative of core vehicle control logic,
3. directly tied to the CVC ASW layer,
4. close to the style of `panda` native-firmware feature tests.

The test does **not** go through the dashboard/system E2E runner.  
Instead, it uses a **test-only API** that executes the real C production
code for `Swc_Pedal.c` and `Swc_CvcCom.c` inside a native harness.

---

## Inputs and outputs

### Input factors

| Factor | Meaning | Equivalence classes / range | Selected values |
|---|---|---|---|
| `sensor1Pct` | pedal sensor 1 percent | 0..100, nominal equal, large mismatch | `40`, `20`, `100` |
| `sensor2Pct` | pedal sensor 2 percent | 0..100, nominal equal, large mismatch | `40`, `80`, `100` |
| `vehicleState` | CVC mode limit source | `RUN`, `DEGRADED`, `LIMP`, `SAFE_STOP`, `SHUTDOWN`, `INIT` | `RUN`, `DEGRADED` |
| `cycles` | number of 10ms pedal cycles to execute | too few for debounce/ramp, enough for debounce, enough for ramp saturation | `2`, `100`, `200` |

### Output factors

| Factor | Meaning | Expected values of interest |
|---|---|---|
| `outputs.torqueRequestPct` | final torque request percentage written by ASW | `40`, `0`, `75` |
| `outputs.pedalFaultName` | ASW pedal fault classification | `NONE`, `PLAUSIBILITY` |
| `outputs.torqueDirection` | torque direction signal | `1` when torque > 0, `0` when torque = 0 |
| `outputs.comSignals.torqueRequestCommandPct` | value forwarded by `Swc_CvcCom` | same as `torqueRequestPct` |

---

## Input range analysis

### 1. Pedal sensor percentages

The harness accepts percentages `0..100` and converts them to the raw
14-bit values consumed by `Swc_Pedal`.

Relevant classes:

1. **matching nominal values** — no plausibility fault
2. **large mismatch** — exceeds plausibility threshold and should trip the fault
3. **high demand** — reaches mode limiting behavior after ramp settles

### 2. Vehicle state

`Swc_Pedal` applies mode limits:

- `RUN` → 100%
- `DEGRADED` → 75%
- `LIMP` → 30%
- `SAFE_STOP / SHUTDOWN / INIT` → 0%

For the first feature set we cover:

- `RUN` for the nominal path
- `DEGRADED` for representative limiting behavior

### 3. Cycle count

This factor is essential because:

- plausibility uses debounce,
- torque rises through a ramp limit,
- therefore a single cycle is not representative.

Selected classes:

1. **2 cycles** — enough to trigger plausibility debounce
2. **100 cycles** — enough for nominal 40% torque to settle
3. **200 cycles** — enough for 100% input to settle and then be capped by DEGRADED mode

---

## Flow

```text
[Receive pedal request]
  ═══→ [Convert pct -> raw sensor values]
  ═══→ [Init Swc_Pedal + Swc_CvcCom]
  ═══→ [Run N pedal cycles]
  ═══→ {sensor mismatch?}
        ├─ Y → [pedal fault latch / torque = 0]
        └─ N → [torque lookup + ramp]
                 ═══→ {vehicle state limit active?}
                       ├─ Y → [cap torque]
                       └─ N → [keep torque]
  ═══→ [Bridge torque through CvcCom]
  ═══→ [Return JSON outputs]
```

---

## Test cases

| Case name | sensor1Pct | sensor2Pct | vehicleState | cycles | Expected torqueRequestPct | Expected fault | Expected direction |
|---|---:|---:|---|---:|---:|---|---:|
| run_matching_40pct_produces_40pct_torque | 40 | 40 | RUN | 100 | 40 | NONE | 1 |
| run_mismatched_pedals_zero_torque_after_debounce | 20 | 80 | RUN | 2 | 0 | PLAUSIBILITY | 0 |
| degraded_mode_caps_full_pedal_to_75pct | 100 | 100 | DEGRADED | 200 | 75 | NONE | 1 |

---

## Coverage checklist

### Code path coverage

- normal no-fault path covered
- plausibility-fault path covered
- mode-limit path covered

### Input coverage

- matching inputs covered
- mismatched inputs covered
- nominal state covered
- degraded state covered
- debounce-sensitive cycle count covered
- ramp-saturation cycle count covered

### Branch coverage

- plausibility branch: both pass and fail covered
- mode-limit branch: uncapped and capped covered
- torque-direction branch: zero and non-zero covered

