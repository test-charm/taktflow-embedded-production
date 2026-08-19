# SC 启动/运行时自检 (sc_selftest) E2E 测试设计

## 被测功能

**SC 启动/运行时自检（SWR-SC-016/017/018/019/020/021，ASIL D）**

`sc_selftest.c` 是 Safety Controller 的启动与运行时自检模块，覆盖
SWR-SC-016..021 六项安全需求。五个公开 API：

- `SC_SelfTest_Init()`：植入堆栈金丝雀 `0xDEADBEEF`、向 `ram_test_area[32]`
  写入交替模式（偶数下标 `0xAA` / 奇数下标 `0x55`）、复位状态标志
  （`startup_passed=FALSE`、`runtime_healthy=TRUE`、`runtime_tick=0`、
  `runtime_step=0`）。
- `SC_SelfTest_Startup()`：7 步启动 BIST，任一步失败立即返回该步步骤号
  （1..7）并阻断后续步骤；全部通过返回 0 并置 `startup_passed=TRUE`。
  步骤顺序：① lockstep BIST → ② RAM PBIST → ③ Flash CRC-32 → ④ DCAN1
  回环 → ⑤ GPIO 读回 → ⑥ 故障 LED 灯测试 → ⑦ TPS3823 看门狗测试。
- `SC_SelfTest_Runtime()`：10ms 周期运行。启动未通过时置
  `runtime_healthy=FALSE` 并直接返回（fail-closed）；否则递增 `runtime_tick`，
  在 60s 周期（`SC_SELFTEST_RUNTIME_PERIOD=6000`）内分 4 步执行：tick 1
  运行期 Flash CRC-32 增量、tick 1500（`PERIOD/4`）RAM 32 字节模式校验、
  tick 3000（`PERIOD/2`）DCAN1 错误状态检查、tick 4500
  （`PERIOD*3/4`）GIO_A0 继电器读回（仅读取、非关键），非步骤 tick 直接
  返回；tick 到达 6000 回绕为 0。任一失败置 `runtime_healthy=FALSE` 并锁存。
- `SC_SelfTest_StackCanaryOk()`：金丝雀与 `SC_STACK_CANARY_VALUE` 相等返回
  TRUE，否则 FALSE。
- `SC_SelfTest_IsHealthy()`：仅当 `startup_passed==TRUE` **且**
  `runtime_healthy==TRUE` 时返回 TRUE（SWR-SC-016/017 健康门控）。

覆盖链路：

```text
测试 API 注入（op=init / op=startup [b1..b7] / op=runtime [repeats 等] / op=canary）
  → SC_SelfTest_Init()
  → SC_SelfTest_Startup()：
        {hw_lockstep_bist()==FALSE?}→1 {hw_ram_pbist()==FALSE?}→2
        {hw_flash_crc_check()==FALSE?}→3 {hw_dcan_loopback_test()==FALSE?}→4
        {hw_gpio_readback_test()==FALSE?}→5 {hw_lamp_test()==FALSE?}→6
        {hw_watchdog_test()==FALSE?}→7 →0（startup_passed=TRUE）
  → SC_SelfTest_Runtime()：
        {startup_passed==FALSE?}→runtime_healthy=FALSE,return
        tick++；{tick≥6000?}→tick=0,step=0
        {tick==1?}→Flash CRC 增量    {tick==1500?}→RAM 32B 模式校验
        {tick==3000?}→DCAN 错误状态  {tick==4500?}→GIO 继电器读回
        else→return；{step_ok==FALSE?}→runtime_healthy=FALSE（锁存）
  → SC_SelfTest_StackCanaryOk() / SC_SelfTest_IsHealthy()
  → 观测（harness 输出）：results[] 每操作快照 + 最终 state
       · result（startup 返回步骤号）· healthy（IsHealthy）
       · startupPassed / runtimeHealthy（UNIT_TEST getter）
       · tick（UNIT_TEST getter）· canaryOk
       · 各硬件 mock 调用计数（lockstepCalls..watchdogCalls、
         flashIncrCalls、dcanErrCalls）
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_selftest.c` 生产代码。7 个启动诊断与 2 个运行期诊断为 harness mock
（默认通过、按 phase 置 0/1，并逐一计数调用次数以证明失败步骤阻断后续
步骤）；GIO 继电器引脚读回经 mock GIO 的 `readback` 注入。harness 启动
自动执行一次 `SC_SelfTest_Init`（模拟上电启动），**生产逻辑零改动**；
仅新增 5 个 `#ifdef UNIT_TEST` 观测/注入钩子（仅测试编译，生产固件不含）。

