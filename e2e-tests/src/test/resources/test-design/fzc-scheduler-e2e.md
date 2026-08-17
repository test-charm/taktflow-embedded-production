# FZC 可运行实体调度表 (Swc_FzcScheduler) E2E 测试设计

## 被测功能

**FZC ASW 可运行实体配置表 SWC — 编译期静态装载 SWR-FZC-029 生产可运行实体表
（7 项，索引 0-6，含周期/优先级/WCET/ASIL），`Swc_FzcScheduler_Init` 置位初始化
标志，未初始化时 `Swc_FzcScheduler_GetTable` 返回 NULL（fail-closed），
`Swc_FzcScheduler_GetCount` 返回固定表项数 7，以及配置表数据正确性（安全任务
优先级高于 QM 任务、总 WCET 在 10ms 周期 80% 利用率上限内）。**

覆盖链路：

```text
测试 API 注入（skipInit / reinit）
  → Swc_FzcScheduler_Init()（SWR-FZC-029）：
       · 无条件置位 FzcSched_Initialized = TRUE（幂等，可重复 Init）
  → Swc_FzcScheduler_GetTable()（SWR-FZC-029）：
       · FzcSched_Initialized != TRUE → NULL_PTR（未初始化守卫，fail-closed）
       · 已初始化 → 返回内部 const 表 FzcSched_Table
  → Swc_FzcScheduler_GetCount()（SWR-FZC-029）：
       · 恒返回 FZC_SCHED_RUNNABLE_COUNT（7u，无守卫，编译期常量）
  → 观测（harness 输出）：initialized / tableMatches / runnableCount /
      runnables 读回表 / safetyPriorityOk / wcetWithinCycle / wcetTotalUs
```

与既有 ASW E2E（CVC `Swc_Scheduler`、FZC `Swc_FzcSafety` 等）一致，通过测试专用
API 在原生测试框架内执行真实的 `Swc_FzcScheduler.c` 生产代码。与 CVC `Swc_Scheduler`
不同，FZC 调度表是模块内部 `static const` 编译期常量（非外部注入配置），因此
**不存在** CVC 那类 NULL 配置 / 空 runnables / 零计数的 Init 守卫分支；本模块唯一
分支是 `GetTable` 的未初始化守卫。内部状态（初始化标志）完全通过公开 API
`GetTable`（NULL 或表指针）与 `GetCount` 观测，**无需新增观测 getter，生产代码
零改动**。

> **被测代码观测**：`FzcSched_Initialized` 为模块静态状态，通过
> `Swc_FzcScheduler_GetTable()`（返回表指针或 NULL）完全推断：
> `GetTable() != NULL ⟺ 已初始化`。harness 另将读回表与 SWR-FZC-029 期望表逐项
> 比较（名称/周期/优先级/WCET/ASIL），输出 `tableMatches`。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_FzcScheduler_Init(void)  │
└──────────────┬───────────────┘
               │
  FzcSched_Initialized = TRUE   （幂等，可重复调用）
```

```
┌────────────────────────────────┐
│ Swc_FzcScheduler_GetTable(void)│
└───────────────┬────────────────┘
                │
  FzcSched_Initialized != TRUE? ──Y──→ return NULL_PTR（未初始化守卫）
                │N
  return FzcSched_Table（内部 const 表）
```

```
┌────────────────────────────────┐
│ Swc_FzcScheduler_GetCount(void)│
└───────────────┬────────────────┘
                │
  return FZC_SCHED_RUNNABLE_COUNT（恒为 7u，无守卫）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_FzcScheduler_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `reinit` | 相位开始前再次调用 `Swc_FzcScheduler_Init()` | `false`、`true`（重复 Init 幂等） | When — 执行控制 |
| 阶段序列 | 多次 harness 相位（前置 + 刺激） | 单次、Init→Init（重复）、Init→skip（不可恢复，无 de-init API） | When — 执行控制 |

