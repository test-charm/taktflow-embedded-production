# Plan: Open-Issues Closure & Repo Health Restoration (2026-07)

**Status: IN PROGRESS**
**Branch: `fix/open-issues-2026-07` (based on `5ed7792`, tip of `hil-tx-debug-2026-04-21`)**
**Date: 2026-07-06**

## How to read this

Audience: an AI worker (or engineer) landing cold with no prior conversation
context. Each step carries: Step ID, Goal, Inputs, Deliverables (concrete file
paths), Acceptance criteria (independently checkable), Gate/review reference,
and Definition of done. Repo rules live in `.claude/rules/` (notably
`development-discipline.md`: TDD, layer verification, no hand-edits to
generated `cfg/` files). Verification commands assume a Linux gcc container
(`docker run --rm -v <worktree>:/work -w /work gcc:13 ...`) because the POSIX
makefile targets Linux.

## Background (evidence, 2026-07-06 audit)

- Master plan Phases 0–17 DONE; Phase 18 (physical HIL) pending. VPS SIL
  suite last ran 14/16 (handoff `sil-tests/2026-05-15`): SIL-009 and SIL-017
  failing.
- **SIL-009**: scenario `test/sil/scenarios/sil_009_e2e_corruption.yaml`
  injects only 4 corrupted 0x100 frames; the active Com-layer E2E supervision
  for Vehicle_State uses `E2eSmWindowInvalid = 10` (generated from
  `max(3, ceil(100 / tx_cycle_ms))`, 10 ms cycle) and Dem needs 3 FAILED
  reports after INVALID → ≥12 frames required for DTC 0xE601. The scenario's
  `TODO:TEST` claim ("E2E not wired") is stale: E2E RX checking lives in
  `firmware/bsw/services/Com/src/Com.c` (E2E_Check + E2E_SMCheck +
  Dem_ReportErrorStatus on INVALID) since Phase 2. Fix is test-side; firmware
  behavior (100 ms detection window) is the designed, FTTI-consistent value.
- **SIL-017**: two independent faults from security-hardening commit
  `1bf6c48`: (a) `gateway/mosquitto/acl` grants user `taktflow` only
  `taktflow/#`, so the ML detector's publish to `vehicle/alerts` (and the CAN
  gateway's `vehicle/dtc/new`, `vehicle/telemetry`) is silently ACL-dropped;
  (b) `test/sil/verdict_checker.py` MQTTMonitor connects anonymously and the
  broker rejects anonymous (rc=5) → it never subscribes. Same latent auth bug
  in `test/hil/hil_runner.py`.
- **Unit-test layer broken at HEAD** (verified in gcc:13 container): 18 test
  harnesses fail to compile across rzc(3)/fzc(3)/cvc(6)/sc(3)/bcm(3); icu and
  tcu pass. Cause: include-source test harnesses block real cfg headers
  (`#define <ECU>_CFG_H`) and their local signal-ID/mocks drifted behind SWC
  source changes from the April SIL-stability series. SC additionally has
  committed `Sc_Hw_Cfg.h` macro redefinitions (`SC_BUS_SILENCE_TICKS`,
  `SC_E2E_MAX_CONSEC_FAIL`) failing `-Werror`, and TMS570-only
  `firmware/ecu/sc/test/test_eth_ping.c` is picked up by the POSIX test glob.
- CI (`ci.yml`, `test.yml`) red since 2026-04-19; MISRA red since 2026-03-17;
  nightly crons stopped 2026-03-17 (details per CI-log audit step S4).

## Steps

### S-OIC-01 — Repair unit-test harnesses (rzc, fzc, cvc, sc, bcm)

- **Goal**: every `make -f firmware/platform/posix/Makefile.posix test
  TARGET=<t>` compiles and passes for all 7 targets.