## 被测代码流程图

### SC_SelfTest_Init（L59-L75）

```text
[Init]
  ═══→ [stack_canary = 0xDEADBEEF]
  ═══→ [for i in 0..31]（i%2==0 → 0xAA，否则 0x55）
  ═══→ [startup_passed=FALSE; runtime_healthy=TRUE]
  ═══→ [runtime_tick=0; runtime_step=0]
```

### SC_SelfTest_Startup（L77-L123）

```text
[Startup]
  ═══→ {hw_lockstep_bist()==FALSE?}
  │        └─ Y → [startup_passed=FALSE; return 1]   ──✗ 阻断后续
  ═══→ {hw_ram_pbist()==FALSE?}        └─ Y → return 2
  ═══→ {hw_flash_crc_check()==FALSE?}  └─ Y → return 3
  ═══→ {hw_dcan_loopback_test()==FALSE?}└─ Y → return 4
  ═══→ {hw_gpio_readback_test()==FALSE?}└─ Y → return 5
  ═══→ {hw_lamp_test()==FALSE?}        └─ Y → return 6
  ═══→ {hw_watchdog_test()==FALSE?}    └─ Y → return 7
  ═══→ [startup_passed=TRUE; return 0]
```

### SC_SelfTest_Runtime（L125-L174）

```text
[Runtime]
  ═══→ {startup_passed==FALSE?}  ── Y → [runtime_healthy=FALSE; return]
  ═══→ [runtime_tick++]
  ═══→ {runtime_tick ≥ 6000?}    ── Y → [runtime_tick=0; runtime_step=0]
  ═══→ {tick==1?}                ── Y → step_ok = hw_flash_crc_incremental()
  │    {tick==1500?}             ── Y → RAM 32B 模式校验（失败 break）
  │    {tick==3000?}             ── Y → step_ok = hw_dcan_error_check()
  │    {tick==4500?}             ── Y → gioGetBit(继电器)；step_ok=TRUE
  │    else                      ── Y → return（非步骤 tick）
  ═══→ {step_ok==FALSE?}         ── Y → [runtime_healthy=FALSE]（锁存）
```

### SC_SelfTest_StackCanaryOk / SC_SelfTest_IsHealthy（L176-L184）

