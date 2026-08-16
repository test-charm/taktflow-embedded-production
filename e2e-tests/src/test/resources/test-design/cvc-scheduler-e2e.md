# CVC 可运行实体调度表 (Swc_Scheduler) E2E 测试设计

## 被测功能

**CVC ASW 可运行实体配置表 SWC — 装载 SWR-CVC-032 生产可运行实体表（8 项：
ID 0-7，含周期/优先级/WCET/ASIL），NULL 配置 / 空 runnables / 零计数三种守卫
拒绝初始化，未初始化时 GetConfig 返回 NULL、GetRunnableCount 返回 0，重复 Init
替换配置表，以及配置表数据正确性（安全任务优先级高于 QM 任务、总 WCET 在最短
周期内）。**

覆盖链路：

```text
测试 API 注入（skipInit / initNull / nullRunnables / zeroCount / tableIndex）
  → Swc_Scheduler_Init(ConfigPtr)（SWR-CVC-032）：
       · ConfigPtr == NULL          → 拒绝（Sched_Initialized=FALSE, CfgPtr=NULL）
       · ConfigPtr->runnables == NULL → 拒绝（同上）
       · ConfigPtr->runnableCount == 0 → 拒绝（同上）
       · 有效配置 → Sched_CfgPtr=ConfigPtr, Sched_Initialized=TRUE
  → Swc_Scheduler_GetConfig()（SWR-CVC-032）：
       · Sched_Initialized != TRUE → NULL_PTR
       · 已初始化 → 返回 Sched_CfgPtr（所传配置指针）
  → Swc_Scheduler_GetRunnableCount()（SWR-CVC-032）：
       · Sched_Initialized != TRUE → 0u
       · Sched_CfgPtr == NULL_PTR  → 0u（防御性守卫，公开 API 不可达）
       · 已初始化 → 返回 Sched_CfgPtr->runnableCount
  → 观测（harness 输出）：initialized / configMatches / runnableCount /
      runnables 读回表 / safetyPriorityOk / wcetWithinCycle
```

与既有 ASW E2E（`Swc_SelfTest`、`Swc_Watchdog` 等）一致，通过测试专用 API 在
原生测试框架内执行真实的 `Swc_Scheduler.c` 生产代码。配置守卫与重复初始化由
harness 脚本注入；由于 `Swc_Scheduler` 为纯配置存储 SWC，无硬件依赖，内部状态
（初始化标志 / 配置指针）完全通过公开 API `GetConfig` / `GetRunnableCount`
观测，**无需新增观测 getter，生产代码零改动**。

> **被测代码观测**：`Sched_Initialized`、`Sched_CfgPtr` 均为模块静态状态，但
> 通过 `Swc_Scheduler_GetConfig`（返回指针或 NULL）与 `Swc_Scheduler_GetRunnableCount`
> 可完全推断：`GetConfig()!=NULL ⟺ 已初始化`。harness 另通过指针相等比较
> （`GetConfig()==&cfg`）输出 `configMatches`，验证 Init 精确保存了所传配置。

## 被测代码流程图

```
┌───────────────────────────────┐
│ Swc_Scheduler_Init(ConfigPtr) │
└───────────────┬───────────────┘
                │
  ConfigPtr == NULL_PTR? ──Y──→ Sched_Initialized=FALSE, CfgPtr=NULL, return
                │N
  runnables == NULL_PTR? ──Y──→ Sched_Initialized=FALSE, CfgPtr=NULL, return
                │N
  runnableCount == 0u? ────Y──→ Sched_Initialized=FALSE, CfgPtr=NULL, return
                │N
  Sched_CfgPtr = ConfigPtr
  Sched_Initialized = TRUE
```

```
┌────────────────────────────────┐
│ Swc_Scheduler_GetConfig(void)  │
└───────────────┬────────────────┘
                │
  Sched_Initialized != TRUE? ──Y──→ return NULL_PTR
                │N
  return Sched_CfgPtr
```

