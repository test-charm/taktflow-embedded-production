# RZC 可运行实体调度表 (Swc_RzcScheduler) E2E 测试设计

## 被测功能

**RZC ASW 可运行实体配置表 SWC — 编译期静态装载 SWR-RZC-028 生产可运行实体表
（8 项，索引 0-7，含周期/优先级/WCET 函数指针），`Swc_RzcScheduler_Init` 复位
8 个 elapsed 计数器并置位初始化标志，`Swc_RzcScheduler_Tick` 按每实体周期累加
elapsed 并分派到期可运行实体（CurrentMonitor 1ms / Motor 10ms / Encoder 10ms /
CanReceive 10ms / Temp 100ms / Battery 100ms / HeartbeatTx 50ms / Watchdog 100ms），
未初始化时 Tick 直接返回（fail-closed），`Swc_RzcScheduler_GetTable` 恒返回内部
静态表（无守卫，与 FZC 的 NULL 守卫不同），`Swc_RzcScheduler_GetUtilPct` 计算
总 WCET 利用率（生产表 10%，低于 80% 上限）。**

覆盖链路：

```text
测试 API 注入（skipInit / reinit / ticks）
  → Swc_RzcScheduler_Init()（SWR-RZC-028）：
       · 复位 RzcSched_Elapsed[0..7] = 0
       · 置位 RzcSched_Initialized = TRUE（幂等，可重复 Init）
  → Swc_RzcScheduler_Tick()（SWR-RZC-028）：
       · RzcSched_Initialized != TRUE → 直接返回（未初始化守卫，fail-closed）
       · 已初始化 → 对每个可运行实体：Elapsed[i]++；若 ≥ period_ms 则
         复位 Elapsed[i] 并调用 func()（若 func != NULL_PTR）
  → Swc_RzcScheduler_GetTable()（SWR-RZC-028）：
       · 恒返回内部 const 表 RzcSched_Table（无初始化守卫，与 FZC 不同）
  → Swc_RzcScheduler_GetUtilPct()（SWR-RZC-028）：
       · 求和 (wcet_us * 10) / period_ms，转百分比返回（生产表 10）
  → 观测（harness 输出）：initialized / tableMatches / runnableCount /
      runnables 读回表 / priorityOrdered / utilPct / utilUnderMax / ticks /
      dispatchTotal / tickCounts[8] / 各可运行实体计数器
```

与既有 ASW E2E（CVC `Swc_Scheduler`、FZC `Swc_FzcScheduler` 等）一致，通过测试专用
API 在原生测试框架内执行真实的 `Swc_RzcScheduler.c` 生产代码。与 CVC `Swc_Scheduler`
不同，RZC 调度表是模块内部 `static const` 编译期常量（含真实函数指针），因此不存在
CVC 那类 NULL 配置 / 空 runnables / 零计数的 Init 守卫分支；与 FZC `Swc_FzcScheduler`
不同，本模块的 `GetTable` **没有**未初始化守卫（恒返回表指针），未初始化行为由
`Tick` 的守卫分支体现（不调度任何可运行实体）。

> **被测代码观测**：`RzcSched_Initialized` 为模块静态状态，通过 UNIT_TEST 保护的
> 观测 getter `Swc_RzcScheduler_GetInitialized()` 直接读取（仅测试编译，交付固件
> 不含）；`RzcSched_Elapsed[]` 内部计数通过 8 个 mock 可运行入口函数的调用计数器
> 观测（Tick 分派可逐实体计数）。harness 将读回表与 SWR-RZC-028 期望表逐项比较
> （周期/优先级/WCET），输出 `tableMatches`。

## 被测代码流程图

```
┌────────────────────────────────┐
│ Swc_RzcScheduler_Init(void)    │
└───────────────┬────────────────┘
                │
  for i in 0..7: RzcSched_Elapsed[i] = 0    （8 次，幂等）
                │
  RzcSched_Initialized = TRUE
```

