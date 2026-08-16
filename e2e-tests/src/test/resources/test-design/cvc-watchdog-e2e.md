# CVC 看门狗 (Swc_Watchdog) E2E 测试设计

## 被测功能

**CVC ASW 外部看门狗 SWC — TPS3823 WDI 喂狗四条件门控（主循环完成 / 栈金丝雀
完好 / RAM 模式测试通过 / CAN 未 bus-off）+ 初始化守卫（NULL 配置、未初始化）**

覆盖链路：

```text
测试 API 注入（loopComplete / canaryOk / ramOk / canOk + 执行控制）
  → Swc_Watchdog_Init(ConfigPtr)（SWR-CVC-023）：
       · ConfigPtr == NULL → 去初始化（Wdg_Initialized=FALSE, Wdg_CfgPtr=NULL）
       · 有效配置 → Wdg_CfgPtr=配置, Wdg_FeedCount=0, Wdg_Initialized=TRUE
  → Swc_Watchdog_Feed(loopComplete, canaryOk, ramOk, canOk)（SWR-CVC-023）：
       · Wdg_Initialized != TRUE → E_NOT_OK（未初始化守卫）
       · Wdg_CfgPtr == NULL     → E_NOT_OK（防御性守卫，公开 API 不可达）
       · loopComplete != TRUE   → E_NOT_OK（主循环未完成）
       · canaryOk     != TRUE   → E_NOT_OK（栈金丝雀损坏）
       · ramOk        != TRUE   → E_NOT_OK（RAM 模式测试失败）
       · canOk        != TRUE   → E_NOT_OK（CAN bus-off）
       · 全部满足 → Dio_FlipChannel(wdiDioChannel) 翻转 WDI + Wdg_FeedCount++
  → 观测（getter / harness mock）：Wdg_Initialized / Wdg_FeedCount / Dio 翻转计数
```

与既有 ASW E2E（`Swc_Heartbeat`、`Swc_CanMonitor` 等）一致，通过测试专用 API 在
原生测试框架内执行真实的 `Swc_Watchdog.c` 生产代码。喂狗四条件由 harness 脚本
注入，`Dio_FlipChannel` 在 harness 中以计数 mock 替身（真实 STM32 MCAL 仅在
物理固件中存在），内部状态（初始化标志、喂狗计数）经 `#ifdef UNIT_TEST` 观测
getter 断言。

> **被测代码观测**：`Wdg_Initialized`、`Wdg_FeedCount` 均为模块静态状态，无法
> 从外部直接读取。为支持 E2E 断言，在 `Swc_Watchdog.c/.h` 增加了
> **`#ifdef UNIT_TEST` 保护**的观测 getter（`Swc_Watchdog_GetInitialized` /
> `Swc_Watchdog_GetFeedCount`）。生产固件构建（STM32/TMS570/POSIX target）不
> 定义 `UNIT_TEST`，这些访问器不进入交付固件；仅测试 harness 编译时生效。
> WDI 引脚翻转行为通过 harness 内 `Dio_FlipChannel` 计数 mock 观测
> （`dioFlipCount` / `dioLastChannel`）。

## 被测代码流程图

```
┌──────────────────────────────────┐
│ Swc_Watchdog_Init(ConfigPtr)     │
└─────────────────┬────────────────┘
                  │
   ConfigPtr == NULL? ──Y──→ Wdg_Initialized=FALSE, Wdg_CfgPtr=NULL, return
                  │N
   Wdg_CfgPtr = ConfigPtr
   Wdg_FeedCount = 0
   Wdg_Initialized = TRUE
```

