# RZC 心跳 (Swc_Heartbeat) E2E 测试设计

## 被测功能

**RZC ASW 心跳 SWC — 50ms TX 边界调度（存活计数器递增/15 回绕 + 车辆状态/故障位掩码发布 + CAN 故障抑制 TX + ECU ID 写入）**

覆盖链路：

```text
RTE 车辆状态信号（RZC_SIG_VEHICLE_STATE，= RZC_SIG_VEHICLE_STATE_MODE =187）
RTE 故障掩码信号（RZC_SIG_FAULT_MASK，= RZC_SIG_VEHICLE_STATE_FAULT_MASK =186）
  → Swc_Heartbeat_MainFunction（10ms 周期）
  → 5 周期（50ms）TX 边界：
       · 读取 vehicle_state（默认 RZC_STATE_INIT=0）
       · 读取 fault_mask（默认 0）
       · CAN 故障位（bit3=0x08，RZC_FAULT_CAN）置位
         **且** vehicle_state == RZC_STATE_SAFE_STOP(4)
         → 抑制 TX（不写任何信号）
       · Rte_Write(RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE，=135，
                   vehicle_state & 0x0F)        → Com TX 自动拉取
       · Rte_Write(RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS，=134，
                   fault_mask & 0x0F)
       · Rte_Write(RZC_SIG_HEARTBEAT_ALIVE，=130，alive 计数)
       · alive_counter++（>15 → 回绕 0）

Swc_Heartbeat_Init()
  · Hb_CycleCounter/Hb_AliveCounter 清零
  · Hb_Initialized = TRUE
  · Rte_Write(RZC_SIG_RZC_HEARTBEAT_ECU_ID，=133，RZC_ECU_ID=0x03)
```

与既有 ASW E2E（CVC `Swc_Heartbeat`、FZC `Swc_Heartbeat`、FZC `Swc_FzcCom`）一致，
通过测试专用 API 在原生测试框架内执行真实的
`firmware/ecu/rzc/src/Swc_Heartbeat.c` 生产代码。RZC 心跳仅保留 TX 调度与
信号发布（与 CVC 心跳相比**无** RX 指示 / 通信状态复位 / WdgM 检查点，实现
更精简）。

> **与 FZC 心跳的差异**：FZC 的 bus-off 抑制位为 bit8（0x0100）且仅判断
> `fault_mask` 单个条件；RZC 使用 bit3（`RZC_FAULT_CAN=0x08`）且要求
> **CAN 故障位置位 AND 车辆处于 SAFE_STOP** 双条件同时成立才抑制 TX
> （`L109-112`）。E2E 用例对该与门两侧的 false 侧分别覆盖
> （`can_fault_without_safe_stop_no_suppress` /
> `safe_stop_without_can_fault_no_suppress`）。

> **被测代码观测**：`Hb_AliveCounter`、`Hb_CycleCounter`、`Hb_Initialized`
> 均为模块静态状态，无法从外部直接读取。为支持 E2E 断言，在
> `Swc_Heartbeat.c/.h` 增加了 **`#ifdef UNIT_TEST` 保护**的观测 getter
> （`Swc_Heartbeat_GetAliveCounter` / `GetCycleCounter` / `GetInitialized`）。
> 生产固件构建（STM32/TMS570/POSIX target）不定义 `UNIT_TEST`，这些访问器不
> 进入交付固件；仅测试 harness 编译时生效。TX 输出信号（operatingMode /
> faultStatus / alive / ecuId）经 harness 的 mock RTE 信号表直接观测，无需
> 额外 getter。

## 被测代码流程图