```
┌────────────────────────────────────┐
│ Swc_Scheduler_GetRunnableCount()   │
└───────────────┬────────────────────┘
                │
  Sched_Initialized != TRUE? ──Y──→ return 0u
                │N
  Sched_CfgPtr == NULL_PTR? ──Y──→ return 0u（防御性，公开 API 不可达）
                │N
  return Sched_CfgPtr->runnableCount
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Scheduler_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `initNull` | 是否调用 `Swc_Scheduler_Init(NULL_PTR)` | `false`（有效配置）、`true`（NULL 配置守卫） | When — 执行控制 |
| `nullRunnables` | 是否用 `runnables==NULL` 的配置 Init | `false`、`true`（空 runnables 守卫） | When — 执行控制 |
| `zeroCount` | 是否用 `runnableCount==0` 的配置 Init | `false`、`true`（零计数守卫） | When — 执行控制 |
| `tableIndex` | 有效 Init 使用的配置表 | `0`=生产 8 项（默认）、`1`=最小 1 项、`2`=最大 16 项（SCHED_MAX_RUNNABLES 边界） | When — 配置选择 |
| 阶段序列 | 多次 `Swc_Scheduler_Init` 调用（多 phase） | 单次、有效→有效（替换）、有效→NULL（清除）、NULL→有效（恢复） | When — 执行控制 |

> 每个守卫因子只有两个等价类（触发/不触发），无数值边界。`tableIndex` 为离散
> 选择：`1` 是计数下边界（最小表），`2` 是 `SCHED_MAX_RUNNABLES`（16）上边界，
> `0` 是生产配置（覆盖单位测试的全部断言）。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `initialized` | `GetConfig()!=NULL`（即 `Sched_Initialized==TRUE`） | 有效 Init 后 1；NULL/守卫/未 Init 为 0 |
| `configMatches` | `GetConfig()==最后一次 Init 所传配置指针` | 有效 Init 后 1；守卫拒绝后 SWC 未初始化（GetConfig 返回 NULL），本因子仅在有效配置场景断言 |
| `runnableCount` | `GetRunnableCount()` | 有效 Init 后 = 表项数（8/1/16）；未 Init 为 0 |
| `runnables[]` | 经 `GetConfig()` 读回的可运行实体表 | 各表项 runnableId/periodMs/priority/wcetUs/asilLevel |
| `safetyPriorityOk` | 读回表中安全任务（ASIL≥B）优先级均 > QM 任务优先级 | 生产表 1 |
| `wcetWithinCycle` | 读回表总 WCET < 最短周期 | 生产表 1 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化 — Swc_Scheduler_Init**：有效配置就绪 / 最小 / 最大表 / NULL /
>   空 runnables / 零计数 / 未初始化 / 失败后恢复，共 8 场景。
> - **规则: 重复初始化 — 配置替换**：有效→有效替换 / 有效→NULL 清除，共 2 场景。
> - **规则: 生产配置表数据正确性（SWR-CVC-032）**：读回表项一致性 / 安全优先级
>   高于 QM，共 2 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/scheduler/setup` 存储）：设置前置调度器
>   状态。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/scheduler` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`skipInit=false`、
> `initNull=false`、`nullRunnables=false`、`zeroCount=false`、`tableIndex=0`）。

### 规则: 初始化 — Swc_Scheduler_Init

| 用例 | 阶段序列 | 期望 initialized | 期望 configMatches | 期望 runnableCount |
|---|---|---|---|---|
| `init_production_ready` | P0: tableIndex=0 | 1 | 1 | 8 |
| `init_min_count` | P0: tableIndex=1 | 1 | 1 | 1 |
| `init_max_count` | P0: tableIndex=2 | 1 | 1 | 16 |
| `init_null_config_rejected` | P0: initNull=true | 0 | — | 0 |
| `init_null_runnables_rejected` | P0: nullRunnables=true | 0 | — | 0 |
| `init_zero_count_rejected` | P0: zeroCount=true | 0 | — | 0 |
| `init_uninitialized_guard` | P0: skipInit=true | 0 | — | 0 |
| `init_recovers_after_reject` | P0: initNull=true; P1: tableIndex=0 | 1 | 1 | 8 |

### 规则: 重复初始化 — 配置替换

| 用例 | 阶段序列 | 期望 initialized | 期望 configMatches | 期望 runnableCount |
|---|---|---|---|---|
| `reinit_replaces_table` | P0: tableIndex=0; P1: tableIndex=1 | 1 | 1 | 1 |
| `reinit_null_clears_table` | P0: tableIndex=0; P1: initNull=true | 0 | — | 0 |

### 规则: 生产配置表数据正确性（SWR-CVC-032）

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `table_content_swr032` | P0: tableIndex=0 | runnables[0]={0,10,10,200,ASIL_D}；runnables[3]={3,50,8,150,ASIL_B}；runnables[4]={4,200,3,500,QM}；runnables[7]={7,10,10,200,ASIL_D}；safetyPriorityOk=1；wcetWithinCycle=1 |
| `safety_priority_over_qm` | P0: tableIndex=0 | runnables[2].priority=11（EStop）；runnables[3].priority=8（Heartbeat，安全任务最低优先级）；runnables[4].priority=3（Dashboard，QM 最高优先级）；safetyPriorityOk=1 |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_production_ready` | 生产配置表初始化后配置就绪 |
> | `init_min_count` | 最小表（1 项）初始化后计数为 1 |
> | `init_max_count` | 最大表（16 项）初始化后计数为 16 |
> | `init_null_config_rejected` | NULL 配置初始化被拒绝 |
> | `init_null_runnables_rejected` | runnables 为空指针时初始化被拒绝 |
> | `init_zero_count_rejected` | runnableCount 为 0 时初始化被拒绝 |
> | `init_uninitialized_guard` | 未初始化时查询配置与计数为空 |
> | `init_recovers_after_reject` | 失败初始化后有效配置可恢复 |
> | `reinit_replaces_table` | 重复初始化替换为最小表 |
> | `reinit_null_clears_table` | NULL 重新初始化清除已存储配置 |
> | `table_content_swr032` | 读回的生产表项与 SWR-CVC-032 一致 |
> | `safety_priority_over_qm` | 安全任务优先级高于 QM 任务优先级 |

