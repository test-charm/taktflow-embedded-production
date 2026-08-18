# SC 切断继电器控制 (sc_relay) E2E 测试设计

## 被测功能

**SC 切断继电器 GPIO 控制（SWR-SC-010/011/012，ASIL D）**

单一 kill 继电器控制模块（GIO_A0，energize-to-run 模式），五个公开 API：

- `SC_Relay_Init()`：置 `relay_commanded=FALSE`、读回失配计数清零、
  `kill_reason=NONE`，写继电器引脚 LOW（安全态）。**不**清除 kill 锁存
  （`relay_killed`），锁存仅断电复位（SWR-SC-011）。
- `SC_Relay_Energize()`：若已锁存则直接返回；否则置 `commanded=TRUE` 并写
  引脚 HIGH。
- `SC_Relay_DeEnergize()`：置 `commanded=FALSE`、`relay_killed=TRUE` 并写
  引脚 LOW（永久锁存）。
- `SC_Relay_CheckTriggers()`：10ms 周期评估全部去能触发（按优先级）：
  (a) E-Stop 命令（最高）、(b) 心跳确认超时、(b) 合理性故障、
  (b2) 蠕动防护（SSR-SC-018）、(c) E2E 关键邮箱持久失败（GAP-SC-002）、
  (d/e) 自检失败、(e) ESM 锁步错误、(f) CAN bus-off、(g) CAN 静默
  （≥200ms）、(h) GPIO 读回失配（连续 2 次阈值）。已锁存时直接返回。
- `SC_Relay_IsKilled()`：返回锁存状态（生产 TMS570 逻辑，无
  PLATFORM_POSIX/HIL 抑制）。
- `SC_Relay_GetKillReason()`：返回最近一次 kill 原因（`SC_KILL_REASON_*`）。

覆盖链路：

```text
测试 API 注入（op / setMock 外部模块状态 / setReadback 注入 GIO 读回值）
  → SC_Relay_Init()：
       · relay_commanded = FALSE / mismatch_count = 0 / reason = NONE
       · gioSetBit(GIO_A, PIN_RELAY, 0)（安全态）
       · 不清除 relay_killed（仅断电复位）
  → SC_Relay_Energize()：
       · {relay_killed}? ─Y→ 直接返回（不吸合）
       · relay_commanded = TRUE；gioSetBit(..., 1)
  → SC_Relay_DeEnergize()：
       · relay_commanded = FALSE；relay_killed = TRUE（永久锁存）
       · gioSetBit(..., 0)
  → SC_Relay_CheckTriggers()：
       · {relay_killed}? ─Y→ 直接返回
       · 依优先级评估 EStop → HB → PLAUS → CREEP → E2E → SELFTEST
         → ESM → BUSOFF → BUS_SILENCE → READBACK；任一命中即置
         kill_reason、DeEnergize 并返回
       · 读回校验：commanded 匹配则计数清零，失配则递增；
         连续 2 次失配 → KILL_REASON_READBACK
  → 观测（harness 输出）：results[] 每操作 state 快照
       · commanded / killed / killedApi（公开 IsKilled）/ mismatchCount /
         reason / gioRelay（GIO 读回值）
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_relay.c` 生产代码。由于模块内部状态全部为 `static` 文件作用域，参照
`sc_state` / `sc_heartbeat` / `sc_e2e` 的既有做法，在 `sc_relay.c/.h`
增加 **UNIT_TEST 保护的观测 getter**（仅测试编译，不影响交付固件）：

- `SC_Relay_TestGetKilled()` — 观测 kill 锁存（公开 `IsKilled` 交叉验证）；
- `SC_Relay_TestGetCommanded()` — 观测命令状态；
- `SC_Relay_TestGetReadbackMismatchCount()` — 观测读回失配计数器。

