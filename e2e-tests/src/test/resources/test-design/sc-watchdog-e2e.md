# SC 外部看门狗喂狗控制 (sc_watchdog) E2E 测试设计

## 被测功能

**SC 外部看门狗（TPS3823）喂狗控制（SWR-SC-022，ASIL D）**

`sc_watchdog.c` 是 SC 的 TPS3823 外部看门狗喂狗模块。TPS3823 需要周期性
翻转 WDI 引脚（约 1.6s 超时）；若任一监控条件失败导致 WDI 不再翻转，
看门狗将饿死并断言 RESET，复位 MCU。两个公开 API：

- `SC_Watchdog_Init()`：置 `wdi_state = 0`，写 WDI 引脚（`SC_GIO_PORT_A` /
  `SC_PIN_WDI`，即 GIO_A5）为 LOW（安全态）。
- `SC_Watchdog_Feed(boolean allChecksOk)`：仅当 `allChecksOk == TRUE`（全部
  监控条件满足：主循环完成 / RAM 模式测试完好 / DCAN1 未 bus-off / ESM
  锁步无错误 / 栈金丝雀完好）时，将 `wdi_state` 异或 1 翻转并写回 WDI 引脚；
  任一条件失败（`FALSE`）时直接返回、**不写** WDI 引脚（看门狗饿死 →
  TPS3823 超时复位）。

> 本模块为 SC 五个公开 SWC 中最简单的纯门控模块：无配置指针、无初始化
> 守卫、无 `#ifdef` 分支。`wdi_state` 与 WDI 引脚电平一一对应（每次状态
> 翻转必然伴随一次引脚写入），因此通过 harness 的 mock GIO 观测引脚电平
> 与写入计数即可完全观测模块行为，**生产代码零改动**（无需 UNIT_TEST
> getter）。

覆盖链路：

```text
测试 API 注入（op=init / op=feed ok=0|1 [repeats=N]）
  → SC_Watchdog_Init()：
       · wdi_state = 0
       · gioSetBit(GIO_A, PIN_WDI, 0)（安全态）
  → SC_Watchdog_Feed(allChecksOk)：
       · {allChecksOk == TRUE?}
           ├─ Y → wdi_state ^= 1；gioSetBit(GIO_A, PIN_WDI, wdi_state)
           └─ N → 直接返回（不写引脚，看门狗饿死）
  → 观测（harness 输出）：results[] 每操作 state 快照
       · wdiPin（mock GIO 读回的 WDI 引脚电平）
       · wdiWriteCount（对 WDI 引脚的总写入次数，含 Init 写入）
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_watchdog.c` 生产代码。WDI GIO 引脚（`SC_GIO_PORT_A` / `SC_PIN_WDI`）
为 harness mock：`gioSetBit` 记录输出电平并镜像为读回，同时对 WDI 引脚
的每次写入累计 `wdi_write_count`。harness 启动自动执行一次
`SC_Watchdog_Init`（模拟上电启动），**无任何生产代码修改**。

## 被测代码流程图

### SC_Watchdog_Init（L27-L31）

```text
[Init]
  ═══→ [wdi_state = 0]
  ═══→ [gioSetBit(GIO_A, PIN_WDI, 0)]（安全态）
```

### SC_Watchdog_Feed（L33-L42）