```
┌────────────────────────────────┐
│ Swc_RzcScheduler_Tick(void)    │
└───────────────┬────────────────┘
                │
  RzcSched_Initialized != TRUE? ──Y──→ return（未初始化守卫，fail-closed）
                │N
  for i in 0..7:                  （按优先级排序的分派顺序）
    │
    RzcSched_Elapsed[i]++
    │
    Elapsed[i] >= period_ms? ──N──→ 下一实体（未到期，不调度）
    │Y
    RzcSched_Elapsed[i] = 0
    │
    func != NULL_PTR? ──N──→ 下一实体（防御性守卫，生产表恒非空）
    │Y
    func()                      （调用可运行实体入口）
```

```
┌────────────────────────────────┐
│ Swc_RzcScheduler_GetTable(void)│
└───────────────┬────────────────┘
                │
  return &RzcSched_Table[0]（恒返回，无未初始化守卫）
```

```
┌────────────────────────────────┐
│ Swc_RzcScheduler_GetUtilPct(void)│
└───────────────┬────────────────┘
                │
  total_util = 0
  for i in 0..7:
    │
    period_ms > 0u? ──N──→ 跳过（防御性守卫，生产表恒非零）
    │Y
    total_util += (wcet_us * 10) / period_ms   （0.01% 单位）
  return (uint8)(total_util / 100u)            （生产表 → 10%）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_RzcScheduler_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `reinit` | 相位开始前再次调用 `Swc_RzcScheduler_Init()` | `false`、`true`（重复 Init 幂等 + 复位 elapsed） | When — 执行控制 |
| `ticks` | `Swc_RzcScheduler_Tick` 调用次数 | 等价类：`0`（不调度）、`1`（CurrentMonitor 1ms 边界）、`10`（10ms 实体边界）、`100`（100ms 实体边界 / 50ms 心跳 2 次）；边界：`0/1/10/50/100` | When — 执行控制 |

> 每个执行控制因子只有少数等价类（触发/不触发 / 边界节拍数）。`ticks` 取值对齐
> 调度表周期：1ms（CurrentMonitor）、10ms（Motor/Encoder/CanReceive）、50ms
> （HeartbeatTx）、100ms（Temp/Battery/Watchdog）。与 CVC `Swc_Scheduler` 不同，
> RZC 调度器无配置注入参数，故不存在 tableIndex 等配置选择因子——表是编译期常量，
> `GetTable` 读回即生产表本身。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `initialized` | `Swc_RzcScheduler_GetInitialized()`（UNIT_TEST getter） | Init 后 1；skipInit 为 0 |
| `tableMatches` | 读回表与 SWR-RZC-028 期望表逐项一致（周期/优先级/WCET） | 恒为 1（GetTable 无守卫） |
| `runnableCount` | 表项数（编译期常量 `RZC_SCHED_RUNNABLE_COUNT`） | 恒为 8 |
| `runnables[]` | 经 `GetTable()` 读回的可运行实体表 | 各表项 index/periodMs/priority/wcetUs |
| `priorityOrdered` | 读回表优先级非递减（安全任务 priority 1/2 高于 QM 3） | 生产表 1 |
| `utilPct` | `Swc_RzcScheduler_GetUtilPct()` | 生产表 10（10.25% → 取整 10） |
| `utilUnderMax` | `utilPct < 80`（`RZC_SCHED_MAX_UTIL_PCT`） | 生产表 1 |
| `ticks` | 本次运行累计 `Swc_RzcScheduler_Tick` 调用次数 | 随 phases 的 ticks 求和 |
| `dispatchTotal` | 8 个 mock 可运行入口的累计调用总数 | 随节拍数增长（1→1、10→13、100→135） |
| `tickCounts[]` / `currentMonitor` 等 | 各可运行实体被 Tick 分派的调用次数 | 按周期精确计数（见用例） |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化与未初始化守卫 — Swc_RzcScheduler_Init / GetTable**：有效
>   初始化就绪 / 未初始化 Tick 不调度 + 表仍可读 / 重复 Init 幂等并复位 elapsed，
>   共 3 场景。
> - **规则: 生产配置表数据正确性（SWR-RZC-028）**：读回表项一致性 / 安全优先级
>   高于 QM / WCET 利用率上限，共 3 场景。
> - **规则: Tick 周期调度（elapsed 计数器与分派）**：单次 Tick 仅 CurrentMonitor /
>   10ms 实体第 10 tick 触发 / 100ms 完整周期全实体精确触发 / 前置 phase 后 reinit
>   复位 elapsed，共 4 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/scheduler/setup` 存储）：设置前置调度器
>   状态。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/rzc/scheduler` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`skipInit=false`、
> `reinit=false`、`ticks=0`）。

### 规则: 初始化与未初始化守卫 — Swc_RzcScheduler_Init / GetTable

| 用例 | 阶段序列 | 期望 initialized | 期望 tableMatches | 期望 runnableCount |
|---|---|---|---|---|
| `init_production_ready` | P0: （默认 Init） | 1 | 1 | 8 |
| `uninitialized_tick_no_dispatch` | P0: skipInit=true, ticks=10 | 0 | 1（GetTable 无守卫） | 8 |
| `reinit_idempotent_reset` | P0: reinit=true, ticks=1 | 1 | 1 | 8 |

### 规则: 生产配置表数据正确性（SWR-RZC-028）

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `table_content_swr_rzc_028` | P0: （默认 Init） | runnables[0]={1,1,50}；[1]={10,2,200}；[2]={10,2,100}；[3]={10,2,150}；[4]={100,3,300}；[5]={100,3,200}；[6]={50,3,100}；[7]={100,3,50} |
| `safety_priority_over_qm` | P0: （默认 Init） | priorityOrdered=1；runnables[0..3].priority=1/2（安全任务）；runnables[4..7].priority=3（QM） |
| `util_under_max` | P0: （默认 Init） | utilPct=10；utilUnderMax=1（10% < 80%） |

### 规则: Tick 周期调度（elapsed 计数器与分派）

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `tick_1ms_current_monitor` | P0: ticks=1 | dispatchTotal=1；currentMonitor=1，其余 7 个实体均为 0 |
| `tick_10ms_periodic_dispatch` | P0: ticks=10 | dispatchTotal=13；currentMonitor=10；motor=1；encoder=1；comReceive=1；temp/battery/heartbeat/wdgm=0 |
| `tick_100ms_full_cycle` | P0: ticks=100 | dispatchTotal=135；currentMonitor=100；motor/encoder/comReceive=10；heartbeat=2；temp/battery/wdgm=1 |
| `reinit_resets_elapsed_after_phase` | P0（Given）: ticks=10；P1（刺激）: reinit=true, ticks=1 | ticks=11；currentMonitor=11（10+1）；motor/encoder/comReceive=1（10ms 实体仅前置 10 tick 时触发，reinit 复位后 1 tick 未到期）；heartbeat/wdgm=0 |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_production_ready` | 生产配置表初始化后配置就绪 |
> | `uninitialized_tick_no_dispatch` | 未初始化时 Tick 不调度且表仍可读 |
> | `reinit_idempotent_reset` | 重复初始化保持就绪且复位 elapsed |
> | `table_content_swr_rzc_028` | 读回的生产表项与 SWR-RZC-028 一致 |
> | `safety_priority_over_qm` | 安全任务优先级高于 QM 任务优先级 |
> | `util_under_max` | 总 WCET 利用率未超 80% 上限 |
> | `tick_1ms_current_monitor` | 单次 Tick 仅 CurrentMonitor（1ms）触发 |
> | `tick_10ms_periodic_dispatch` | 10ms 周期实体在第 10 个 Tick 各触发一次 |
> | `tick_100ms_full_cycle` | 100ms 完整周期内所有可运行实体按周期触发 |
> | `reinit_resets_elapsed_after_phase` | 前置 phase 后再 reinit 复位 elapsed 计数 |