> 每个执行控制因子只有两个等价类（触发/不触发），无数值边界。与 CVC `Swc_Scheduler`
> 不同，FZC 调度器无配置注入参数，故不存在 tableIndex 等配置选择因子——表是编译期
> 常量，`GetTable` 读回即生产表本身。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `initialized` | `GetTable() != NULL`（即 `FzcSched_Initialized==TRUE`） | Init 后 1；skipInit 为 0 |
| `tableMatches` | 读回表与 SWR-FZC-029 期望表逐项一致（名称/周期/优先级/WCET/ASIL） | 已初始化后 1；未初始化 0 |
| `runnableCount` | `GetCount()` | 恒为 7（无论是否 Init） |
| `runnables[]` | 经 `GetTable()` 读回的可运行实体表 | 各表项 index/name/periodMs/priority/wcetUs/asilLevel |
| `safetyPriorityOk` | 读回表中安全任务（ASIL≥B）优先级均 ≥ HIGH 且 > QM 任务优先级 | 生产表 1 |
| `wcetWithinCycle` | 读回表总 WCET < 10ms 周期的 80% 上限 | 生产表 1 |
| `wcetTotalUs` | 读回表总 WCET（调试观测） | 生产表 2900 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化与未初始化守卫 — Swc_FzcScheduler_Init / GetTable**：有效
>   初始化就绪 / 未初始化守卫 / 重复 Init 幂等，共 3 场景。
> - **规则: 生产配置表数据正确性（SWR-FZC-029）**：读回表项一致性 / 安全优先级
>   高于 QM / WCET 利用率上限，共 3 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/scheduler/setup` 存储）：设置前置调度器
>   状态。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/fzc/scheduler` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`skipInit=false`、
> `reinit=false`）。

### 规则: 初始化与未初始化守卫 — Swc_FzcScheduler_Init / GetTable

| 用例 | 阶段序列 | 期望 initialized | 期望 tableMatches | 期望 runnableCount |
|---|---|---|---|---|
| `init_production_ready` | P0: （默认 Init） | 1 | 1 | 7 |
| `uninitialized_table_null` | P0: skipInit=true | 0 | 0 | 7 |
| `reinit_idempotent` | P0: reinit=true | 1 | 1 | 7 |