```text
[StackCanaryOk]  {stack_canary == 0xDEADBEEF?} → TRUE : FALSE
[IsHealthy]      {startup_passed==TRUE && runtime_healthy==TRUE} → TRUE : FALSE
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `startup` / `runtime` / `canary` | When — 执行控制 |
| `b1..b7` | 7 项启动硬件诊断结果 | `0`（失败）/ `1`（通过） | When — 启动条件 |
| `flashIncr` | 运行期 Flash CRC 增量结果 | `0`（失败）/ `1`（通过） | When — 运行期条件 |
| `dcanErr` | 运行期 DCAN 错误状态 | `0`（有错）/ `1`（无错） | When — 运行期条件 |
| `readback` | GIO 继电器引脚读回值 | `0` / `1` | When — 运行期观测 |
| `corruptCanary` | 金丝雀损坏注入 | `0`（不损坏）/ `1`（置 0） | When — 注入 |
| `corruptRam` | RAM 模式字节损坏注入 | `0`（不损坏）/ `1`（翻转 byte0） | When — 注入 |
| `repeats` | `runtime` 调用次数 | `1`（tick1）、`10`（空转）、`1500`（RAM 步骤）、`3000`（DCAN 步骤）、`4500`（GIO 步骤）、`6001`（回绕） | When — 周期推进 |

> `b1..b7`/`flashIncr`/`dcanErr`/`corrupt*`/`readback` 均为布尔型，各两个
> 等价类。`repeats` 取各步骤 tick 的精确值覆盖 4 个运行期步骤与回绕边界；
> `6001` 覆盖 `tick≥6000` 回绕分支两侧。每个 `startup` 阶段重置 7 项诊断
> mock；每个 `runtime` 阶段重置运行期 mock 与继电器读回。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].result` | `Startup` 返回的失败步骤号 | `0`（通过）/ `1..7` |
| `results[i].healthy` | `SC_SelfTest_IsHealthy()` | `0`/`1` |
| `results[i].startupPassed` | `startup_passed` 标志（UNIT_TEST getter） | `0`/`1` |
| `results[i].runtimeHealthy` | `runtime_healthy` 标志（UNIT_TEST getter） | `0`/`1` |
| `results[i].tick` | 运行期 tick 计数（UNIT_TEST getter） | `0`/`1`/`10`/`1500`/`3000`/`4500` |
| `results[i].canaryOk` | `SC_SelfTest_StackCanaryOk()` | `0`/`1` |
| `results[i].{lockstep..watchdog}Calls` | 各启动诊断 mock 调用计数 | `0`/`1`/`2` |
| `state.flashIncrCalls` / `dcanErrCalls` | 运行期 mock 累计调用计数 | `1`/`2` |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。
> 每个请求独立进程，harness 启动自动 `SC_SelfTest_Init`（模拟上电启动）。