> **被测代码观测**：生产固件（TMS570）不定义 `UNIT_TEST`，上述 getter 绝不
> 进入交付固件。外部模块 getter（`SC_CAN_IsEStopActive` / `IsBusOff` /
> `IsBusSilent`、`SC_Heartbeat_IsAnyConfirmed`、`SC_Plausibility_IsFaulted` /
> `IsCreepFaulted`、`SC_E2E_IsAnyCriticalFailed`、`SC_SelfTest_IsHealthy`、
> `SC_ESM_IsErrorActive`）为 harness 注入 mock；继电器 GIO 引脚（`SC_GIO_PORT_A`
> / `SC_PIN_RELAY`）为 harness mock，`gioSetBit` 默认令读回镜像输出，
> `setReadback` 可覆盖读回值以驱动失配分支。harness 以生产 TMS570 配置编译
> （无 PLATFORM_POSIX/HIL），锁存与全部触发语义严格生效。

## 被测代码流程图

### SC_Relay_Init（L51-L61）

```text
[Init]
  ═══→ [relay_commanded = FALSE]
  ═══→ [readback_mismatch_count = 0]
  ═══→ [kill_reason = SC_KILL_REASON_NONE]
  ═══→ [gioSetBit(GIO_A, PIN_RELAY, 0)]（安全态，不清除 kill 锁存）
```

### SC_Relay_Energize（L63-L72）

```text
[Energize]
  ═══→ {relay_killed == TRUE?}
          ├─ Y → [return]（锁存禁止再吸合）
          └─ N → [relay_commanded = TRUE]
                → [gioSetBit(..., 1)]
```

### SC_Relay_DeEnergize（L74-L86）

```text
[DeEnergize]
  ═══→ [relay_commanded = FALSE]
  ═══→ [relay_killed = TRUE]（永久锁存，仅断电复位）
  ═══→ [gioSetBit(..., 0)]
```

### SC_Relay_CheckTriggers（L88-L204）

```text
[CheckTriggers]
  ═══→ {relay_killed == TRUE?} ─Y→ [return]（已锁存不再评估）
   ↓ N
  {SC_CAN_IsEStopActive()?} ─Y→ [reason=ESTOP] → [DeEnergize] → [return]
   ↓ N
  {SC_Heartbeat_IsAnyConfirmed()?} ─Y→ [reason=HB_TIMEOUT] → [DeEnergize] → [return]
   ↓ N
  {SC_Plausibility_IsFaulted()?} ─Y→ [reason=PLAUSIBILITY] → [DeEnergize] → [return]
   ↓ N
  {SC_Plausibility_IsCreepFaulted()?} ─Y→ [reason=CREEP_GUARD] → [DeEnergize] → [return]
   ↓ N
  {SC_E2E_IsAnyCriticalFailed()?} ─Y→ [reason=E2E_FAIL] → [DeEnergize] → [return]
   ↓ N
  {SC_SelfTest_IsHealthy() == FALSE?} ─Y→ [reason=SELFTEST] → [DeEnergize] → [return]
   ↓ N
  {SC_ESM_IsErrorActive()?} ─Y→ [reason=ESM] → [DeEnergize] → [return]
   ↓ N
  {SC_CAN_IsBusOff()?} ─Y→ [reason=BUSOFF] → [DeEnergize] → [return]
   ↓ N
  {SC_CAN_IsBusSilent()?} ─Y→ [reason=BUS_SILENCE] → [DeEnergize] → [return]
   ↓ N
  [readback = gioGetBit(GIO_A, PIN_RELAY)]
  {relay_commanded == TRUE?}
     ├─ Y → {readback != 1u?}
     │        ├─ Y → [mismatch_count++]
     │        └─ N → [mismatch_count = 0]
     └─ N → {readback != 0u?}
              ├─ Y → [mismatch_count++]
              └─ N → [mismatch_count = 0]
  {mismatch_count >= 2?}
     ├─ Y → [reason=READBACK] → [DeEnergize]
     └─ N → [return]（无 kill）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `energize` / `deEnergize` / `checkTriggers` / `setMock` / `setReadback` | When — 执行控制 |
| `repeats` | `checkTriggers` 调用次数 | `1`（单次）、`2`（读回阈值边界）、`3`（无触发重复） | When — 执行控制 |
| `estop` | `SC_CAN_IsEStopActive` mock | `0` / `1` | When — 触发条件 |
| `hb` | `SC_Heartbeat_IsAnyConfirmed` mock | `0` / `1` | When — 触发条件 |
| `plaus` | `SC_Plausibility_IsFaulted` mock | `0` / `1` | When — 触发条件 |
| `creep` | `SC_Plausibility_IsCreepFaulted` mock | `0` / `1` | When — 触发条件 |
| `e2e` | `SC_E2E_IsAnyCriticalFailed` mock | `0` / `1` | When — 触发条件 |
| `selftest` | `SC_SelfTest_IsHealthy` mock | `0`（不健康）/ `1`（健康） | When — 触发条件 |
| `esm` | `SC_ESM_IsErrorActive` mock | `0` / `1` | When — 触发条件 |
| `busoff` | `SC_CAN_IsBusOff` mock | `0` / `1` | When — 触发条件 |
| `busSilent` | `SC_CAN_IsBusSilent` mock | `0` / `1` | When — 触发条件 |
| `value` | `setReadback` 注入的 GIO 读回值 | `0` / `1` | When — 载荷 |
| 前置状态 | 进入操作前 `relay_killed`/`relay_commanded` | 未锁存 / 已吸合 / 已锁存 | When — 前置条件 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].state.killed` | `SC_Relay_TestGetKilled()`（与公开 `IsKilled` 一致） | `0`/`1` |
| `results[i].state.killedApi` | 公开 `SC_Relay_IsKilled()` 读回 | `0`/`1` |
| `results[i].state.commanded` | `SC_Relay_TestGetCommanded()` | `0`/`1` |
| `results[i].state.mismatchCount` | `SC_Relay_TestGetReadbackMismatchCount()` | `0`/`1`/`2` |
| `results[i].state.reason` | `SC_Relay_GetKillReason()` | `SC_KILL_REASON_*` |
| `results[i].state.gioRelay` | mock GIO 继电器引脚读回值 | `0`/`1` |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。
> 每个请求独立进程，harness 启动自动 `Init`（skipInit=0）。