```
                     ┌──────────────────────┐
                     │  Swc_Heartbeat_Init   │
                     │  (cycle=0, alive=0,   │
                     │   initialized=TRUE,   │
                     │   Rte_Write(ECU_ID))  │
                     └──────────┬───────────┘
                                │
                     ┌──────────▼───────────┐
                     │   MainFunction        │
                     │   (每 10ms 周期)      │
                     └──────────┬───────────┘
                                │
   Step1: initialized != TRUE？ ──Y──→ return（未初始化空转）
                                │N
   Step2: Hb_CycleCounter++
                                │
   Step3: Hb_CycleCounter < 5？ ──Y──→ return（未到 50ms 边界）
                                │N
   Step4: Hb_CycleCounter = 0（周期计数器复位）
                                │
   Step5: Rte_Read(RZC_SIG_VEHICLE_STATE)  → vehicle_state
          Rte_Read(RZC_SIG_FAULT_MASK)     → fault_mask
                                │
   Step6: (fault_mask & 0x08≠0) 且 ──N──→ 正常 TX（Step7）
          (vehicle_state==SAFE_STOP)？ │
                                │Y
                                └──→ return（抑制 TX）
                                │
   Step7: Rte_Write(OPERATING_MODE, vehicle_state & 0x0F)
          Rte_Write(FAULT_STATUS,   fault_mask   & 0x0F)
          Rte_Write(ALIVE,          Hb_AliveCounter)
          Hb_AliveCounter++
                                │
   Step8: Hb_AliveCounter > 15？ ──Y──→ Hb_AliveCounter = 0（回绕）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Heartbeat_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`4`（<5 无 TX，边界）、`5`（恰 5 周期，边界）、`10`（2×TX）、`80`（16×TX，回绕） | When — 执行控制 |
| `vehicleState` | RTE `RZC_SIG_VEHICLE_STATE` 读取值 | `1`（RUN）、`2`（DEGRADED）、`4`（SAFE_STOP，抑制条件）、`0x1F`（低半字节掩码边界） | When — 状态注入 |
| `faultMask` | RTE `RZC_SIG_FAULT_MASK` 读取值 | `0`（无故障）、`0x01`（过流）、`0x31`（低半字节掩码边界）、`0x08`（CAN 故障位，抑制条件） | When — 状态注入 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `aliveCounter` | TX 存活计数器（getter） | TX 次数 mod 16；回绕后为 0 |
| `cycleCounter` | TX 周期计数器（getter） | 边界后为 0；边界前为已累计周期 |
| `initialized` | 初始化标志（getter） | Init 后 1；skipInit 后 0 |
| `ecuId` | RTE `RZC_SIG_RZC_HEARTBEAT_ECU_ID`（=133） | Init 后 `RZC_ECU_ID`（3） |
| `operatingMode` | RTE `RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE`（=135） | = vehicleState & 0x0F |
| `faultStatus` | RTE `RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS`（=134） | = faultMask & 0x0F |
| `alive` | RTE `RZC_SIG_HEARTBEAT_ALIVE`（=130） | 每次 TX 写入的 alive 值（递增前） |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: 初始化 — Swc_Heartbeat_Init**：Init 默认值 + ECU ID 写入，共 1 场景。
> - **规则: TX 调度 — Swc_Heartbeat_MainFunction**：未初始化守卫 / TX 边界
>   时序 / 存活计数器回绕 / 车辆状态与故障发布，共 9 场景。
> - **规则: CAN 总线关闭抑制 — fault_mask bus-off**：CAN 故障位与 SAFE_STOP
>   双条件抑制 / 抑制条件各单侧 false / 恢复，共 5 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/heartbeat/setup` 存储）：设置前置心跳
>   状态（如 bus-off 基线或车辆状态基线）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/rzc/heartbeat` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`cycles=1`、
> `vehicleState=RUN`、`faultMask=0`、`skipInit=false`）。

### 规则: 初始化 — Swc_Heartbeat_Init

| 用例 | 阶段序列 | 期望 initialized | 期望 ecuId | 期望 aliveCounter | 期望 cycleCounter |
|---|---|---|---|---|---|
| init_defaults_ecu_id | P0: cycles=1 | 1 | 3 | 0 | 1 |

### 规则: TX 调度 — Swc_Heartbeat_MainFunction

| 用例 | 阶段序列 | 期望 aliveCounter | 期望 cycleCounter | 期望 operatingMode | 期望 faultStatus | 期望 alive |
|---|---|---|---|---|---|---|
| uninitialized_main_noop | P0: cycles=10, skipInit=true | 0 | 0 | 0 | 0 | 0 |
| no_tx_below_boundary | P0: cycles=4 | 0 | 4 | 0 | 0 | 0 |
| tx_at_exact_boundary | P0: cycles=5 | 1 | 0 | 1（RUN） | 0 | 0 |
| tx_every_5_cycles | P0: cycles=10 | 2 | 0 | 1（RUN） | 0 | 1 |
| alive_counter_wraps | P0: cycles=80 | 0（第 16 次 TX 回绕） | 0 | 1（RUN） | 0 | 15 |
| operating_mode_tracks_state | P0: cycles=5, vehicleState=2 | 1 | 0 | 2（DEGRADED） | 0 | 0 |
| operating_mode_low_nibble_mask | P0: cycles=5, vehicleState=0x1F | 1 | 0 | 0x0F | 0 | 0 |
| fault_status_tracks_mask | P0: cycles=5, faultMask=0x01 | 1 | 0 | 1（RUN） | 1 | 0 |
| fault_status_low_nibble_mask | P0: cycles=5, faultMask=0x31 | 1 | 0 | 1（RUN） | 1 | 0 |

### 规则: CAN 总线关闭抑制 — fault_mask bus-off

| 用例 | 阶段序列 | 期望 aliveCounter | 期望 cycleCounter | 期望 operatingMode | 期望 faultStatus |
|---|---|---|---|---|---|
| can_fault_safe_stop_suppresses_tx | P0: cycles=10, faultMask=0x08, vehicleState=4 | 0 | 0 | 0 | 0 |
| can_fault_without_safe_stop_no_suppress | P0: cycles=5, faultMask=0x08, vehicleState=1 | 1 | 0 | 1（RUN） | 0x08 |
| safe_stop_without_can_fault_no_suppress | P0: cycles=5, faultMask=0, vehicleState=4 | 1 | 0 | 4（SAFE_STOP） | 0 |
| can_fault_cleared_recovers | 前置: cycles=5, faultMask=0x08, vehicleState=4; P0: cycles=5, faultMask=0, vehicleState=1 | 1 | 0 | 1（RUN） | 0 |
| multi_phase_state_change | 前置: cycles=5, vehicleState=1; P0: cycles=5, vehicleState=4 | 2 | 0 | 4（SAFE_STOP） | 0 |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_defaults_ecu_id` | 初始化后写入 RZC ECU ID 且计数器清零 |
> | `uninitialized_main_noop` | 未初始化时主函数不动作 |
> | `no_tx_below_boundary` | 初始化后 4 周期内未到 TX 边界不发送 |
> | `tx_at_exact_boundary` | 恰好在 5 周期 (50ms) 边界发送心跳 |
> | `tx_every_5_cycles` | 每 5 周期发送一次 (10 周期两次) |
> | `alive_counter_wraps` | 存活计数器在第 16 次发送时从 15 回绕到 0 |
> | `operating_mode_tracks_state` | TX 边界将车辆状态写入心跳 OperatingMode |
> | `operating_mode_low_nibble_mask` | OperatingMode 只取车辆状态低 4 位 |
> | `fault_status_tracks_mask` | TX 边界将故障掩码写入心跳 FaultStatus |
> | `fault_status_low_nibble_mask` | FaultStatus 只取故障掩码低 4 位 |
> | `can_fault_safe_stop_suppresses_tx` | CAN 故障且车辆 SAFE_STOP 时抑制心跳 TX |
> | `can_fault_without_safe_stop_no_suppress` | CAN 故障但非 SAFE_STOP 时不抑制 TX |
> | `safe_stop_without_can_fault_no_suppress` | SAFE_STOP 但无 CAN 故障时不抑制 TX |
> | `can_fault_cleared_recovers` | 总线故障清除后心跳 TX 恢复 |
> | `multi_phase_state_change` | 车辆状态变化在后续周期透传到 OperatingMode |