## 代码路径覆盖

- `Swc_RzcScheduler_Init` 全部可执行行 ✅
  - 循环复位 8 个 elapsed（`RzcSched_Elapsed[i]=0`，行 91-93）✅
  - `RzcSched_Initialized = TRUE`（`initialized=1` 断言，行 95）✅
- `Swc_RzcScheduler_Tick` 全部可执行行 ✅
  - 未初始化守卫 → `return`（`uninitialized_tick_no_dispatch`，行 107-109）✅
  - 已初始化：elapsed 累加 + 周期判定（行 113-116，true/false 两侧）✅
  - 到期复位 + 函数指针分派（行 117-124，`func()` 调用）✅
- `Swc_RzcScheduler_GetTable` 全部可执行行 ✅
  - 恒返回表指针（所有场景 `tableMatches=1`、`runnables[]` 断言）✅
- `Swc_RzcScheduler_GetUtilPct` 全部可执行行 ✅
  - `period_ms > 0` true 侧（生产表 8 项全为正，`utilPct=10` 断言）✅
  - 累加与换算（行 154-160，`utilPct=10`）✅
- UNIT_TEST 观测 getter `Swc_RzcScheduler_GetInitialized` ✅（所有场景输出
  `initialized` 字段调用）。
- **不可达防御分支（2 个）**：`func != NULL_PTR` 的 false 侧（行 120）与
  `period_ms > 0u` 的 false 侧（行 152）。生产表为编译期 `static const` 常量，
  8 个函数指针全非空、8 个周期全为正，通过公开 API 无法构造空指针/零周期表项，
  故两个防御分支的 false 侧**无法通过公开 API 覆盖**（详见「无法覆盖的代码说明」）。