```text
[Feed(allChecksOk)]
  ═══→ {allChecksOk == TRUE?}
          ├─ Y → [wdi_state ^= 1]（异或翻转）
          │     → [gioSetBit(GIO_A, PIN_WDI, wdi_state)]
          └─ N → [return]（不写引脚，看门狗饿死 → TPS3823 超时复位）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `feed` | When — 执行控制 |
| `ok` | `allChecksOk` 参数 | `0`（条件失败，饿死）/ `1`（全部满足，翻转） | When — 门控条件 |
| `repeats` | `feed` 调用次数 | `1`（单次）、`2`/`3`（翻转回绕）、`10`/`100`（长时间失败/长序列边界） | When — 执行控制 |
| 前置状态 | 进入操作前 `wdi_state`/引脚电平 | 未初始化 / 已喂狗（HIGH）/ 已饿死（LOW） | When — 前置条件 |

> `allChecksOk` 为布尔型，仅两个等价类（TRUE/FALSE）。`repeats` 用于
> 覆盖连续失败（饿死持久化）与长喂狗序列的翻转回绕边界（偶数次回 LOW、
> 奇数次回 HIGH）。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].state.wdiPin` | mock GIO 读回的 WDI 引脚电平 | `0`/`1` |
| `results[i].state.wdiWriteCount` | 对 WDI 引脚的总写入次数（含 Init） | `1`/`2`/`3`/…/`101` |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。
> 每个请求独立进程，harness 启动自动 `SC_Watchdog_Init`（模拟上电启动，
> 故初始 `wdiWriteCount=1`）。

### 规则: 初始化 — SC_Watchdog_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_sets_wdi_low` | P0: init | wdiPin=0, wdiWriteCount=2（自动 Init + 显式 Init） |
| `reinit_resets_wdi_low` | P0: feed ok=1; P1: init | P0 wdiPin=1；P1 wdiPin=0, wdiWriteCount=3（重复 Init 复位） |

### 规则: 喂狗成功 — allChecksOk==TRUE

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `feed_ok_toggles_high` | P0: feed ok=1 | wdiPin=1, wdiWriteCount=2 |
| `feed_ok_twice_back_low` | P0: feed ok=1 repeats=2 | wdiPin=0, wdiWriteCount=3（0→1→0） |
| `feed_ok_three_high` | P0: feed ok=1 repeats=3 | wdiPin=1, wdiWriteCount=4（0→1→0→1） |
| `feed_ok_100_even_low` | P0: feed ok=1 repeats=100 | wdiPin=0, wdiWriteCount=101（偶数次翻转回 LOW） |

### 规则: 喂狗失败饿死 — allChecksOk==FALSE

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `feed_fail_sticks_low` | P0: feed ok=0 repeats=3 | wdiPin=0, wdiWriteCount=1（无写入，饿死） |
| `feed_fail_after_toggle_high` | P0: feed ok=1; P1: feed ok=0 repeats=2 | P1 wdiPin=1, wdiWriteCount=2（保持 HIGH，无写入） |

### 规则: 交替成功/失败 — 保持最后成功状态

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `alternating_pass_fail_continues` | P0: feed ok=1; P1: feed ok=0; P2: feed ok=1 | P0 wdiPin=1 → P1 wdiPin=1（失败不改变）→ P2 wdiPin=0（成功继续翻转） |

### 规则: 失败恢复 — SWR-SC-022

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `resume_after_failure_toggles` | P0: feed ok=0 repeats=10; P1: feed ok=1; P2: feed ok=1 | P0 wdiPin=0, wc=1（饿死）→ P1 wdiPin=1（恢复翻转）→ P2 wdiPin=0 |

## 代码路径覆盖

- `SC_Watchdog_Init`：`wdi_state=0` 赋值 + WDI 引脚 LOW 写入全路径覆盖
  （`init_sets_wdi_low`、`reinit_resets_wdi_low`、全部用例的自动 Init）。
- `SC_Watchdog_Feed`：
  - `allChecksOk == TRUE` 分支两侧全覆盖：true 侧（`feed_ok_toggles_high`
    等全部成功喂狗用例）与 false 侧（`feed_fail_sticks_low`、
    `feed_fail_after_toggle_high`、`alternating_pass_fail_continues` 的
    P1、`resume_after_failure_toggles` 的 P0）。
  - `wdi_state ^= 1u` 异或翻转：奇偶次交替（0→1→0→1→…）经
    `feed_ok_twice_back_low` / `feed_ok_three_high` / `feed_ok_100_even_low`
    验证回绕正确。
  - 失败路径不写引脚：`wdiWriteCount` 不累加（`feed_fail_sticks_low` /
    `feed_fail_after_toggle_high`）。