### 规则: 生产配置表数据正确性（SWR-FZC-029）

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `table_content_swr_fzc_029` | P0: （默认 Init） | runnables[0]={SteeringMonitor,10,High,800,ASIL_D}；runnables[1]={BrakeMonitor,10,High,600,ASIL_D}；runnables[2]={LidarMonitor,10,High,500,ASIL_C}；runnables[3]={CanReceive,10,High,400,ASIL_D}；runnables[4]={HeartbeatTx,50,Med,200,ASIL_B}；runnables[5]={WatchdogFeed,100,High,100,ASIL_D}；runnables[6]={BuzzerDriver,10,Low,300,QM} |
| `safety_priority_over_qm` | P0: （默认 Init） | runnables[0..5].priority ≥ HIGH(3)（安全任务）；runnables[6].priority=LOW(1)（QM）；safetyPriorityOk=1 |
| `wcet_under_util_cap` | P0: （默认 Init） | wcetTotalUs=2900；wcetWithinCycle=1（2900us < 8000us，即 29% < 80%） |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_production_ready` | 生产配置表初始化后配置就绪 |
> | `uninitialized_table_null` | 未初始化时查询表为空但计数为 7 |
> | `reinit_idempotent` | 重复初始化保持就绪 |
> | `table_content_swr_fzc_029` | 读回的生产表项与 SWR-FZC-029 一致 |
> | `safety_priority_over_qm` | 安全任务优先级高于 QM 任务优先级 |
> | `wcet_under_util_cap` | 总 WCET 未超 10ms 周期 80% 上限 |

## 代码路径覆盖

- `Swc_FzcScheduler_Init` 全部可执行行 ✅
  - `FzcSched_Initialized = TRUE`（`initialized=1`、`tableMatches=1` 断言）✅
- `Swc_FzcScheduler_GetTable` 全部可执行行 ✅
  - 未初始化 → `NULL_PTR`（`initialized=0` 断言）✅
  - 已初始化 → 返回表指针（`tableMatches` / `runnables[]` 断言）✅
- `Swc_FzcScheduler_GetCount` 全部可执行行 ✅
  - 恒返回 7（已初始化与未初始化场景均断言 `runnableCount=7`）✅
- harness 数据检查（`safetyPriorityOk` / `wcetWithinCycle` / `wcetTotalUs`，
  仅测试编译）✅ 由生产表数据断言全部命中。
- **不可达防御分支**：本模块不存在。`GetCount` 无守卫（编译期常量返回）；
  `GetTable` 唯一分支为未初始化守卫，由 `uninitialized_table_null` /
  `init_production_ready` 两侧覆盖。与 CVC `Swc_Scheduler` 的
  `GetRunnableCount` 防御性 `CfgPtr == NULL_PTR` 守卫不同，本模块无此豁免项。

> 被测功能无 `#ifdef UNIT_TEST` 观测 getter 新增（`GetTable` / `GetCount`
> 为既有公开 API），生产代码零改动。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_FzcScheduler.c.gcov.html` 实测（2026-08-17 全量套件运行后，含本 feature
6 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（12 / 12 行） |
| **分支覆盖** | **100%**（2 / 2 分支） |
| **函数覆盖** | **100%**（3 / 3 函数） |

覆盖到的函数：`Swc_FzcScheduler_Init`、`Swc_FzcScheduler_GetTable`、
`Swc_FzcScheduler_GetCount`。

> 下表「实测命中」为**本容器生命周期内**（重启后：1 次仅本 feature 运行 + 2 次
> 全量 440 场景套件 + 2 次 curl 冒烟调用）的累积值：共触发 **20 次 harness 调用**
> （其中 4 次 `skipInit` 跳过 Init、1 次 `reinit` 额外 Init，故 `Swc_FzcScheduler_Init`
> 命中 19 次），`GetTable` 命中 20 次（true 侧 4 次 / false 侧 16 次），
> `GetCount` 命中 20 次。每次运行因容器重启会重新累积，具体数字可能不同，但
> 覆盖关系不变（行 12/12、分支 2/2、函数 3/3 均为 100%）。

---

## 行覆盖分析（100%，12/12）

行覆盖反映**每一行是否被执行**。12 行全部覆盖，无行级缺口。genhtml 统计的
12 行即三个函数的全部可执行行（L117-119、L126-132、L139-141）。

### 逐函数代码行覆盖映射

#### Swc_FzcScheduler_Init（L116-119）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L117 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init，除 skipInit 场景） | 19 |
| L118 | `FzcSched_Initialized = TRUE` | 全部已初始化场景（initialized=1、tableMatches=1 断言） | 19 |
| L119 | 函数结束 `}` | 全部已初始化场景 | 19 |

#### Swc_FzcScheduler_GetTable（L125-132）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L126 | 函数入口 `{` | 全部场景（harness 输出 JSON 逐次调用） | 20 |
| L127 | `if (FzcSched_Initialized != TRUE)` | true 侧：`uninitialized_table_null`（skipInit=true）；false 侧：其余已初始化场景 | 20 |
| L128 | `return NULL_PTR` | `uninitialized_table_null`（initialized=0 断言） | 4 |
| L129 | 空分支结束 `}` | `uninitialized_table_null` | 4 |
| L131 | `return FzcSched_Table` | 全部已初始化场景（tableMatches=1、runnables[] 断言） | 16 |
| L132 | 函数结束 `}` | 全部已初始化场景 | 20 |

#### Swc_FzcScheduler_GetCount（L138-141）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L139 | 函数入口 `{` | 全部场景（harness 输出 JSON 的 runnableCount 字段逐次调用） | 20 |
| L140 | `return FZC_SCHED_RUNNABLE_COUNT` | 全部场景（runnableCount=7 断言，含未初始化场景） | 20 |
| L141 | 函数结束 `}` | 全部场景 | 20 |

> 常量/静态声明（L47-104 静态 const 表、L110 静态变量、L37-41 宏）为非执行行，
> 不计入行覆盖。genhtml 的行统计中 L127 为「带分支计数的条件行」，两侧均覆盖
> （详见分支覆盖分析），故 12/12 行覆盖。

---

## 分支覆盖分析（100%，2/2）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `FzcSched_Initialized != TRUE` | L127 | ✅ 两侧 | true 侧（return NULL）：`uninitialized_table_null`（4 次）；false 侧（return 表）：全部已初始化场景（16 次） |

> 本模块唯一条件分支（`GetTable` 的未初始化守卫）两侧均已覆盖，无无法覆盖的分支。

---

## 无法覆盖的代码说明

**无豁免项。** 与 CVC `Swc_Scheduler`（`GetRunnableCount` 中不可达的防御性
`Sched_CfgPtr == NULL_PTR` 守卫）不同，`Swc_FzcScheduler.c` 不存在通过公开 API
不可达的代码路径：

1. `Swc_FzcScheduler_Init` 无参数、无守卫，恒置位初始化标志，全部行可执行。
2. `Swc_FzcScheduler_GetTable` 唯一分支是未初始化守卫，由
   `uninitialized_table_null`（true 侧）与 `init_production_ready` 等（false 侧）
   覆盖。
3. `Swc_FzcScheduler_GetCount` 无守卫，恒返回编译期常量 `FZC_SCHED_RUNNABLE_COUNT`，
   全部场景（含未初始化）均执行。

因此行 / 分支 / 函数覆盖均为 100%。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（12/12） | 0 行 | — |
| 分支 | 100%（2/2） | 0 个 | — |
| 函数 | 100%（3/3） | — | — |