```
┌──────────────────────────────────────────┐
│ Swc_Watchdog_Feed(loopComplete, canaryOk,│
│                   ramOk, canOk)          │
└─────────────────────┬────────────────────┘
                      │
  Wdg_Initialized != TRUE? ──Y──→ return E_NOT_OK
                      │N
  Wdg_CfgPtr == NULL_PTR? ──Y──→ return E_NOT_OK（防御性，公开 API 不可达）
                      │N
  loopComplete != TRUE? ──Y──→ return E_NOT_OK
                      │N
  canaryOk != TRUE? ──────Y──→ return E_NOT_OK
                      │N
  ramOk != TRUE? ──────────Y──→ return E_NOT_OK
                      │N
  canOk != TRUE? ──────────Y──→ return E_NOT_OK
                      │N
  Dio_FlipChannel(wdiDioChannel)   ← WDI 引脚翻转（harness mock 计数）
  Wdg_FeedCount++
  return E_OK
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Watchdog_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `initNull` | 是否调用 `Swc_Watchdog_Init(NULL)` | `false`（有效配置）、`true`（NULL 配置守卫） | When — 执行控制 |
| `loopComplete` | Feed 的主循环完成标志 | `true`（完成）、`false`（未完成） | When — 状态注入 |
| `canaryOk` | Feed 的栈金丝雀标志 | `true`（完好）、`false`（损坏） | When — 状态注入 |
| `ramOk` | Feed 的 RAM 模式测试标志 | `true`（通过）、`false`（失败） | When — 状态注入 |
| `canOk` | Feed 的 CAN 总线标志 | `true`（未 bus-off）、`false`（bus-off） | When — 状态注入 |
| `feedCount` | `Swc_Watchdog_Feed` 调用次数 | `1`（单次）、`3`（连续多次） | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `initialized` | `Wdg_Initialized`（getter） | 有效 Init 后 1；NULL/未 Init 为 0 |
| `feedCount` | `Wdg_FeedCount`（getter，成功喂狗计数） | = 成功 Feed 次数 |
| `dioFlipCount` | `Dio_FlipChannel` 调用次数（harness mock） | = 成功 Feed 次数 |
| `dioLastChannel` | 最后一次翻转的 DIO 通道（harness mock） | 配置的 `wdiDioChannel`（=6） |
| `feedResult` | 最后一次 Feed 返回值 | E_OK(0)/E_NOT_OK(1)；未调用为 -1 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: 初始化 — Swc_Watchdog_Init**：有效配置就绪 / NULL 配置守卫 /
>   未初始化守卫，共 3 场景。
> - **规则: 四条件喂狗门控 — Swc_Watchdog_Feed**：全条件满足喂狗 / 各单条件
>   失败拒绝 / 连续喂狗计数 / 失败后恢复，共 7 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/watchdog/setup` 存储）：设置前置喂狗
>   状态（如成功喂狗基线）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/watchdog` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`skipInit=false`、
> `initNull=false`、`loopComplete=true`、`canaryOk=true`、`ramOk=true`、
> `canOk=true`、`feedCount=1`）。

### 规则: 初始化 — Swc_Watchdog_Init

| 用例 | 阶段序列 | 期望 initialized | 期望 feedResult | 期望 dioFlipCount |
|---|---|---|---|---|
| init_valid_config_ready | P0: feedCount=0 | 1 | -1（未 Feed） | 0 |
| init_null_config_guard | P0: initNull=true, feedCount=1 | 0 | E_NOT_OK(1) | 0 |
| feed_uninitialized_guard | P0: skipInit=true, feedCount=1 | 0 | E_NOT_OK(1) | 0 |

### 规则: 四条件喂狗门控 — Swc_Watchdog_Feed

| 用例 | 阶段序列 | 期望 feedResult | 期望 feedCount / dioFlipCount | 期望 dioLastChannel |
|---|---|---|---|---|
| feed_all_conditions_met | P0: feedCount=1 | E_OK(0) | 1 / 1 | 6 |
| feed_loop_incomplete_blocked | P0: loopComplete=false | E_NOT_OK(1) | 0 / 0 | — |
| feed_canary_corrupted_blocked | P0: canaryOk=false | E_NOT_OK(1) | 0 / 0 | — |
| feed_ram_failed_blocked | P0: ramOk=false | E_NOT_OK(1) | 0 / 0 | — |
| feed_can_busoff_blocked | P0: canOk=false | E_NOT_OK(1) | 0 / 0 | — |
| feed_multiple_counts_accumulate | P0: feedCount=3 | E_OK(0) | 3 / 3 | 6 |
| feed_recovers_after_single_fail | 前置: feedCount=1（成功）; P0: loopComplete=false（拒绝）; P1: feedCount=1（成功） | E_OK(0) | 2 / 2 | 6 |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_valid_config_ready` | 有效配置初始化后内部状态就绪 |
> | `init_null_config_guard` | NULL 配置初始化使 SWC 未初始化 |
> | `feed_uninitialized_guard` | 未初始化时喂狗被拒绝 |
> | `feed_all_conditions_met` | 四个条件全部满足时喂狗并翻转 WDI |
> | `feed_loop_incomplete_blocked` | 主循环未完成时拒绝喂狗 |
> | `feed_canary_corrupted_blocked` | 栈金丝雀损坏时拒绝喂狗 |
> | `feed_ram_failed_blocked` | RAM 模式测试失败时拒绝喂狗 |
> | `feed_can_busoff_blocked` | CAN 总线 bus-off 时拒绝喂狗 |
> | `feed_multiple_counts_accumulate` | 连续喂狗次数累加且每次翻转 WDI |
> | `feed_recovers_after_single_fail` | 单条件失败后条件恢复可继续喂狗 |