## 代码路径覆盖

- `Swc_Heartbeat_Init` 全部可执行行 ✅
- `Swc_Heartbeat_MainFunction` 全部可执行行 ✅
  - 未初始化守卫（`initialized != TRUE` → return）✅
  - `Hb_CycleCounter++` 与 `Hb_CycleCounter < HB_PERIOD_CYCLES` 两侧
    （<5 无 TX / =5 TX）✅
  - TX 边界：Rte_Read 两路 / 抑制双条件两侧 / Rte_Write 三路 / alive 递增与回绕 ✅
- UNIT_TEST 观测 getters（仅测试编译）✅ 由 harness 输出读取，全部命中

---

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`firmware/ecu/rzc/src/Swc_Heartbeat.c.gcov.html` 实测（2026-08-17 全量套件
511 场景运行后，含本 feature 15 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（43 / 43 行） |
| **分支覆盖** | **100%**（10 / 10 分支） |
| **函数覆盖** | **100%**（5 / 5 函数） |

覆盖到的函数：`Swc_Heartbeat_Init`、`Swc_Heartbeat_MainFunction`，以及 3 个
`#ifdef UNIT_TEST` 观测 getter（`GetAliveCounter`、`GetCycleCounter`、
`GetInitialized`）。实测命中（`func.html`，2026-08-17 最终验证运行后）：