### 规则: 初始化与堆栈金丝雀 — SWR-SC-021

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_plants_canary` | P0: init | canaryOk=1, healthy=0, tick=0（最短路径：Init 后金丝雀正确、未启动不健康） |
| `corrupt_canary_fails` | P0: init; P1: canary corruptCanary=1 | P0 canaryOk=1；P1 canaryOk=0（金丝雀损坏 → 检查失败） |

### 规则: 启动自检全通过 — SWR-SC-019

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `startup_all_pass` | P0: startup | result=0, healthy=1, startupPassed=1, 7 项诊断各调用 1 次 |
| `startup_idempotent` | P0: startup; P1: startup | P0/P1 result=0；state.healthy=1, lockstepCalls=2 |

### 规则: 启动自检失败 — 返回步骤号并阻断后续步骤

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `step1_fail_blocks_rest` | P0: startup b1=0 b7=0 | result=1, healthy=0, startupPassed=0, lockstepCalls=1 且 ramPbist..watchdogCalls 全为 0（阻断证明） |
| `step2_ram_pbist_fail` | P0: startup b2=0 | result=2, ramPbistCalls=1, flashCrcCalls=0 |
| `step3_flash_crc_fail` | P0: startup b3=0 | result=3, flashCrcCalls=1, dcanLoopbackCalls=0 |
| `step4_dcan_loopback_fail` | P0: startup b4=0 | result=4, dcanLoopbackCalls=1, gpioReadbackCalls=0 |
| `step5_gpio_readback_fail` | P0: startup b5=0 | result=5, gpioReadbackCalls=1, lampCalls=0 |
| `step6_lamp_test_fail` | P0: startup b6=0 | result=6, lampCalls=1, watchdogCalls=0 |
| `step7_watchdog_test_fail` | P0: startup b7=0 | result=7, watchdogCalls=1, healthy=0, startupPassed=0 |

### 规则: 运行期周期分步 — SWR-SC-020

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `tick1_flash_incremental` | P0: startup; P1: runtime repeats=1 | P1 tick=1, healthy=1, runtimeHealthy=1 |
| `tick1500_ram_check` | P0: startup; P1: runtime repeats=1500 | P1 tick=1500, healthy=1, runtimeHealthy=1 |
| `tick3000_dcan_check` | P0: startup; P1: runtime repeats=3000 | P1 tick=3000, healthy=1, runtimeHealthy=1 |
| `tick4500_gpio_readback` | P0: startup; P1: runtime repeats=4500 readback=1 | P1 tick=4500, healthy=1（读回非关键） |
| `non_step_tick_returns` | P0: startup; P1: runtime repeats=10 | P1 tick=10, healthy=1（空转 tick 直接返回） |
| `period_wrap_restarts` | P0: startup; P1: runtime repeats=6001 | P1 tick=1, healthy=1, state.flashIncrCalls=2（6000 回绕后重做第一步） |

### 规则: 运行期失败 — 锁存不健康

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `flash_incr_fail_unhealthy` | P0: startup; P1: runtime repeats=1 flashIncr=0 | P1 tick=1, healthy=0, startupPassed=1, runtimeHealthy=0 |
| `ram_corrupt_fail_unhealthy` | P0: startup; P1: runtime repeats=1500 corruptRam=1 | P1 tick=1500, healthy=0, runtimeHealthy=0 |
| `dcan_err_fail_unhealthy` | P0: startup; P1: runtime repeats=3000 dcanErr=0 | P1 tick=3000, healthy=0, runtimeHealthy=0 |

### 规则: 启动未完成即运行 — fail-closed

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `runtime_before_startup` | P0: runtime repeats=10 | P0 tick=0, healthy=0, startupPassed=0, runtimeHealthy=0（不递增 tick、不执行检查） |

### 规则: 健康状态组合与复位 — SWR-SC-016/017

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `startup_fail_then_runtime` | P0: startup b1=0; P1: runtime repeats=1 | P1 tick=0, healthy=0, startupPassed=0, runtimeHealthy=0（启动失败后运行期也进入不健康） |
| `reinit_resets_state` | P0: startup; P1: init | P1 canaryOk=1, healthy=0, tick=0（重复 Init 复位全部状态） |

## 代码路径覆盖

- `SC_SelfTest_Init`：金丝雀植入、RAM 交替模式写循环（0xAA/0x55 两分支）、
  4 个状态标志复位全覆盖（`init_plants_canary`、`reinit_resets_state` +
  全部用例的自动 Init）。
- `SC_SelfTest_Startup`：
  - 7 个 `hw_*()==FALSE` 判断点两侧全覆盖：true 侧分别由
    `step1..step7_fail` 七个用例驱动（各返回 1..7），false 侧由
    `startup_all_pass` / `startup_idempotent` 驱动。
  - 失败阻断语义经 mock 调用计数证明（`step1_fail_blocks_rest` 断言
    后续 6 项诊断计数为 0）。
- `SC_SelfTest_Runtime`：
  - `startup_passed==FALSE` 早退分支（`runtime_before_startup`、
    `startup_fail_then_runtime`），false 侧全部已启动用例。
  - `tick≥6000` 回绕（`period_wrap_restarts`）。
  - 4 个运行期步骤分支：tick1 Flash 增量（`tick1_flash_incremental`）、
    tick1500 RAM 校验（`tick1500_ram_check`）、tick3000 DCAN 错误
    （`tick3000_dcan_check`）、tick4500 GIO 读回（`tick4500_gpio_readback`），
    以及 else 空转返回（`non_step_tick_returns`）。
  - RAM 校验内部循环与 `ram_test_area[i]!=expected` 失配 break
    （`tick1500_ram_check` 全匹配 / `ram_corrupt_fail_unhealthy` 失配）。
  - `step_ok==FALSE` 锁存（`flash_incr_fail_unhealthy` /
    `ram_corrupt_fail_unhealthy` / `dcan_err_fail_unhealthy`），
    false 侧全部健康运行期用例。
- `SC_SelfTest_StackCanaryOk`：`stack_canary==0xDEADBEEF` 两侧
  （`init_plants_canary` / `corrupt_canary_fails`）。
- `SC_SelfTest_IsHealthy`：`&&` 两侧——`startup_passed` false 侧
  （`runtime_before_startup`）、true 且 `runtime_healthy` false 侧
  （`flash_incr_fail_unhealthy` 等）、双 true 侧（`startup_all_pass`、
  `tick1_flash_incremental` 等）。

## 覆盖率报告实测

> 本模块无 `#ifdef PLATFORM_HIL/SIL_DIAG` 编译期排除项（SC 平台仅以生产
> TMS570 配置编译）；无防御性守卫；全部可执行行均可经公开 API + UNIT_TEST
> 钩子驱动。

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_selftest.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（99 / 99）** |
| 分支覆盖 | **100%（44 / 44）** |
| 函数覆盖 | **100%（10 / 10，含 5 个 `#ifdef UNIT_TEST` 观测/注入钩子，生产固件不含）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_selftest.feature` | **23 scenarios / 138 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **758 scenarios / 4577 steps passed**（含本 feature 23 场景） |

函数命中次数（`sc_selftest.c.func.html`，全量运行后）：

| 函数 | 命中 |
|---|---:|
| `SC_SelfTest_Init` | 53 |
| `SC_SelfTest_Startup` | 43 |
| `SC_SelfTest_Runtime` | 40548 |
| `SC_SelfTest_StackCanaryOk` | 55 |
| `SC_SelfTest_IsHealthy` | 119 |
| `SC_SelfTest_TestGetRuntimeTick`（UNIT_TEST） | 76 |
| `SC_SelfTest_TestGetStartupPassed`（UNIT_TEST） | 113 |
| `SC_SelfTest_TestGetRuntimeHealthy`（UNIT_TEST） | 70 |
| `SC_SelfTest_TestCorruptCanary`（UNIT_TEST） | 2 |
| `SC_SelfTest_TestCorruptRam`（UNIT_TEST） | 2 |

### 逐行代码覆盖映射

> 下表直接依据 `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_selftest.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### SC_SelfTest_Init（L59-L75）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L60 | `{` | 53 | 全部 23 场景 × 启动自动 Init（含显式 init 阶段） |
| L61 | `uint8 i;` | 53 | 同上 |
| L64 | `stack_canary = SC_STACK_CANARY_VALUE;` | 53 | 全部场景（`init_plants_canary` 断言 canaryOk=1） |
| L67 | `for (i = 0u; i < SC_RAM_TEST_SIZE; i++)` | 1749（分支 0×1696 / 分支 1×53） | 全部场景（32 次循环 × 53 次 Init） |
| L68 | `ram_test_area[i] = ((i % 2u) == 0u) ? 0xAAu : 0x55u;` | 1696（0xAA×848 / 0x55×848） | 同上（交替模式写入） |
| L69 | `}` | 1696 | 同上 |
| L71 | `startup_passed = FALSE;` | 53 | 全部场景 |
| L72 | `runtime_healthy = TRUE;` | 53 | 全部场景 |
| L73 | `runtime_tick = 0u;` | 53 | 全部场景 |
| L74 | `runtime_step = 0u;` | 53 | 全部场景 |
| L75 | `}` | 53 | 全部场景 |