- 失败恢复：失败不改变 `wdi_state`，恢复后从最后成功电平继续翻转
  （`alternating_pass_fail_continues` / `resume_after_failure_toggles`）。

## 覆盖率报告实测

> 本模块为纯门控模块，无 `#ifdef PLATFORM_HIL/SIL_DIAG` 编译期排除项，
> 无防御性守卫；全部可执行行均可经公开 API 驱动。

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_watchdog.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（10 / 10）** |
| 分支覆盖 | **100%（2 / 2）** |
| 函数覆盖 | **100%（2 / 2）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_watchdog.feature` | **10 scenarios / 60 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **735 scenarios / 4439 steps passed** |

函数命中次数（`sc_watchdog.c.func.html`，全量运行后）：

| 函数 | 命中 |
|---|---:|
| `SC_Watchdog_Feed` | 259 |
| `SC_Watchdog_Init` | 26 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_watchdog.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### SC_Watchdog_Init（L27-L31）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L28 | `{` | 26 | 全部用例（10 个场景 × 启动自动 Init，含 2 个显式 init 阶段） |
| L29 | `wdi_state = 0u;` | 26 | 全部用例 |
| L30 | `gioSetBit(SC_GIO_PORT_A, SC_PIN_WDI, 0u);` | 26 | 全部用例（安全态断言 wdiPin=0） |
| L31 | `}` | 26 | 全部用例 |

#### SC_Watchdog_Feed（L33-L42）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L34 | `{` | 259 | 全部 feed 调用（含 `repeats` 批量调用） |
| L35 | `if (allChecksOk == TRUE)` | 259 | true 侧 `feed_ok_toggles_high`、`feed_ok_twice_back_low`、`feed_ok_three_high`、`feed_ok_100_even_low`、`alternating_pass_fail_continues`(P0/P2)、`resume_after_failure_toggles`(P1/P2)、`reinit_resets_wdi_low`(P0)；false 侧 `feed_fail_sticks_low`、`feed_fail_after_toggle_high`(P1)、`alternating_pass_fail_continues`(P1)、`resume_after_failure_toggles`(P0) |
| L37 | `wdi_state ^= 1u;` | 226 | 全部成功喂狗用例（wdiPin 奇偶翻转断言） |
| L38 | `gioSetBit(SC_GIO_PORT_A, SC_PIN_WDI, wdi_state);` | 226 | 同上（wdiWriteCount 递增断言） |
| L39 | `}` | 226 | 同上 |
| L42 | `}` | 259 | 全部 feed 调用 |

### 分支覆盖分析

唯一判断点两侧全部命中（2/2 分支）：

| 行号 | 判断 | 分支 0（真） | 分支 1（假） |
|---|---|---|---:|
| L35 | `Feed: allChecksOk == TRUE` | 226（成功喂狗用例，WDI 翻转） | 33（失败饿死用例，WDI 不写） |

## 无法覆盖的代码说明

> 本模块**无**无法覆盖的可执行代码。
>
> - `sc_watchdog.c` 不含任何 `#ifdef PLATFORM_HIL` / `SIL_DIAG` / `UNIT_TEST`
>   分支，无编译期排除项。
> - 无配置指针、无初始化守卫、无防御性分支——唯一分支（`allChecksOk ==
>   TRUE`）两侧均被端到端用例覆盖（成功喂狗/失败饿死）。
> - 与 CVC `Swc_Watchdog`（存在 `Wdg_CfgPtr == NULL_PTR` 防御守卫豁免）或
>   RZC `Swc_RzcScheduler`（编译期只读表防御分支豁免）不同，本模块无需
>   任何文档化豁免。
> - 未新增 UNIT_TEST 观测 getter：`wdi_state` 与 WDI 引脚电平一一对应，
>   mock GIO 的 `wdiPin` / `wdiWriteCount` 已完全观测模块行为，**生产代码
>   零改动**。
>
> **10/10 行、2/2 分支、2/2 函数全部被端到端测试覆盖，无无法覆盖的
> 可执行代码**。