### 规则: 初始化与安全状态 — SC_Relay_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_state_ready` | P0: init | commanded=0, killed=0, mismatchCount=0, reason=0(NONE), gioRelay=0 |
| `kill_latch_survives_reinit` | P0: energize; P1: deEnergize; P2: init | killed=1（锁存不受 Init 影响），gioRelay=0 |

### 规则: 吸合 / 断开 — SWR-SC-010

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `energize_sets_relay_high` | P0: energize | commanded=1, gioRelay=1, killed=0 |
| `deenergize_latches_kill` | P0: energize; P1: deEnergize | commanded=0, killed=1, gioRelay=0 |
| `killed_latch_blocks_reenergize` | P0: energize; P1: deEnergize; P2: energize | killed=1, commanded=0, gioRelay=0（锁存禁止再吸合） |

### 规则: 无触发条件 — 继电器保持吸合

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `no_trigger_keeps_energized` | P0: energize; P1: checkTriggers(3) | commanded=1, killed=0, gioRelay=1, mismatchCount=0 |
| `check_triggers_not_energized_no_kill` | P0: checkTriggers | killed=0, mismatchCount=0 |

### 规则: E-Stop 触发 — SWR-SC-035 / GAP-SC-001

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `estop_trigger_kills` | P0: energize; P1: setMock estop=1; P2: checkTriggers | killed=1, reason=7(ESTOP), gioRelay=0 |
| `estop_priority_over_heartbeat` | P0: energize; P1: setMock estop=1 hb=1; P2: checkTriggers | killed=1, reason=7 |

### 规则: 心跳确认超时触发 — SWR-SC-012

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `heartbeat_timeout_trigger_kills` | P0: energize; P1: setMock hb=1; P2: checkTriggers | killed=1, reason=1(HB_TIMEOUT) |

### 规则: 合理性触发 — SWR-SC-012 / SSR-SC-018

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `plausibility_trigger_kills` | P0: energize; P1: setMock plaus=1; P2: checkTriggers | killed=1, reason=2(PLAUSIBILITY) |
| `plausibility_priority_over_e2e` | P0: energize; P1: setMock plaus=1 e2e=1; P2: checkTriggers | killed=1, reason=2 |
| `creep_guard_trigger_kills` | P0: energize; P1: setMock creep=1; P2: checkTriggers | killed=1, reason=10(CREEP_GUARD) |