- **Inputs**: baseline failure list (this plan's Background); generated cfg
  headers `firmware/ecu/<t>/include/<T>_Cfg.h` (authoritative signal-ID
  values); SWC sources under `firmware/ecu/<t>/src/`.
- **Deliverables** (test files only; SWC/product code untouched except listed):
  - `firmware/ecu/rzc/test/test_Swc_Heartbeat_asilc.c` — add
    `RZC_SIG_RZC_HEARTBEAT_ECU_ID 133u`, `_FAULT_STATUS 134u`,
    `_OPERATING_MODE 135u`; enlarge mock signal table; add assertions that
    Init writes ECU_ID and MainFunction writes mode/fault fields.
  - `firmware/ecu/rzc/test/test_Swc_RzcCom_asild.c` — add missing
    `RZC_COM_SIG_*` defines; delete/redirect dead `Rzc_E2eRxCheck` cases.
  - `firmware/ecu/rzc/include/Swc_RzcCom.h` — remove stale `Rzc_E2eRxCheck`
    declaration (dead code; function deleted from source).
  - `firmware/ecu/rzc/test/test_Swc_TempMonitor_asila.c` — mock
    `IoHwAb_ReadMotorTemp2`, define `RZC_TEMP_PLAUS_DELTA_DDC`.
  - `firmware/ecu/fzc/test/test_Swc_FzcCom_asild.c`,
    `test_Swc_FzcSafety_asild.c` (mock `Com_GetRxPduQuality` + quality/ID
    defines), `test_Swc_Heartbeat_asilc.c`.
  - `firmware/ecu/cvc/test/test_Swc_CvcCom_asild.c`, `test_Swc_CvcDcm_qm.c`
    (`CVCDCM_RESET_DELAY_CYCLES`), `test_Swc_Heartbeat_asilc.c`,
    `test_Swc_Pedal_asild.c` (`CVC_SIG_TORQUE_REQUEST_DIRECTION`),
    `test_Swc_VehicleState_asild.c` (mock `Com_GetRxPduQuality` etc.).
  - `firmware/ecu/bcm/test/test_Swc_DoorLock_qm.c`, `test_Swc_Indicators_qm.c`,
    `test_Swc_Lights_qm.c`.
  - `firmware/ecu/sc/include/Sc_Hw_Cfg.h` — resolve duplicate macro
    definitions (hand-written header; guard/unify, preserve SIL/HIL intent).
  - POSIX test-glob exclusion for `firmware/ecu/sc/test/test_eth_ping.c`
    (TMS570-only; exclude in `firmware/platform/posix/Makefile.posix` or
    guard the file).
- **Acceptance criteria**:
  - All 7 targets build + all unit tests pass in gcc:13 container.
  - New behavior gained real assertions (not compile-only stubs) for:
    heartbeat ECU_ID/mode/fault writes; pedal Direction write.
  - Signal-ID values in tests match generated `<T>_Cfg.h` values exactly.
  - No changes under `firmware/ecu/*/cfg/` (generated).
- **Gate / review**: repo rule `development-discipline.md` §2 (TDD exception:
  test-harness bug, not behavior change) and §1 (no generated-file edits).
- **Definition of done**: container run shows 0 failing test binaries for all
  7 TARGETs.

### S-OIC-02 — SIL-009 scenario correction

- **Goal**: SIL-009 verifies the real E2E-CRC→DTC 0xE601 chain against the
  current Com-layer design.
- **Inputs**: root-cause analysis (Background); `firmware/bsw/services/Com/src/Com.c`
  RX E2E path; `firmware/ecu/rzc/cfg/Com_Cfg_Rzc.c:646` (window=10, read-only).
- **Deliverables**:
  - `test/sil/scenarios/sil_009_e2e_corruption.yaml` — 14 corrupted 0x100
    injections (≥12 required: 10 to latch E2E_SM INVALID + 3 Dem FAILED
    debounce), verdict restored to `dtc_code: 0x00E601`, `ecu_source: 3`;
    header/TODO text rewritten to describe the Com-layer mechanism.
  - `firmware/bsw/test/test_E2E_asild.c` — new case: `E2E_SMCheck`
    VALID→INVALID at exactly `WindowSizeInvalid` consecutive errors (N−1
    stays VALID).
- **Acceptance criteria**:
  - New E2E unit test passes; written to fail if window semantics change.
  - Scenario YAML parses (runner dry-run or yaml lint) and its comments no
    longer claim E2E is unwired.
- **Gate / review**: SWE.5 scenario traceability (`@verifies SWR-BSW-010/011,
  SWR-RZC-020` retained).
- **Definition of done**: unit test green; scenario updated; full-suite SIL
  verification recorded as follow-up if no local vcan (see S-OIC-06).

### S-OIC-03 — SIL-017 MQTT alert path

- **Goal**: ML anomaly alerts reach `vehicle/alerts` and the SIL runner can
  observe them.
- **Inputs**: `gateway/mosquitto/acl`; `test/sil/verdict_checker.py`;
  `test/sil/sil_test_lib.py` (env-credential pattern); `test/hil/hil_runner.py`.
- **Deliverables**:
  - `gateway/mosquitto/acl` — add `topic readwrite vehicle/#` for user
    `taktflow`.
  - `test/sil/verdict_checker.py` — MQTTMonitor authenticates via
    `MQTT_USER`/`MQTT_PASSWORD` env (default `taktflow`/`taktflow-dev`).
  - `test/hil/hil_runner.py` — same credential fix.
- **Acceptance criteria**:
  - ACL grants publish/subscribe on `vehicle/#` to the shared service user.
  - Verdict checker subscribes successfully against hardened broker (rc=0) —
    verifiable with a local mosquitto container pub/sub round-trip.
- **Gate / review**: restores architecturally sanctioned topics
  (`docs/aspice/system/system-architecture.md` §MQTT topics); unblocks cloud
  connector forwarding (`vehicle/dtc/new`, `vehicle/telemetry`).
- **Definition of done**: local broker round-trip on `vehicle/alerts` with
  project credentials succeeds pre-fix-fail/post-fix-pass.

### S-OIC-04 — CI restoration

- **Goal**: `ci.yml` and `test.yml` pass at branch tip; red causes beyond
  S-OIC-01 enumerated and fixed or explicitly deferred with basis.
- **Inputs**: GitHub Actions failure logs (gh CLI audit), `.github/workflows/*`.
- **Deliverables**: fixes as identified (workflow files and/or source);
  enumerated in the audit section appended to this plan when the log audit
  lands.
- **Acceptance criteria**: a push of this branch produces green CI + Tests &
  Coverage runs (Traceability stays green).
- **Gate / review**: CI is the repo's blocking gate; README claims depend on
  it (S-OIC-05).
- **Definition of done**: green run URLs recorded in the handoff.

### S-OIC-05 — Truthful portfolio surface & hygiene

- **Goal**: README and plan headers state only verifiable facts; shell
  scripts survive Windows checkouts.
- **Inputs**: repo-health audit findings (README command drift, stale claims,
  missing LF normalization).
- **Deliverables**:
  - `.gitattributes` — `*.sh text eol=lf` (fixes remote `run_sil.sh`
    line-ending fragility from the 2026-05-15 handoff).
  - `README.md` — Quick Start commands corrected (`TARGET=` required; real
    traceability-matrix path); stale "CI green / MISRA 0 violations" claims
    replaced with current, dated statements + CI badges for `ci.yml`,
    `test.yml`, `misra.yml`, `traceability.yml`.
  - `docs/plans/master-plan.md` — status header updated (dated 2026-07-06):
    SIL suite state, Phase 18 pending, pointer to this plan.
  - `scripts/ws_smoke.py` — WebSocket telemetry smoke check (connect to WS
    endpoint, assert telemetry snapshot within 10 s; exit non-zero otherwise)
    per 2026-05-14 handoff next-step.
- **Acceptance criteria**:
  - Every README command copy-pastes successfully on a fresh clone (container
    for make targets, Windows for pipeline).
  - No README claim contradicts recorded CI state.
  - LICENSE decision explicitly deferred to owner (documented, not chosen).
- **Gate / review**: `never_commit_private_data` rule sweep before commit
  (no IPs, serials, personal paths).
- **Definition of done**: README renders with badges; smoke script exits 0
  against the live VPS WS endpoint (or failure documented).

### S-OIC-06 — Verification & evidence

- **Goal**: all fixes verified at the highest locally reachable layer.
- **Inputs**: S-OIC-01..05 outputs; Docker Desktop (Linux engine) on the dev
  machine; WSL2 kernel vcan availability probe.
- **Deliverables**:
  - Container unit-test run log, all 7 targets green (committed to handoff,
    not repo).
  - Local mosquitto round-trip evidence for S-OIC-03.
  - If vcan available: `./test/sil/run_sil.sh --scenario=sil_009_e2e_corruption`
    and `--scenario=sil_017_gateway_ml_anomaly` results; else: documented
    exact VPS commands for the owner to run.
- **Acceptance criteria**: each fix has at least Layer-1 (unit) evidence;
  SIL-layer evidence or a concrete, copy-pasteable escalation path.
- **Gate / review**: `development-discipline.md` §3 (incremental layer
  verification).
- **Definition of done**: evidence linked in handoff.

### S-OIC-07 — Commit series & handoff

- **Goal**: reviewable history; durable record.
- **Inputs**: all prior steps.
- **Deliverables**: conventional commits grouped: `fix(test): ...` per ECU
  cluster, `fix(sil): sil-009 scenario ...`, `fix(gateway,test): mqtt acl +
  runner auth ...`, `docs: ...`, `chore: gitattributes ...`; handoff YAML
  under the handoff root (project `taktflow-embedded-production`, part
  `repo-health`).
- **Acceptance criteria**: no commit touches `firmware/ecu/*/cfg/`; no
  AI-co-author trailers; private-data grep clean; branch NOT merged/pushed
  without owner decision.
- **Gate / review**: owner review of the branch; push/PR is an owner action.
- **Definition of done**: handoff written; final summary lists commits and
  exact next actions (push, VPS suite run, physical-bench items).

## Explicitly out of scope (owner decisions / hardware-gated)

- The dirty working tree on `hil-tx-debug-2026-04-21` in the primary checkout
  (mid-debug TMS570 UDS/DCAN work): analyzed, untouched. Disposition
  (quarantine commit vs. continue vs. discard) is an owner decision.
- Physical bench faults (FZC BUS_OFF, swapped CVC/RZC board images), SC
  lockstep re-enable (`sc_main.c` WAIVER HIL-PF-008): require hardware.
- openbsw #370 experiment push/PR: external repo, owner decision.
- LICENSE selection: legal/ownership decision.
- Merging this fix branch (and the 33 SIL-stability commits it contains) to
  `main`: owner decision; note `main` diverges by one trivial commit
  (`b6598af`).