| 函数 | 实测命中 |
|---|---:|
| `Swc_Heartbeat_MainFunction` | 635 |
| `Swc_Heartbeat_Init` | 48 |
| `Swc_Heartbeat_GetAliveCounter` / `GetCycleCounter` / `GetInitialized` | 各 52 |

> 命中计数为容器生命周期内**累积值**（含本 feature 15 场景在单测运行与全量
> 套件中的 harness 调用、以及覆盖报告生成前的历次运行），每次容器重启后
> 重新累积，具体数字可能不同，但覆盖关系不变。本 feature 15 场景共触发
> **15 次 harness 调用**（其中 1 次 `skipInit` 跳过 Init），`MainFunction`
> 合计进入 **170 次**（各场景 cycles 之和：1+10+4+5+10+80+5+5+5+5+10+5+5+
> 10+10=170）。生产固件编译不定义 `UNIT_TEST`，getter 相关行不计入交付固件。

---

## 行覆盖分析（100%，43/43）

行覆盖反映**每一行是否被执行**。43 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_Heartbeat_Init（L68-76）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L69 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init） | 48 |
| L70 | `Hb_CycleCounter = 0u` | `init_defaults_ecu_id`（cycleCounter=0 断言）及全部已初始化场景 | 48 |
| L71 | `Hb_AliveCounter = 0u` | `init_defaults_ecu_id`（aliveCounter=0 断言）及全部已初始化场景 | 48 |
| L72 | `Hb_Initialized = TRUE` | `init_defaults_ecu_id`（initialized=1 断言）及全部已初始化场景 | 48 |
| L75 | `Rte_Write(ECU_ID, RZC_ECU_ID)` | `init_defaults_ecu_id`（ecuId=3 断言）及全部已初始化场景 | 48 |
| L76 | 函数结束 `}` | 全部已初始化场景 | 48 |

#### Swc_Heartbeat_MainFunction（L82-124）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L83 | 函数入口 `{` | 全部场景（每周期进入） | 635 |
| L84-85 | `uint32 vehicle_state; fault_mask;` | 全部场景 | 635 |
| L87 | `if (Hb_Initialized != TRUE)` 守卫 | true 侧：`uninitialized_main_noop`；false 侧：全部已初始化周期 | 635 |
| L88-89 | `return; }` | `uninitialized_main_noop`（skipInit=true，未初始化空转） | 40 |
| L92 | `Hb_CycleCounter++` | 全部已初始化场景 | 595 |
| L94 | `if (Hb_CycleCounter < HB_PERIOD_CYCLES)` | true 侧（<5 无 TX）：`init_defaults_ecu_id`、`no_tx_below_boundary` 等；false 侧（=5 TX）：TX 场景 | 595 |
| L95-96 | `return; }` | `no_tx_below_boundary`、`init_defaults_ecu_id`（未到边界） | 479 |
| L99 | `Hb_CycleCounter = 0u` | 全部 TX 边界场景（cycleCounter=0 断言） | 116 |
| L102-103 | `vehicle_state = RZC_STATE_INIT; Rte_Read(...)` | 全部 TX 边界场景（读到注入的 vehicleState） | 116 |
| L105-106 | `fault_mask = 0u; Rte_Read(...)` | 全部 TX 边界场景（读到注入的 faultMask） | 116 |
| L109 | `if ((fault_mask & RZC_FAULT_CAN) != 0u)` 第一条件 | true 侧（CAN 故障位置位）：`can_fault_safe_stop_suppresses_tx`、`can_fault_without_safe_stop_no_suppress`、`can_fault_cleared_recovers` 前置；false 侧：其余 TX 边界 | 116 |
| L110 | `if (vehicle_state == RZC_STATE_SAFE_STOP)` 第二条件 | true 侧（SAFE_STOP）：`can_fault_safe_stop_suppresses_tx`、`can_fault_cleared_recovers` 前置；false 侧（非 SAFE_STOP）：`can_fault_without_safe_stop_no_suppress` | 116 |
| L111-112 | `return; }` | CAN 故障 + SAFE_STOP 场景（TX 抑制，alive/opMode/faultStatus 不写） | 12 |
| L115 | `Rte_Write(OPERATING_MODE, vehicle_state & 0x0F)` | 正常 TX 场景（operatingMode 断言） | 104 |
| L116 | `Rte_Write(FAULT_STATUS, fault_mask & 0x0F)` | 正常 TX 场景（faultStatus 断言） | 104 |
| L117 | `Rte_Write(ALIVE, Hb_AliveCounter)` | 正常 TX 场景（alive 断言） | 104 |
| L120 | `Hb_AliveCounter++` | 正常 TX 场景 | 104 |
| L121 | `if (Hb_AliveCounter > RZC_HB_ALIVE_MAX)` | true 侧：`alive_counter_wraps`（第 16 次 TX 回绕）；false 侧：其余 TX 场景 | 104 |
| L122-123 | `Hb_AliveCounter = 0u; }` | `alive_counter_wraps`（回绕后 aliveCounter=0 断言） | 4 |
| L124 | 函数结束 `}` | 全部 TX 边界场景 | 104 |