> 被测功能新增 1 个 UNIT_TEST 观测 getter（`Swc_RzcScheduler_GetInitialized`，
> 仅测试编译，交付固件不含），与 FZC CanMonitor / RZC Heartbeat 先例一致。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_RzcScheduler.c.gcov.html` 实测（2026-08-18 全量 570 场景套件运行后，含本
feature 10 场景 + 3 次 curl 冒烟调用）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（47 / 47 行） |
| **分支覆盖** | **85.7%**（12 / 14 分支） |
| **函数覆盖** | **100%**（5 / 5 函数） |

覆盖到的函数：`Swc_RzcScheduler_Init`、`Swc_RzcScheduler_Tick`、
`Swc_RzcScheduler_GetTable`、`Swc_RzcScheduler_GetUtilPct`、
`Swc_RzcScheduler_GetInitialized`（UNIT_TEST getter）。

> 下表「实测命中」为**本容器生命周期内**（重启后：3 次 curl 冒烟调用 + 1 次仅本
> feature 运行 + 1 次全量 570 场景套件）的累积值：共触发 **23 次 harness 调用**
> （其中 3 次 `skipInit` 跳过全局 Init、4 次 `reinit` 额外 Init——场景 3/10 各
> 一次，故 `Swc_RzcScheduler_Init` 命中 24 次、`GetTable`/`GetUtilPct`/
> `GetInitialized` 各命中 23 次），`Tick` 命中 286 次（其中 30 次未初始化守卫
> 直接返回，256 次完整分派），`func()` 分派 341 次。每次运行因容器重启会重新
> 累积，具体数字可能不同，但覆盖关系不变（行 47/47、函数 5/5 均为 100%，
> 分支 12/14）。

---

## 行覆盖分析（100%，47/47）

行覆盖反映**每一行是否被执行**。47 行全部覆盖，无行级缺口。genhtml 统计的
47 行即四个 API + 一个 getter 的全部可执行行。

### 逐函数代码行覆盖映射

#### Swc_RzcScheduler_Init（L86-96）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L87 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init，除 skipInit 场景） | 24 |
| L88 | `uint8 i;` | 全部已初始化场景 | 24 |
| L90 | `for (i = 0u; i < 8; i++)`（循环头） | 全部已初始化场景（分支 +/− 两侧，见分支分析） | 24 |
| L91 | 循环体 `{` | 全部已初始化场景（每次 Init 8 次迭代） | 192 |
| L92 | `RzcSched_Elapsed[i] = 0u;` | 全部已初始化场景（elapsed 复位） | 192 |
| L93 | 循环体 `}` | 全部已初始化场景 | 192 |
| L95 | `RzcSched_Initialized = TRUE;` | 全部已初始化场景（`initialized=1` 断言） | 24 |
| L96 | 函数结束 `}` | 全部已初始化场景 | 24 |

#### Swc_RzcScheduler_Tick（L102-126）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L103 | 函数入口 `{` | 全部 tick 场景（含 skipInit+ticks） | 286 |
| L104 | `uint8 i;` | 全部 tick 场景 | 286 |
| L106 | `if (RzcSched_Initialized != TRUE)` | true 侧：`uninitialized_tick_no_dispatch`（skipInit+ticks=10）；false 侧：其余已初始化 tick 场景 | 286 |
| L107 | 守卫体 `{` | `uninitialized_tick_no_dispatch` | 30 |
| L108 | `return;` | `uninitialized_tick_no_dispatch`（`dispatchTotal=0` 断言） | 30 |
| L109 | 守卫体 `}` | `uninitialized_tick_no_dispatch` | 30 |
| L112 | `for (i = 0u; i < 8; i++)`（循环头） | 全部已初始化 tick 场景（分支 +/− 两侧） | 256 |
| L113 | 循环体 `{` | 全部已初始化 tick 场景 | 2048 |
| L114 | `RzcSched_Elapsed[i]++;` | 全部已初始化 tick 场景（elapsed 累加） | 2048 |
| L116 | `if (Elapsed[i] >= period_ms)` | true 侧：到期实体（tick_1ms/10ms/100ms 场景）；false 侧：未到期实体（同批 tick 中未达周期者） | 2048 |
| L117 | 到期体 `{` | tick_1ms/10ms/100ms 场景（`currentMonitor/motor/...` 计数断言） | 341 |
| L118 | `RzcSched_Elapsed[i] = 0u;` | tick 场景（周期复位） | 341 |
| L120 | `if (func != NULL_PTR)` | true 侧：全部 8 个可运行实体分派（`dispatchTotal` 断言）；**false 侧不可达**（见「无法覆盖的代码说明」） | 341 |
| L121 | 分派体 `{` | tick 场景（`func()` 调用） | 341 |
| L122 | `RzcSched_Table[i].func();` | tick 场景（8 个 mock 入口计数器递增） | 341 |
| L123 | 分派体 `}` | tick 场景 | 341 |
| L124 | 到期体 `}` | tick 场景 | 341 |
| L125 | 循环体 `}` | 全部已初始化 tick 场景 | 2048 |
| L126 | 函数结束 `}` | 全部已初始化 tick 场景 | 256 |

#### Swc_RzcScheduler_GetTable（L132-135）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L133 | 函数入口 `{` | 全部场景（harness 输出 JSON 逐次调用） | 23 |
| L134 | `return &RzcSched_Table[0];` | 全部场景（`tableMatches=1`、`runnables[]` 断言，含 skipInit 场景） | 23 |
| L135 | 函数结束 `}` | 全部场景 | 23 |

#### Swc_RzcScheduler_GetUtilPct（L141-161）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L142 | 函数入口 `{` | 全部场景（harness 输出 utilPct 字段逐次调用） | 23 |
| L143 | `uint32 total_util;` | 全部场景 | 23 |
| L144 | `uint8 i;` | 全部场景 | 23 |
| L146 | `total_util = 0u;` | 全部场景 | 23 |
| L148 | `for (i = 0u; i < 8; i++)`（循环头） | 全部场景（分支 +/− 两侧） | 23 |
| L149 | 循环体 `{` | 全部场景（每次调用 8 次迭代） | 184 |
| L152 | `if (period_ms > 0u)` | true 侧：生产表 8 项全为正（`utilPct=10` 断言）；**false 侧不可达**（见「无法覆盖的代码说明」） | 184 |
| L153 | 分支体 `{` | 全部场景（8 项均进入） | 184 |
| L154 | `total_util += (wcet_us * 10)` | 全部场景（利用率累加） | 184 |
| L155 | `/ period_ms;` | 全部场景 | 184 |
| L156 | 分支体 `}` | 全部场景 | 184 |
| L157 | 循环体 `}` | 全部场景 | 184 |
| L160 | `return (uint8)(total_util / 100u);` | 全部场景（`utilPct=10` 断言） | 23 |
| L161 | 函数结束 `}` | 全部场景 | 23 |

#### Swc_RzcScheduler_GetInitialized（L171-174，UNIT_TEST only）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L172 | 函数入口 `{` | 全部场景（harness 输出 initialized 字段逐次调用） | 23 |
| L173 | `return RzcSched_Initialized;` | 全部场景（`initialized=1/0` 断言） | 23 |
| L174 | 函数结束 `}` | 全部场景 | 23 |

> 常量/静态声明（L60-70 静态 const 表、L77/L80 静态变量）为非执行行，不计入行
> 覆盖。genhtml 的行统计中 L90/L112/L148 为「带分支计数的循环条件行」、L106/L116/
> L120/L152 为「带分支计数的条件行」，两侧覆盖情况详见分支覆盖分析，故 47/47 行覆盖。

---

## 分支覆盖分析（85.7%，12/14）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `i < 8` 循环条件（Init） | L90 | ✅ 两侧 | true 侧 192 次（8 次迭代 × 24 次 Init）；false 侧 24 次（循环退出） |
| `RzcSched_Initialized != TRUE`（Tick 守卫） | L106 | ✅ 两侧 | true 侧 30 次（`uninitialized_tick_no_dispatch`，skipInit+ticks=10 × 3 轮运行）；false 侧 256 次（已初始化 tick） |
| `i < 8` 循环条件（Tick） | L112 | ✅ 两侧 | true 侧 2048 次（8 次迭代 × 256 次已初始化 tick）；false 侧 256 次 |
| `Elapsed[i] >= period_ms`（到期判定） | L116 | ✅ 两侧 | true 侧 341 次（到期分派）；false 侧 1707 次（未到期，如 10ms 实体前 9 个 tick） |
| `func != NULL_PTR`（分派守卫） | L120 | ⚠️ 仅 true 侧 | true 侧 341 次（全部 8 个可运行实体分派）；**false 侧不可达**（生产表编译期常量，8 个函数指针全非空） |
| `i < 8` 循环条件（GetUtilPct） | L148 | ✅ 两侧 | true 侧 184 次（8 次迭代 × 23 次调用）；false 侧 23 次 |
| `period_ms > 0u`（利用率守卫） | L152 | ⚠️ 仅 true 侧 | true 侧 184 次（生产表 8 项周期全为正）；**false 侧不可达**（生产表编译期常量） |

> 12 个分支两侧均已覆盖；2 个分支（L120 false、L152 false）为防御性守卫的
> 不可达侧，无法通过公开 API 构造，详见下节。

---

## 无法覆盖的代码说明

**2 个豁免项**，均为编译期静态表驱动的防御性分支：

1. **`if (RzcSched_Table[i].func != NULL_PTR)` 的 false 侧（L120）**：
   `RzcSched_Table` 是模块内部 `static const` 编译期常量，8 个表项的 `func`
   均为真实函数指针（`Swc_CurrentMonitor_MainFunction`、
   `Swc_Motor_MainFunction`、`Swc_Encoder_MainFunction`、`Swc_RzcCom_Receive`、
   `Swc_TempMonitor_MainFunction`、`Swc_Battery_MainFunction`、
   `Swc_Heartbeat_MainFunction`、`WdgM_MainFunction`），全非空。该分支是防御性
   代码（防止表项被破坏/链接错误导致空指针调用）；通过公开 API 无法把任一表项
   func 置空（无配置注入、无表修改接口），因此 false 侧在原生 harness 下不可达。
   表损坏场景超出 SWC 单元职责（由编译器/链接器保证），不在 E2E 覆盖范围内。

2. **`if (RzcSched_Table[i].period_ms > 0u)` 的 false 侧（L152）**：
   生产表 8 个周期值（1/10/10/10/100/100/50/100）全为正，编译期确定。该分支是
   防御性代码（防止除零）；通过公开 API 无法构造 period_ms==0 的表项，因此
   false 侧不可达。除零防护是编译期静态表的固有不变量，不属于运行时行为。

这两处与 CVC `Swc_Scheduler` 的 `GetRunnableCount` NULL 配置守卫性质相同——
均为对「不可能通过公开 API 到达」状态的防御。**若要 100% 覆盖，需引入测试专用
表注入接口（如 UNIT_TEST 保护的 `RzcSched_Table` 覆盖钩子），但会改变模块
「编译期只读表」的不可变性设计，故不采用**；以文档化豁免处理。行 / 函数覆盖
不受影响（仍 100%），分支覆盖 12/14 = 85.7%。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（47/47） | 0 行 | — |
| 分支 | 85.7%（12/14） | 2 个 | L120 `func != NULL` false 侧（生产表函数指针全非空）、L152 `period_ms > 0` false 侧（生产表周期全为正）——编译期静态表防御分支，公开 API 不可达 |
| 函数 | 100%（5/5） | — | — |