## 代码路径覆盖

- `Swc_Watchdog_Init` 全部可执行行 ✅
  - `ConfigPtr == NULL` → 去初始化（`initialized=0` 断言）✅
  - 有效配置 → 指针保存 / FeedCount 清零 / Initialized=TRUE ✅
- `Swc_Watchdog_Feed` 全部可执行行 ✅
  - 未初始化守卫（`Wdg_Initialized != TRUE` → return E_NOT_OK）✅
  - 四条件门控每路 true 侧（单条件失败场景）与 false 侧（全满足场景）✅
  - WDI 翻转（`dioFlipCount` / `dioLastChannel` 断言）+ FeedCount++ ✅
- `Swc_Watchdog_GetInitialized` / `GetFeedCount`（UNIT_TEST 观测 getter）✅
  由 harness 输出读取，全部命中
- **不可达防御分支**：`Swc_Watchdog_Feed` 中 `Wdg_CfgPtr == NULL_PTR` 分支的
  true 侧（return E_NOT_OK）为防御性代码，公开 API 无法到达 —— `Wdg_CfgPtr`
  仅在 `Swc_Watchdog_Init` 中赋值：有效配置时与 `Wdg_Initialized=TRUE` 同时写入，
  NULL 配置时同时置 FALSE/NULL，`Wdg_Initialized==TRUE ⟹ Wdg_CfgPtr != NULL`，
  故 Feed 内该检查恒为 false（详见下方覆盖率实测）。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Watchdog.c.gcov.html` 实测（2026-08-16 全量套件 322 场景运行后，
含本 feature 10 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **93.5%**（43 / 46 行） |
| **分支覆盖** | **92.9%**（13 / 14 分支） |
| **函数覆盖** | **100%**（4 / 4 函数） |

覆盖到的函数：`Swc_Watchdog_Init`、`Swc_Watchdog_Feed`（生产 API），以及 2 个
`#ifdef UNIT_TEST` 观测 getter（`GetInitialized`、`GetFeedCount`）。

> 下表「实测命中」为完整套件（322 场景）运行后的累积值：10 次 harness 调用，
> 其中 1 次因 `skipInit` 跳过 Init、1 次因 `initNull` 走 NULL 配置分支；每次运行
> 因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。生产固件编译不定义
> `UNIT_TEST`，getter 相关行不计入交付固件的有效代码。