### 规则: E2E 关键失败触发 — GAP-SC-002

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `e2e_fail_trigger_kills` | P0: energize; P1: setMock e2e=1; P2: checkTriggers | killed=1, reason=9(E2E_FAIL) |
| `e2e_priority_over_selftest` | P0: energize; P1: setMock e2e=1 selftest=0; P2: checkTriggers | killed=1, reason=9 |

### 规则: 自检失败 / ESM 锁步错误触发 — SWR-SC-012

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `selftest_failure_trigger_kills` | P0: energize; P1: setMock selftest=0; P2: checkTriggers | killed=1, reason=3(SELFTEST) |
| `esm_error_trigger_kills` | P0: energize; P1: setMock esm=1; P2: checkTriggers | killed=1, reason=4(ESM) |

### 规则: CAN bus-off / 静默触发 — SWR-SC-036 / GAP-SC-003

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `busoff_trigger_kills` | P0: energize; P1: setMock busoff=1; P2: checkTriggers | killed=1, reason=5(BUSOFF) |
| `bus_silence_trigger_kills` | P0: energize; P1: setMock busSilent=1; P2: checkTriggers | killed=1, reason=8(BUS_SILENCE) |

### 规则: GPIO 读回校验 — SWR-SC-012

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `readback_1_mismatch_no_kill` | P0: energize; P1: setReadback 0; P2: checkTriggers | killed=0, mismatchCount=1, reason=0 |
| `readback_2_consecutive_kills` | P0: energize; P1: setReadback 0; P2: checkTriggers; P3: checkTriggers | killed=1, mismatchCount=2, reason=6(READBACK), gioRelay=0 |
| `readback_counter_resets_on_match` | P0: energize; P1: setReadback 0; P2: checkTriggers; P3: setReadback 1; P4: checkTriggers; P5: setReadback 0; P6: checkTriggers | P2 mismatchCount=1 → P4 清零 → P6 mismatchCount=1, killed=0 |
| `readback_mismatch_while_deenergized` | P0: setReadback 1; P1: checkTriggers; P2: checkTriggers | killed=1, mismatchCount=2, reason=6（未吸合时读回非 0） |

### 规则: 已锁存后的行为 — SWR-SC-011

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `already_killed_check_triggers_noop` | P0: energize; P1: setMock estop=1; P2: checkTriggers; P3: setMock(全部复位); P4: checkTriggers | P2 killed=1 reason=7；P4 仍 killed=1 reason=7（已锁存不再评估） |

## 代码路径覆盖

- `SC_Relay_Init`：全部赋值 + 安全态 GIO LOW 路径覆盖；锁存不清除语义经
  `kill_latch_survives_reinit` 验证。
- `SC_Relay_Energize`：正常吸合路径 + 锁存拒绝路径（true/false 两侧）。
- `SC_Relay_DeEnergize`：commanded=FALSE、killed=TRUE、GIO LOW 全路径。
- `SC_Relay_CheckTriggers`：
  - 已锁存提前返回（true/false 两侧）；
  - 九个触发条件各自 true/false 两侧全覆盖，优先级（EStop > HB > PLAUS >
    CREEP > E2E > SELFTEST > ESM > BUSOFF > BUS_SILENCE）经
    `estop_priority_over_heartbeat` / `plausibility_priority_over_e2e` /
    `e2e_priority_over_selftest` 交叉验证；
  - 读回分支：commanded=true 的失配（`readback != 1u` true/false）与
    commanded=false 的失配（`readback != 0u` true/false）两侧全覆盖；
  - 阈值分支：mismatch_count 1（未达）与 2（触发）两侧全覆盖。