#### SC_SelfTest_Startup（L77-L123）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L78 | `{` | 43 | 全部 startup 阶段（43 次） |
| L80 | `if (hw_lockstep_bist() == FALSE)` | 43（true 4 / false 39） | true `step1_fail_blocks_rest`、`startup_fail_then_runtime`；false 其余 |
| L81 | `startup_passed = FALSE;` | 4 | `step1_fail_blocks_rest`、`startup_fail_then_runtime` |
| L82 | `return 1u;` | 4 | 同上 |
| L83 | `}` | 4 | 同上 |
| L86 | `if (hw_ram_pbist() == FALSE)` | 39（true 2 / false 37） | true `step2_ram_pbist_fail` |
| L87 | `startup_passed = FALSE;` | 2 | `step2_ram_pbist_fail` |
| L88 | `return 2u;` | 2 | 同上 |
| L89 | `}` | 2 | 同上 |
| L92 | `if (hw_flash_crc_check() == FALSE)` | 37（true 2 / false 35） | true `step3_flash_crc_fail` |
| L93 | `startup_passed = FALSE;` | 2 | `step3_flash_crc_fail` |
| L94 | `return 3u;` | 2 | 同上 |
| L95 | `}` | 2 | 同上 |
| L98 | `if (hw_dcan_loopback_test() == FALSE)` | 35（true 2 / false 33） | true `step4_dcan_loopback_fail` |
| L99 | `startup_passed = FALSE;` | 2 | `step4_dcan_loopback_fail` |
| L100 | `return 4u;` | 2 | 同上 |
| L101 | `}` | 2 | 同上 |
| L104 | `if (hw_gpio_readback_test() == FALSE)` | 33（true 2 / false 31） | true `step5_gpio_readback_fail` |
| L105 | `startup_passed = FALSE;` | 2 | `step5_gpio_readback_fail` |
| L106 | `return 5u;` | 2 | 同上 |
| L107 | `}` | 2 | 同上 |
| L110 | `if (hw_lamp_test() == FALSE)` | 31（true 2 / false 29） | true `step6_lamp_test_fail` |
| L111 | `startup_passed = FALSE;` | 2 | `step6_lamp_test_fail` |
| L112 | `return 6u;` | 2 | 同上 |
| L113 | `}` | 2 | 同上 |
| L116 | `if (hw_watchdog_test() == FALSE)` | 29（true 2 / false 27） | true `step7_watchdog_test_fail` |
| L117 | `startup_passed = FALSE;` | 2 | `step7_watchdog_test_fail` |
| L118 | `return 7u;` | 2 | 同上 |
| L119 | `}` | 2 | 同上 |
| L121 | `startup_passed = TRUE;` | 27 | `startup_all_pass`、`startup_idempotent` 及全部运行期用例的前置 startup |
| L122 | `return 0u;` | 27 | 同上 |
| L123 | `}` | 29 | 全部 startup 阶段 |