---

## 行覆盖分析（93.5%，43/46）

行覆盖反映**每一行是否被执行**。46 行中 43 行覆盖；唯一缺口是 `Swc_Watchdog_Feed`
中 `Wdg_CfgPtr == NULL_PTR` 防御分支的 3 行函数体（L83-85），该分支为不可达的
防御性代码（详见下文「无法覆盖的代码说明」）。

### 逐函数代码行覆盖映射

#### Swc_Watchdog_Init（L40-52）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L41 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init，除 skipInit 场景） | 9 |
| L42 | `if (ConfigPtr == NULL_PTR)` | true 侧：`init_null_config_guard`；false 侧：其余 Init 场景 | 9 |
| L43-46 | NULL 分支：`Wdg_Initialized=FALSE`、`Wdg_CfgPtr=NULL`、`return` | `init_null_config_guard`（initialized=0 断言） | 1 |
| L47 | NULL 分支结束 `}` | `init_null_config_guard` | 1 |
| L49 | `Wdg_CfgPtr = ConfigPtr` | 有效配置全部场景 | 8 |
| L50 | `Wdg_FeedCount = 0` | 有效配置全部场景（feedCount 初值断言） | 8 |
| L51-52 | `Wdg_Initialized = TRUE` + `}` | 有效配置全部场景（initialized=1 断言） | 8 |

#### Swc_Watchdog_Feed（L72-114）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L76 | 函数入口 `{` | 全部 Feed 调用场景 | 13 |
| L77 | `if (Wdg_Initialized != TRUE)` | true 侧：`init_null_config_guard`、`feed_uninitialized_guard`；false 侧：其余场景 | 13 |
| L78-80 | 未初始化守卫 `return E_NOT_OK` | `init_null_config_guard`、`feed_uninitialized_guard`（feedResult=1 断言） | 2 |
| L82 | `if (Wdg_CfgPtr == NULL_PTR)` | false 侧：全部已初始化 Feed（条件求值） | 11 |
| L88 | `if (loopComplete != TRUE)` | true 侧：`feed_loop_incomplete_blocked` + `feed_recovers_after_single_fail` P0；false 侧：其余 | 11 |
| L89-91 | 主循环未完成 `return E_NOT_OK` | `feed_loop_incomplete_blocked`（feedResult=1 断言） | 2 |
| L93 | `if (canaryOk != TRUE)` | true 侧：`feed_canary_corrupted_blocked`；false 侧：其余 | 9 |
| L94-96 | 金丝雀损坏 `return E_NOT_OK` | `feed_canary_corrupted_blocked`（feedResult=1 断言） | 1 |
| L98 | `if (ramOk != TRUE)` | true 侧：`feed_ram_failed_blocked`；false 侧：其余 | 8 |
| L99-101 | RAM 失败 `return E_NOT_OK` | `feed_ram_failed_blocked`（feedResult=1 断言） | 1 |
| L103 | `if (canOk != TRUE)` | true 侧：`feed_can_busoff_blocked`；false 侧：其余 | 7 |
| L104-106 | CAN bus-off `return E_NOT_OK` | `feed_can_busoff_blocked`（feedResult=1 断言） | 1 |
| L109 | `Dio_FlipChannel(wdiDioChannel)` | 全部成功喂狗场景（dioFlipCount 断言） | 6 |
| L111 | `Wdg_FeedCount++` | 全部成功喂狗场景（feedCount 断言） | 6 |
| L113 | `return E_OK` | 全部成功喂狗场景（feedResult=0 断言） | 6 |
| L114 | 函数结束 `}` | 全部 Feed 场景 | 7 |

#### UNIT_TEST 观测 getters（L124-132，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L125-127 | `Swc_Watchdog_GetInitialized` 返回 `Wdg_Initialized` | 全部场景（harness 输出 JSON 逐次调用） | 10 |
| L130-132 | `Swc_Watchdog_GetFeedCount` 返回 `Wdg_FeedCount` | 全部场景（harness 输出 JSON 逐次调用） | 10 |