- `SC_Relay_IsKilled`：公开 API 锁存读回路径覆盖（harness 每阶段调用）。
- `SC_Relay_GetKillReason`：全部 kill reason 读回覆盖。
- `#ifdef PLATFORM_HIL`（DeEnergize 空操作）与
  `#if defined(PLATFORM_POSIX) || defined(PLATFORM_HIL)`（IsKilled 抑制）
  由预处理器排除（HIL/SIL 运行时测试覆盖），不参与覆盖率统计。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_relay.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（122 / 122）** |
| 分支覆盖 | **100%（30 / 30）** |
| 函数覆盖 | **100%（9 / 9）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_relay.feature` | **24 scenarios / 144 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **682 scenarios / 4121 steps passed** |

函数命中次数（`sc_relay.c.func.html`，全量运行后）：

| 函数 | 命中 |
|---|---:|
| `SC_Relay_CheckTriggers` | 52 |
| `SC_Relay_Init` | 52 |
| `SC_Relay_Energize` | 44 |
| `SC_Relay_DeEnergize` | 36 |
| `SC_Relay_IsKilled` | 190 |
| `SC_Relay_GetKillReason` | 190 |
| `SC_Relay_TestGetKilled` | 190 |
| `SC_Relay_TestGetCommanded` | 190 |
| `SC_Relay_TestGetReadbackMismatchCount` | 190 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_relay.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### SC_Relay_Init（L51-L61）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L52 | `{` | 52 | 全部用例（harness 启动自动 Init 与显式 init 阶段均调用） |
| L55 | `relay_commanded = FALSE;` | 52 | 全部用例 |
| L56 | `readback_mismatch_count = 0u;` | 52 | 全部用例 |
| L57 | `kill_reason = SC_KILL_REASON_NONE;` | 52 | 全部用例 |
| L60 | `gioSetBit(SC_GIO_PORT_A, SC_PIN_RELAY, 0u);` | 52 | 全部用例（安全态断言 gioRelay=0） |
| L61 | `}` | 52 | 全部用例 |

#### SC_Relay_Energize（L63-L72）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L64 | `{` | 44 | 全部吸合用例 |
| L66 | `if (relay_killed == TRUE)` | 44 | true 侧 `killed_latch_blocks_reenergize`（P2）；false 侧全部吸合用例 |
| L67 | `return;` | 2 | `killed_latch_blocks_reenergize`（P2 锁存拒绝） |
| L68 | `}` | 2 | `killed_latch_blocks_reenergize` |
| L70 | `relay_commanded = TRUE;` | 42 | 全部吸合用例（`energize_sets_relay_high`、`deenergize_latches_kill` 等） |
| L71 | `gioSetBit(SC_GIO_PORT_A, SC_PIN_RELAY, 1u);` | 42 | 同上（断言 gioRelay=1） |
| L72 | `}` | 42 | 同上 |

#### SC_Relay_DeEnergize（L74-L86）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L75 | `{` | 36 | 全部触发 kill 用例（EStop/HB/PLAUS/CREEP/E2E/SELFTEST/ESM/BUSOFF/BUS_SILENCE/READBACK） |
| L82 | `relay_commanded = FALSE;` | 36 | 同上（断言 commanded=0） |
| L83 | `relay_killed = TRUE;` | 36 | 同上（断言 killed=1） |
| L84 | `gioSetBit(SC_GIO_PORT_A, SC_PIN_RELAY, 0u);` | 36 | 同上（断言 gioRelay=0） |
| L85 | `#endif` | 36 | 同上（`#ifdef PLATFORM_HIL` 块被预处理器排除，此行为编译期残留） |
| L86 | `}` | 36 | 同上 |

#### SC_Relay_CheckTriggers（L88-L204）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L89 | `{` | 52 | 全部 checkTriggers 调用 |
| L90 | `uint8 readback;` | 52 | 全部 checkTriggers 调用 |
| L93 | `if (relay_killed == TRUE)` | 52 | true 侧 `already_killed_check_triggers_noop`（P4）；false 侧全部其余调用 |
| L94 | `return;` | 2 | `already_killed_check_triggers_noop`（P4 已锁存提前返回） |
| L95 | `}` | 2 | `already_killed_check_triggers_noop` |
| L98 | `if (SC_CAN_IsEStopActive() == TRUE)` | 50 | true 侧 `estop_trigger_kills`、`estop_priority_over_heartbeat`、`already_killed_check_triggers_noop`（P2）；false 侧全部其余 |
| L99 | `kill_reason = SC_KILL_REASON_ESTOP;` | 6 | 三个 EStop 场景 |
| L100 | `SC_RELAY_DIAG("KILL reason=ESTOP");` | 6 | 同上 |
| L101 | `SC_Relay_DeEnergize();` | 6 | 同上（断言 gioRelay=0） |
| L102 | `return;` | 6 | 同上 |
| L103 | `}` | 6 | 同上 |
| L106 | `if (SC_Heartbeat_IsAnyConfirmed() == TRUE)` | 44 | true 侧 `heartbeat_timeout_trigger_kills`、`estop_priority_over_heartbeat`；false 侧全部其余 |
| L107 | `kill_reason = SC_KILL_REASON_HB_TIMEOUT;` | 2 | `heartbeat_timeout_trigger_kills` |
| L108-L111 | `SC_RELAY_DIAG(...)`（含 `SC_Heartbeat_IsTimedOut` 参数） | 2 | `heartbeat_timeout_trigger_kills`（无 SIL_DIAG 时展开为空操作，参数不求值） |
| L112 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L113 | `return;` | 2 | 同上 |
| L114 | `}` | 2 | 同上 |
| L117 | `if (SC_Plausibility_IsFaulted() == TRUE)` | 42 | true 侧 `plausibility_trigger_kills`、`plausibility_priority_over_e2e`；false 侧全部其余 |
| L118 | `kill_reason = SC_KILL_REASON_PLAUSIBILITY;` | 4 | 两个合理性场景 |
| L119 | `SC_RELAY_DIAG("KILL reason=PLAUSIBILITY");` | 4 | 同上 |
| L120 | `SC_Relay_DeEnergize();` | 4 | 同上 |
| L121 | `return;` | 4 | 同上 |
| L122 | `}` | 4 | 同上 |
| L125 | `if (SC_Plausibility_IsCreepFaulted() == TRUE)` | 38 | true 侧 `creep_guard_trigger_kills`；false 侧全部其余 |
| L126 | `kill_reason = SC_KILL_REASON_CREEP_GUARD;` | 2 | `creep_guard_trigger_kills` |
| L127 | `SC_RELAY_DIAG("KILL reason=CREEP_GUARD (DTC 0xE312)");` | 2 | 同上 |
| L128 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L129 | `return;` | 2 | 同上 |
| L130 | `}` | 2 | 同上 |
| L133 | `if (SC_E2E_IsAnyCriticalFailed() == TRUE)` | 36 | true 侧 `e2e_fail_trigger_kills`、`e2e_priority_over_selftest`、`plausibility_priority_over_e2e`；false 侧全部其余 |
| L134 | `kill_reason = SC_KILL_REASON_E2E_FAIL;` | 4 | 两个 E2E 场景 |
| L135 | `SC_RELAY_DIAG("KILL reason=E2E_FAIL");` | 4 | 同上 |
| L136 | `SC_Relay_DeEnergize();` | 4 | 同上 |
| L137 | `return;` | 4 | 同上 |
| L138 | `}` | 4 | 同上 |
| L141 | `if (SC_SelfTest_IsHealthy() == FALSE)` | 32 | true 侧 `selftest_failure_trigger_kills`、`e2e_priority_over_selftest`；false 侧全部其余 |
| L142 | `kill_reason = SC_KILL_REASON_SELFTEST;` | 2 | `selftest_failure_trigger_kills` |
| L143 | `SC_RELAY_DIAG("KILL reason=SELFTEST");` | 2 | 同上 |
| L144 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L145 | `return;` | 2 | 同上 |
| L146 | `}` | 2 | 同上 |
| L149 | `if (SC_ESM_IsErrorActive() == TRUE)` | 30 | true 侧 `esm_error_trigger_kills`；false 侧全部其余 |
| L150 | `kill_reason = SC_KILL_REASON_ESM;` | 2 | `esm_error_trigger_kills` |
| L151 | `SC_RELAY_DIAG("KILL reason=ESM");` | 2 | 同上 |
| L152 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L153 | `return;` | 2 | 同上 |
| L154 | `}` | 2 | 同上 |
| L159 | `if (SC_CAN_IsBusOff() == TRUE)` | 28 | true 侧 `busoff_trigger_kills`；false 侧全部其余（`#ifndef PLATFORM_HIL` 内） |
| L160 | `kill_reason = SC_KILL_REASON_BUSOFF;` | 2 | `busoff_trigger_kills` |
| L161 | `SC_RELAY_DIAG("KILL reason=BUSOFF");` | 2 | 同上 |
| L162 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L163 | `return;` | 2 | 同上 |
| L164 | `}` | 2 | 同上 |
| L167 | `if (SC_CAN_IsBusSilent() == TRUE)` | 26 | true 侧 `bus_silence_trigger_kills`；false 侧全部其余 |
| L168 | `kill_reason = SC_KILL_REASON_BUS_SILENCE;` | 2 | `bus_silence_trigger_kills` |
| L169 | `SC_RELAY_DIAG("KILL reason=BUS_SILENCE");` | 2 | 同上 |
| L170 | `SC_Relay_DeEnergize();` | 2 | 同上 |
| L171 | `return;` | 2 | 同上 |
| L172 | `}` | 2 | 同上 |
| L179 | `readback = gioGetBit(SC_GIO_PORT_A, SC_PIN_RELAY);` | 24 | 全部未锁存 checkTriggers 用例（读回分支） |
| L180 | `if (relay_commanded == TRUE)` | 24 | true 侧已吸合用例（`no_trigger_keeps_energized`、读回用例等）；false 侧 `check_triggers_not_energized_no_kill`、`readback_mismatch_while_deenergized` |
| L181 | `if (readback != 1u)` | 18 | true 侧 `readback_1_mismatch_no_kill`、`readback_2_consecutive_kills`、`readback_counter_resets_on_match`；false 侧 `no_trigger_keeps_energized`（读回匹配清零） |
| L182 | `readback_mismatch_count++;` | 10 | 三个读回失配用例（计数递增） |
| L184 | `readback_mismatch_count = 0u;` | 8 | `no_trigger_keeps_energized`、`readback_counter_resets_on_match`（P4 匹配清零） |
| L187 | `if (readback != 0u)` | 6 | true 侧 `readback_mismatch_while_deenergized`；false 侧 `check_triggers_not_energized_no_kill`（读回匹配） |
| L188 | `readback_mismatch_count++;` | 4 | `readback_mismatch_while_deenergized` |
| L190 | `readback_mismatch_count = 0u;` | 2 | `check_triggers_not_energized_no_kill` |
| L194 | `if (readback_mismatch_count >= SC_RELAY_READBACK_THRESHOLD)` | 24 | true 侧 `readback_2_consecutive_kills`、`readback_mismatch_while_deenergized`；false 侧 `readback_1_mismatch_no_kill` 等 |
| L195 | `kill_reason = SC_KILL_REASON_READBACK;` | 4 | `readback_2_consecutive_kills`、`readback_mismatch_while_deenergized` |
| L196-L198 | `SC_RELAY_DIAG("KILL reason=READBACK ...")` | 4 | 同上（无 SIL_DIAG 时展开为空操作） |
| L199 | `SC_Relay_DeEnergize();` | 4 | 同上（断言 gioRelay=0） |
| L200 | `}` | 4 | 同上 |
| L204 | `}` | 24 | 全部未锁存 checkTriggers 调用 |

#### SC_Relay_IsKilled（L206-L234）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L207 | `{` | 190 | 全部用例（harness 每阶段 `killedApi` 观测调用公开 IsKilled） |
| L233 | `return relay_killed;` | 190 | 全部用例（锁存读回，`#if PLATFORM_POSIX/HIL` 抑制块被预处理器排除） |
| L234 | `}` | 190 | 全部用例 |

#### SC_Relay_GetKillReason（L236-L239）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L237 | `{` | 190 | 全部用例（harness 每阶段 reason 观测） |
| L238 | `return kill_reason;` | 190 | 全部用例（各 kill reason 与 NONE 断言） |
| L239 | `}` | 190 | 全部用例 |

#### UNIT_TEST 观测 getter（L246-L259，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L247 | `{` | 190 | 全部用例（`SC_Relay_TestGetKilled`） |
| L248 | `return relay_killed;` | 190 | 全部用例 |
| L249 | `}` | 190 | 全部用例 |
| L252 | `{` | 190 | 全部用例（`SC_Relay_TestGetCommanded`） |
| L253 | `return relay_commanded;` | 190 | 全部用例 |
| L254 | `}` | 190 | 全部用例 |
| L257 | `{` | 190 | 全部用例（`SC_Relay_TestGetReadbackMismatchCount`） |
| L258 | `return readback_mismatch_count;` | 190 | 全部用例（读回计数断言） |
| L259 | `}` | 190 | 全部用例 |

### 分支覆盖分析

15 个判断点全部两侧命中（30/30 分支）：

| 行号 | 判断 | 分支 0（真） | 分支 1（假） |
|---|---|---|---:|
| L66 | `Energize: relay_killed == TRUE` | 2（`killed_latch_blocks_reenergize`） | 42 |
| L93 | `CheckTriggers: relay_killed == TRUE` | 2（`already_killed_check_triggers_noop`） | 50 |
| L98 | `SC_CAN_IsEStopActive()` | 6（3 个 EStop 场景） | 44 |
| L106 | `SC_Heartbeat_IsAnyConfirmed()` | 2（HB 场景） | 42 |
| L117 | `SC_Plausibility_IsFaulted()` | 4（2 个合理性场景） | 38 |
| L125 | `SC_Plausibility_IsCreepFaulted()` | 2（蠕动防护场景） | 36 |
| L133 | `SC_E2E_IsAnyCriticalFailed()` | 4（2 个 E2E 场景） | 32 |
| L141 | `SC_SelfTest_IsHealthy() == FALSE` | 2（自检失败场景） | 30 |
| L149 | `SC_ESM_IsErrorActive()` | 2（ESM 场景） | 28 |
| L159 | `SC_CAN_IsBusOff()` | 2（bus-off 场景） | 26 |
| L167 | `SC_CAN_IsBusSilent()` | 2（静默场景） | 24 |
| L180 | `relay_commanded == TRUE` | 18（已吸合读回） | 6（未吸合读回） |
| L181 | `readback != 1u` | 10（吸合失配） | 8（吸合匹配清零） |
| L187 | `readback != 0u` | 4（未吸合失配） | 2（未吸合匹配清零） |
| L194 | `mismatch_count >= THRESHOLD(2)` | 4（连续 2 次失配） | 20（未达阈值） |

## 无法覆盖的代码说明

> 本模块**无**无法覆盖的可执行代码。
>
> - `SC_Relay_IsKilled` 的 `#if defined(PLATFORM_POSIX) || defined(PLATFORM_HIL)`
>   抑制块（L208-L231）与 `SC_Relay_DeEnergize` 的 `#ifdef PLATFORM_HIL`
>   空操作块（L76-L80）为 SIL/HIL 运行时适应逻辑，harness 以生产 TMS570
>   配置编译时由预处理器排除，不参与覆盖率统计（对应行为由 SIL 场景
>   `sil_005_watchdog_timeout_cvc.yaml` 与 HIL `test_sc_integration.py` 覆盖）。
> - `SC_RELAY_DIAG` 诊断打印（无 `SIL_DIAG` 时展开为空操作）已行覆盖；其
>   参数（如 `SC_Heartbeat_IsTimedOut` 调用）不求值，属编译期行为，无需
>   独立 mock。
> - 3 个 UNIT_TEST 观测 getter 仅在测试编译中存在（`#ifdef UNIT_TEST`），
>   绝不进入交付固件（与 `sc_state` / `sc_heartbeat` / `sc_e2e` 一致）。
>
> **122/122 行、30/30 分支、9/9 函数全部被端到端测试覆盖，无无法覆盖的
> 可执行代码**。