#### SC_SelfTest_Runtime（L125-L174）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L126 | `{` | 40548 | 全部 runtime 调用（repeats 批量，累计 40548 次） |
| L127 | `boolean step_ok = TRUE;` | 40548 | 同上 |
| L129 | `if (startup_passed == FALSE)` | 40548（true 22 / false 40526） | true `runtime_before_startup`（10 次×2）与 `startup_fail_then_runtime`（1 次×2）；false 全部已启动用例 |
| L130 | `runtime_healthy = FALSE;` | 22 | `runtime_before_startup`、`startup_fail_then_runtime`（fail-closed 早退） |
| L131 | `return;` | 22 | 同上 |
| L132 | `}` | 22 | 同上 |
| L135 | `runtime_tick++;` | 40526 | 全部已启动 runtime 调用 |
| L138 | `if (runtime_tick >= SC_SELFTEST_RUNTIME_PERIOD)` | 40526（true 2 / false 40524） | true `period_wrap_restarts`（tick 6000 回绕） |
| L139 | `runtime_tick = 0u;` | 2 | `period_wrap_restarts` |
| L140 | `runtime_step = 0u;` | 2 | `period_wrap_restarts` |
| L141 | `}` | 2 | 同上 |
| L144 | `if (runtime_tick == 1u)` | 40526（true 21 / false 40505） | true `tick1_flash_incremental`、`flash_incr_fail_unhealthy`、`period_wrap_restarts`（回绕后）等 |
| L146 | `step_ok = hw_flash_crc_incremental();` | 21 | `tick1_flash_incremental`、`flash_incr_fail_unhealthy`、`period_wrap_restarts` 等 tick=1 调用 |
| L147 | `else if (runtime_tick == (SC_SELFTEST_RUNTIME_PERIOD / 4u))` | 40505（true 13 / false 40492） | true `tick1500_ram_check`、`ram_corrupt_fail_unhealthy` 等 tick=1500 调用 |
| L149 | `uint8 i;` | 13 | tick=1500 的 13 次调用 |
| L150 | `for (i = 0u; i < SC_RAM_TEST_SIZE; i++)` | 365（分支 0×354 / 分支 1×11） | RAM 校验循环（32 字节 × 11 次全匹配 + 2 次失配早退） |
| L151 | `uint8 expected = ((i % 2u) == 0u) ? 0xAAu : 0x55u;` | 354（0xAA×178 / 0x55×176） | 同上（期望模式计算） |
| L152 | `if (ram_test_area[i] != expected)` | 354（true 2 / false 352） | true `ram_corrupt_fail_unhealthy`（byte0 损坏失配 break） |
| L153 | `step_ok = FALSE;` | 2 | `ram_corrupt_fail_unhealthy` |
| L154 | `break;` | 2 | 同上 |
| L155 | `}` | 2 | 同上 |
| L156 | `}` | 354 | RAM 校验循环 |
| L157 | `else if (runtime_tick == (SC_SELFTEST_RUNTIME_PERIOD / 2u))` | 40492（true 8 / false 40484） | true `tick3000_dcan_check`、`dcan_err_fail_unhealthy` 等 tick=3000 调用 |
| L159 | `step_ok = hw_dcan_error_check();` | 8 | `tick3000_dcan_check`、`dcan_err_fail_unhealthy` 等 |
| L160 | `else if (runtime_tick == ((SC_SELFTEST_RUNTIME_PERIOD * 3u) / 4u))` | 40484（true 4 / false 40480） | true `tick4500_gpio_readback` 等 tick=4500 调用 |
| L162 | `uint8 readback = gioGetBit(SC_GIO_PORT_A, SC_PIN_RELAY);` | 4 | `tick4500_gpio_readback` 等（readback=1 注入） |
| L164 | `(void)readback;` | 4 | 同上 |
| L165 | `step_ok = TRUE;` | 4 | 同上（读回非关键） |
| L166 | `else` | 40480 | 空转 tick（tick≠1/1500/3000/4500） |
| L168 | `return;` | 40480 | `non_step_tick_returns` 及其余空转 tick |
| L169 | `}` | 40480 | 同上 |
| L171 | `if (step_ok == FALSE)` | 46（true 6 / false 40） | true `flash_incr_fail_unhealthy`、`ram_corrupt_fail_unhealthy`、`dcan_err_fail_unhealthy` |
| L172 | `runtime_healthy = FALSE;` | 6 | 三个运行期失败用例（锁存不健康） |
| L173 | `}` | 6 | 同上 |
| L174 | `}` | 46 | 全部运行期调用 |