#### UNIT_TEST 观测 getters（L133-148，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L134-137 | `GetAliveCounter` 返回静态 alive 计数 | 全部场景（harness 输出 JSON 逐次调用） | 52 |
| L139-142 | `GetCycleCounter` 返回静态周期计数 | 全部场景 | 52 |
| L144-147 | `GetInitialized` 返回初始化标志 | 全部场景 | 52 |

> 常量/静态声明（L39-44 `HB_PERIOD_CYCLES`/`_Static_assert`、L51-52 字节偏移
> 宏、L58-60 静态变量）为非执行行，不计入行覆盖；`_Static_assert` 为编译期
> 检查，由 Docker 构建成功隐含验证。genhtml 的行统计另含 5 个「带分支计数的
> 条件行」：`L87`（`Hb_Initialized != TRUE`）、`L94`（`< HB_PERIOD_CYCLES`）、
> `L109`（CAN 故障位）、`L110`（SAFE_STOP）、`L121`（`> RZC_HB_ALIVE_MAX`），
> 均由相应场景命中，故 43/43 行全部覆盖。

---

## 分支覆盖分析（100%，10/10）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `Hb_Initialized != TRUE` | L87 | ✅ 两侧 | `uninitialized_main_noop`（true，40 次）/ 全部已初始化场景（false，595 次） |
| `Hb_CycleCounter < HB_PERIOD_CYCLES` | L94 | ✅ 两侧 | `no_tx_below_boundary`、`init_defaults_ecu_id`（true，479 次）/ TX 边界场景（false，116 次） |
| `fault_mask & RZC_FAULT_CAN != 0` | L109 | ✅ 两侧 | `can_fault_safe_stop_suppresses_tx`、`can_fault_without_safe_stop_no_suppress`、`can_fault_cleared_recovers` 前置（true，16 次）/ 无 CAN 故障 TX 边界（false，100 次） |
| `vehicle_state == RZC_STATE_SAFE_STOP` | L110 | ✅ 两侧 | `can_fault_safe_stop_suppresses_tx`、`can_fault_cleared_recovers` 前置（true，12 次）/ `can_fault_without_safe_stop_no_suppress`（false，4 次） |
| `Hb_AliveCounter > RZC_HB_ALIVE_MAX` | L121 | ✅ 两侧 | `alive_counter_wraps`（true，4 次）/ 其余 TX（false，100 次） |

> 全部 5 个分支点两侧均已覆盖，无无法覆盖的分支。`L109 && L110` 是与门
> 短路求值：`can_fault_without_safe_stop_no_suppress`
> （CAN 故障位置位但车辆非 SAFE_STOP）使 L109 true 侧 + L110 false 侧同时
> 命中；`safe_stop_without_can_fault_no_suppress`（无 CAN 故障但车辆
> SAFE_STOP）使 L109 false 侧命中（L110 短路不求值）。与 CVC `Swc_Heartbeat`
> 不同，RZC 版本无 WdgM 检查点、RX 指示或通信状态复位逻辑，无额外分支点。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（43/43） | 0 行 | — |
| 分支 | 100%（10/10） | 0 个 | — |
| 函数 | 100%（5/5） | — | — |

> 本模块全部代码路径均经公开 API（`Init` + `MainFunction`）驱动，无防御性
> 不可达分支：`Hb_Initialized`、`Hb_CycleCounter`、`Hb_AliveCounter` 三个静态
> 状态全部由 Init/MainFunction 赋值，无 NULL 指针或非法参数路径。与
> `Swc_Watchdog`/`Swc_Scheduler` 中不可达的 `CfgPtr == NULL_PTR` 守卫不同，
> RZC 心跳无配置指针参数，因此**无需豁免项**。观测 getter（`#ifdef UNIT_TEST`）
> 不计入交付固件，仅为测试编译产物。