> 常量/静态声明（L32-34 静态变量）为非执行行，不计入行覆盖。genhtml 的行统计
> 另含 3 个「带分支计数的条件行」：`L42`（`ConfigPtr == NULL_PTR`，命中 9）、
> `L77`（`Wdg_Initialized != TRUE`，命中 13）、`L82`（`Wdg_CfgPtr == NULL_PTR`，
> 命中 11）、`L88`/`L93`/`L98`/`L103`（四条件门控，命中 11/9/8/7）。其中
> `L42`/`L77`/`L88`/`L93`/`L98`/`L103` 两分支均覆盖，`L82` 仅 false 侧（详见
> 分支覆盖分析），故 43/46 行覆盖。

---

## 分支覆盖分析（92.9%，13/14）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `ConfigPtr == NULL_PTR` | L42 | ✅ 两侧 | `init_null_config_guard`（true）/ 有效配置场景（false） |
| `Wdg_Initialized != TRUE` | L77 | ✅ 两侧 | `feed_uninitialized_guard`、`init_null_config_guard`（true）/ 其余（false） |
| `Wdg_CfgPtr == NULL_PTR` | L82 | ⚠️ 仅 false 侧 | true 侧**不可达**（防御性代码，见「无法覆盖的代码说明」） |
| `loopComplete != TRUE` | L88 | ✅ 两侧 | `feed_loop_incomplete_blocked`（true）/ 其余（false） |
| `canaryOk != TRUE` | L93 | ✅ 两侧 | `feed_canary_corrupted_blocked`（true）/ 其余（false） |
| `ramOk != TRUE` | L98 | ✅ 两侧 | `feed_ram_failed_blocked`（true）/ 其余（false） |
| `canOk != TRUE` | L103 | ✅ 两侧 | `feed_can_busoff_blocked`（true）/ 其余（false） |

---

## 无法覆盖的代码说明

**`Swc_Watchdog_Feed` 中 `Wdg_CfgPtr == NULL_PTR` 守卫的 true 侧（L83-85，3 行）**

该守卫是**防御性代码**（fail-closed 原则），但通过公开 API **不可达**：

- `Wdg_CfgPtr` 仅在 `Swc_Watchdog_Init` 中写入，且与 `Wdg_Initialized` **同时**
  保持一致：
  - 有效配置：`Wdg_CfgPtr = ConfigPtr`（非 NULL）且 `Wdg_Initialized = TRUE`；
  - NULL 配置：`Wdg_CfgPtr = NULL_PTR` 且 `Wdg_Initialized = FALSE`。
- 因此不变式恒成立：`Wdg_Initialized == TRUE ⟹ Wdg_CfgPtr != NULL`。
- `Swc_Watchdog_Feed` 首先检查 `Wdg_Initialized != TRUE`（L77）即返回 E_NOT_OK，
  走到 L82 时必有 `Wdg_Initialized == TRUE`，从而 `Wdg_CfgPtr` 必非 NULL，
  该条件恒为 false。

结论：该分支是面向内存损坏/静态状态被意外篡改等物理故障的纵深防御，正常
软件流程下不会进入。端到端测试通过公开 API 无法（也不应）构造该状态；刻意
篡改静态内部状态来“覆盖”它反而会破坏测试的真实性。因此作为不可达防御分支
记录在案，属合理豁免。若未来引入允许独立修改两个内部状态的可测试接口，
可另行覆盖。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 93.5%（43/46） | 3 行（L83-85） | `Wdg_CfgPtr == NULL_PTR` 防御分支函数体，公开 API 不可达（不变式保证） |
| 分支 | 92.9%（13/14） | 1 个（L82 true 侧） | 同上，防御性不可达分支 |
| 函数 | 100%（4/4） | — | — |