#### SC_SelfTest_StackCanaryOk / SC_SelfTest_IsHealthy（L176-L184）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L177 | `{` | 55 | 全部 canaryOk 断言 |
| L178 | `return (stack_canary == SC_STACK_CANARY_VALUE) ? TRUE : FALSE;` | 55（true 51 / false 4） | true `init_plants_canary` 等；false `corrupt_canary_fails` |
| L179 | `}` | 55 | 同上 |
| L182 | `{` | 119 | 全部 healthy 断言 |
| L183 | `return (startup_passed == TRUE) && (runtime_healthy == TRUE) ? TRUE : FALSE;` | 119（`&&` 双 true 69 / 组合 false 50） | 双 true `startup_all_pass`、`tick1_flash_incremental` 等；`startup_passed` false 侧 `runtime_before_startup`；`runtime_healthy` false 侧 `flash_incr_fail_unhealthy` 等 |
| L184 | `}` | 119 | 同上 |

### 分支覆盖分析

44/44 分支全部两侧命中：

| 行号 | 判断 | 分支 0（真） | 分支 1（假） |
|---|---|---:|---:|
| L67 | `Init: i < SC_RAM_TEST_SIZE` | 1696（循环体） | 53（退出） |
| L68 | `Init: (i % 2u) == 0u` | 848（0xAA） | 848（0x55） |
| L80 | `Startup: hw_lockstep_bist() == FALSE` | 4（返回 1） | 39 |
| L86 | `Startup: hw_ram_pbist() == FALSE` | 2（返回 2） | 37 |
| L92 | `Startup: hw_flash_crc_check() == FALSE` | 2（返回 3） | 35 |
| L98 | `Startup: hw_dcan_loopback_test() == FALSE` | 2（返回 4） | 33 |
| L104 | `Startup: hw_gpio_readback_test() == FALSE` | 2（返回 5） | 31 |
| L110 | `Startup: hw_lamp_test() == FALSE` | 2（返回 6） | 29 |
| L116 | `Startup: hw_watchdog_test() == FALSE` | 2（返回 7） | 27 |
| L129 | `Runtime: startup_passed == FALSE` | 22（早退不健康） | 40526 |
| L138 | `Runtime: runtime_tick >= 6000` | 2（回绕） | 40524 |
| L144 | `Runtime: runtime_tick == 1` | 21（Flash 增量） | 40505 |
| L147 | `Runtime: runtime_tick == 1500` | 13（RAM 校验） | 40492 |
| L150 | `Runtime: i < SC_RAM_TEST_SIZE` | 354（循环体） | 11（退出） |
| L151 | `Runtime: (i % 2u) == 0u` | 178（0xAA） | 176（0x55） |
| L152 | `Runtime: ram_test_area[i] != expected` | 2（失配 break） | 352（全匹配） |
| L157 | `Runtime: runtime_tick == 3000` | 8（DCAN 错误） | 40484 |
| L160 | `Runtime: runtime_tick == 4500` | 4（GIO 读回） | 40480 |
| L171 | `Runtime: step_ok == FALSE` | 6（锁存不健康） | 40 |
| L178 | `CanaryOk: stack_canary == SC_STACK_CANARY_VALUE` | 51（完好） | 4（损坏） |
| L183 | `IsHealthy: startup_passed == TRUE && runtime_healthy == TRUE` | 双 true 69 | 组合 false 50 |