## 代码路径覆盖

- `Swc_Scheduler_Init` 全部可执行行 ✅
  - `ConfigPtr == NULL_PTR` → 拒绝（`initialized=0`、`runnableCount=0` 断言）✅
  - `ConfigPtr->runnables == NULL_PTR` → 拒绝 ✅
  - `ConfigPtr->runnableCount == 0u` → 拒绝 ✅
  - 有效配置 → 指针保存（`configMatches=1`）/ `Initialized=TRUE` ✅
- `Swc_Scheduler_GetConfig` 全部可执行行 ✅
  - 未初始化 → `NULL_PTR`（`initialized=0` 断言）✅
  - 已初始化 → 返回配置指针（`configMatches` / `runnables[]` 断言）✅
- `Swc_Scheduler_GetRunnableCount` 全部可执行行 ✅
  - 未初始化守卫 → `0u` ✅
  - 已初始化 → 返回 `runnableCount`（8/1/16 边界断言）✅
- **不可达防御分支**：`GetRunnableCount` 中 `Sched_CfgPtr == NULL_PTR` 分支的
  true 侧（return 0u）为防御性代码，公开 API 无法到达 —— `Sched_CfgPtr` 仅在
  `Swc_Scheduler_Init` 中赋值：有效配置时与 `Sched_Initialized=TRUE` 同时写入，
  任一守卫拒绝时同时置 FALSE/NULL，`Sched_Initialized==TRUE ⟹ Sched_CfgPtr != NULL`，
  故该检查恒为 false（详见下方覆盖率实测）。