## 无法覆盖的代码说明

> 本模块**无**无法覆盖的可执行代码。
>
> - `sc_selftest.c` 不含任何 `#ifdef PLATFORM_HIL` / `SIL_DIAG` 编译期排除项
>   （SC 平台 harness 均以生产 TMS570 配置编译，无 PLATFORM_POSIX/HIL），
>   因此没有像 `sc_e2e`/`sc_relay` 那样的预处理器排除分支。
> - 无配置指针、无初始化守卫、无防御性分支——7 个启动判断、4 个运行期
>   步骤判断、回绕判断、金丝雀判断与健康门控 `&&` 的全部 44 个分支两侧
>   均被端到端用例命中。
> - `runtime_step`（L74/L140）为模块内从未被读取的残留状态变量（仅赋值），
>   不构成可测逻辑；其赋值行经 Init 与回绕用例执行覆盖，无独立分支。
> - 唯一需要注入的失败路径（金丝雀损坏、RAM 模式损坏）经
>   `#ifdef UNIT_TEST` 钩子 `SC_SelfTest_TestCorruptCanary` /
>   `SC_SelfTest_TestCorruptRam` 驱动——这两处损坏在生产固件中仅由真实
>   内存损坏/栈溢出可达，无法经公开 API 构造（与 `sc_state` 的
>   `SC_State_TestSetRaw` 同型豁免）；钩子仅测试编译，不影响交付固件。
>   RAM 校验失配分支（L152 true 侧）由 `ram_corrupt_fail_unhealthy` 场景
>   经该钩子驱动。
> - 5 个 UNIT_TEST 钩子（TestGetRuntimeTick / TestGetStartupPassed /
>   TestGetRuntimeHealthy / TestCorruptCanary / TestCorruptRam）计入函数
>   覆盖（10/10），生产固件不含。
>
> **99/99 行、44/44 分支、10/10 函数全部被端到端测试覆盖，无无法覆盖的
> 可执行代码**。