- harness 数据检查（`safetyPriorityOk` / `wcetWithinCycle`，仅测试编译）✅
  由生产表数据断言全部命中。

> 被测功能无 `#ifdef UNIT_TEST` 观测 getter 新增（`GetConfig` / `GetRunnableCount`
> 为既有公开 API），生产代码零改动。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Scheduler.c.gcov.html` 实测（2026-08-16 全量套件运行后，含本 feature
12 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **92.5%**（37 / 40 行） |
| **分支覆盖** | **91.7%**（11 / 12 分支） |
| **函数覆盖** | **100%**（3 / 3 函数） |

覆盖到的函数：`Swc_Scheduler_Init`、`Swc_Scheduler_GetConfig`、
`Swc_Scheduler_GetRunnableCount`。

> 下表「实测命中」为完整套件（344 场景）运行后的累积值（本容器运行期间多次执行
> feature 的累积：12 次 harness 调用、44 次 Init 执行）；每次运行因容器重启
> 会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（92.5%，37/40）

行覆盖反映**每一行是否被执行**。40 行中 37 行覆盖；唯一缺口是
`Swc_Scheduler_GetRunnableCount` 中 `Sched_CfgPtr == NULL_PTR` 防御分支的 3 行
函数体（L96-98），该分支为不可达的防御性代码（详见下文「无法覆盖的代码说明」）。

### 逐函数代码行覆盖映射

#### Swc_Scheduler_Init（L43-68）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L44 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init，除 skipInit 场景） | 44 |
| L45 | `if (ConfigPtr == NULL_PTR)` | true 侧：`init_null_config_rejected`、`init_recovers_after_reject` P0、`reinit_null_clears_table` P1；false 侧：其余 Init 场景 | 44 |
| L47-49 | NULL 分支：`Sched_Initialized=FALSE`、`CfgPtr=NULL`、`return` | `init_null_config_rejected`（initialized=0 断言） | 10 |
| L52 | `if (ConfigPtr->runnables == NULL_PTR)` | true 侧：`init_null_runnables_rejected`；false 侧：其余 | 34 |
| L54-56 | 空 runnables 分支：拒绝 + return | `init_null_runnables_rejected`（initialized=0 断言） | 3 |
| L59 | `if (ConfigPtr->runnableCount == 0u)` | true 侧：`init_zero_count_rejected`；false 侧：其余 | 31 |
| L61-63 | 零计数分支：拒绝 + return | `init_zero_count_rejected`（initialized=0 断言） | 3 |
| L66 | `Sched_CfgPtr = ConfigPtr` | 有效配置全部场景（configMatches=1 断言） | 28 |
| L67 | `Sched_Initialized = TRUE` | 有效配置全部场景（initialized=1 断言） | 28 |
| L68 | 函数结束 `}` | 有效配置全部场景 | 28 |

#### Swc_Scheduler_GetConfig（L74-82）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L75 | 函数入口 `{` | 全部场景（harness 输出 JSON 逐次调用） | 38 |
| L76 | `if (Sched_Initialized != TRUE)` | true 侧：未初始化/被拒绝场景（`init_null_config_rejected`、`init_null_runnables_rejected`、`init_zero_count_rejected`、`init_uninitialized_guard`、`reinit_null_clears_table`）；false 侧：有效配置场景 | 38 |
| L78 | `return NULL_PTR` | 未初始化/被拒绝场景（initialized=0 断言） | 16 |
| L81 | `return Sched_CfgPtr` | 有效配置场景（configMatches=1、runnables[] 断言） | 22 |
| L82 | 函数结束 `}` | 全部场景 | 38 |

#### Swc_Scheduler_GetRunnableCount（L88-101）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L89 | 函数入口 `{` | 全部场景 | 38 |
| L90 | `if (Sched_Initialized != TRUE)` | true 侧：未初始化/被拒绝场景；false 侧：有效配置场景 | 38 |
| L92 | `return 0u` | 未初始化/被拒绝场景（runnableCount=0 断言） | 16 |
| L95 | `if (Sched_CfgPtr == NULL_PTR)` | false 侧：全部有效配置场景（条件求值） | 22 |
| L100 | `return Sched_CfgPtr->runnableCount` | 有效配置场景（runnableCount 8/1/16 边界断言） | 22 |
| L101 | 函数结束 `}` | 有效配置场景 | 22 |

> 常量/静态声明（L36-37 静态变量）为非执行行，不计入行覆盖。genhtml 的行统计
> 中 L45/L52/L59/L76/L90/L95 为「带分支计数的条件行」，其中 L45/L52/L59/L76/L90
> 两分支均覆盖，L95 仅 false 侧（详见分支覆盖分析），故 37/40 行覆盖。

---

## 分支覆盖分析（91.7%，11/12）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `ConfigPtr == NULL_PTR` | L45 | ✅ 两侧 | `init_null_config_rejected`、`reinit_null_clears_table`（true）/ 有效配置场景（false） |
| `ConfigPtr->runnables == NULL_PTR` | L52 | ✅ 两侧 | `init_null_runnables_rejected`（true）/ 有效配置场景（false） |
| `ConfigPtr->runnableCount == 0u` | L59 | ✅ 两侧 | `init_zero_count_rejected`（true）/ 有效配置场景（false） |
| `Sched_Initialized != TRUE`（GetConfig） | L76 | ✅ 两侧 | 未初始化/被拒绝场景（true）/ 有效配置场景（false） |
| `Sched_Initialized != TRUE`（GetRunnableCount） | L90 | ✅ 两侧 | 未初始化/被拒绝场景（true）/ 有效配置场景（false） |
| `Sched_CfgPtr == NULL_PTR` | L95 | ⚠️ 仅 false 侧 | true 侧**不可达**（防御性代码，见「无法覆盖的代码说明」） |

---

## 无法覆盖的代码说明

**`Swc_Scheduler_GetRunnableCount` 中 `Sched_CfgPtr == NULL_PTR` 守卫的 true 侧
（L96-98，3 行）**

该守卫是**防御性代码**（fail-closed 原则），但通过公开 API **不可达**：

- `Sched_CfgPtr` 仅在 `Swc_Scheduler_Init` 中写入，且与 `Sched_Initialized` **同时**
  保持一致：
  - 有效配置：`Sched_CfgPtr = ConfigPtr`（非 NULL）且 `Sched_Initialized = TRUE`；
  - 任一守卫拒绝（NULL / 空 runnables / 零计数）：`Sched_CfgPtr = NULL_PTR` 且
    `Sched_Initialized = FALSE`。
- 因此不变式恒成立：`Sched_Initialized == TRUE ⟹ Sched_CfgPtr != NULL`。
- `Swc_Scheduler_GetRunnableCount` 首先检查 `Sched_Initialized != TRUE`（L90）即
  返回 0u，走到 L95 时必有 `Sched_Initialized == TRUE`，从而 `Sched_CfgPtr` 必非
  NULL，该条件恒为 false。

结论：该分支是面向内存损坏/静态状态被意外篡改等物理故障的纵深防御，正常
软件流程下不会进入。端到端测试通过公开 API 无法（也不应）构造该状态；刻意
篡改静态内部状态来"覆盖"它反而会破坏测试的真实性。因此作为不可达防御分支
记录在案，属合理豁免（与 `Swc_Watchdog` 的 `Wdg_CfgPtr == NULL_PTR` 守卫同理）。
若未来引入允许独立修改两个内部状态的可测试接口，可另行覆盖。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 92.5%（37/40） | 3 行（L96-98） | `Sched_CfgPtr == NULL_PTR` 防御分支函数体，公开 API 不可达（不变式保证） |
| 分支 | 91.7%（11/12） | 1 个（L95 true 侧） | 同上，防御性不可达分支 |
| 函数 | 100%（3/3） | — | — |
