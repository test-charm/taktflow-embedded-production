# CVC 心跳 (Swc_Heartbeat) E2E 测试设计

## 被测功能

**CVC ASW 心跳 SWC — 50ms TX 边界调度（存活计数器递增/回绕 + WdgM SE3 喂狗 + OperatingMode RTE 写）、RX 指示（FZC/RZC/未知 ECU 标志位）、post-INIT 宽限期通信状态复位（OK + E2E SM 强制 VALID）**

覆盖链路：

```text
RTE 车辆状态信号（CVC_SIG_VEHICLE_STATE，=187）
  → Swc_Heartbeat_MainFunction（10ms 周期）
  → 5 周期（50ms）TX 边界：
       · WdgM_CheckpointReached(SE 3)        喂狗检查点
       · alive_counter++（4-bit，>15 → 回绕 0）TX 存活计数器
       · Rte_Write(CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE，=56)  → Com TX 自动拉取

Com 层心跳 RX（FZC 0x02 / RZC 0x03 / 未知）
  → Swc_Heartbeat_RxIndication(ecuId)         RX 指示标志位锁存

启动瞬态（Docker 宽限期结束）
  → Swc_Heartbeat_ResetCommStatus()
       · fzc/rzc_comm_status = OK
       · E2E_Sm_Init + Status 强制 VALID（跳过 MIN_OK_INIT 窗口）
       · Rte_Write(FZC/RZC_COMM_STATUS，=77/134，OK)
```

与既有 ASW E2E（`Swc_Pedal`、`Swc_VehicleState`、`Swc_EStop`、`Swc_CvcCom`）一致，
通过测试专用 API 在原生测试框架内执行真实的 `Swc_Heartbeat.c` 生产代码。当前
`Swc_Heartbeat` 中 RX 超时监控与 DTC 上报已移交 `Com_MainFunction_Rx` 截止时间监控
（CommStatusRteSignalId），本 SWC 仅保留 TX 调度、RX 指示标志与通信状态复位。

> **被测代码观测**：`alive_counter`、`fzc/rxc_rx_flag`、`fzc/rzc_comm_status`、
> `fzc/rzc_sm_state.Status` 均为模块静态状态，无法从外部直接读取。为支持 E2E 断言，
> 在 `Swc_Heartbeat.c/.h` 增加了 **`#ifdef UNIT_TEST` 保护**的观测 getter
> （`Swc_Heartbeat_GetAliveCounter` / `GetFzcRxFlag` / `GetRzcRxFlag` /
> `GetFzcCommStatus` / `GetRzcCommStatus` / `GetFzcSmStatus` / `GetRzcSmStatus`）。
> 生产固件构建（STM32/TMS570/POSIX target）不定义 `UNIT_TEST`，这些访问器不进入
> 交付固件；仅测试 harness 编译时生效。

## 被测代码流程图

```
                       ┌──────────────────┐
                       │ Swc_Heartbeat_Init│
                       │ (alive=0, tx_t=0, │
                       │  flags=FALSE,     │
                       │  comm=TIMEOUT,    │
                       │  E2E_Sm_Init,     │
                       │  initialized=TRUE)│
                       └────────┬─────────┘
                                │
                       ┌────────▼─────────┐
                       │  MainFunction     │
                       │  (每 10ms)        │
                       └────────┬─────────┘
                                │
         Step1: initialized==FALSE？ ──Y──→ return（未初始化空转）
                                │N
         Step2: tx_timer++
                                │
         Step3: tx_timer >= HB_TX_CYCLES(5)？ ──N──→ return（未到 50ms 边界）
                                │Y
         Step4: TX 边界处理
                · Rte_Read(CVC_SIG_VEHICLE_STATE)
                · WdgM_CheckpointReached(3)    喂狗
                · alive_counter++
                · alive_counter > 15？ ──Y──→ alive_counter = 0（回绕）
                · tx_timer = 0
                · Rte_Write(CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE, vehicle_state)
                                │
         （RX 监控已移交 Com_MainFunction_Rx，此处无代码）
```

RX 指示与复位：

```
  Swc_Heartbeat_RxIndication(ecuId)
    ├─ ecuId == FZC(0x02) → fzc_rx_flag = TRUE
    ├─ ecuId == RZC(0x03) → rzc_rx_flag = TRUE
    └─ 其他（未知）        → 忽略

  Swc_Heartbeat_ResetCommStatus()
    · fzc/rzc_comm_status = OK
    · E2E_Sm_Init（fzc/rzc）
    · fzc/rzc_sm_state.Status = E2E_SM_VALID（强制跳过 MIN_OK_INIT 窗口）
    · Rte_Write(FZC/RZC_COMM_STATUS, OK)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Heartbeat_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`4`（<5 无 TX，边界）、`5`（恰 5 周期，边界）、`10`（2×TX）、`80`（16×TX，回绕） | When — 执行控制 |
| `vehicleState` | RTE `CVC_SIG_VEHICLE_STATE` 读取值 | `1`（RUN）、`2`（DEGRADED）、`4`（SAFE_STOP） | When — 状态注入 |
| `rxEcu` | `RxIndication` 参数 | `0`（不调用）、`2`（FZC）、`3`（RZC）、`255`（未知，边界） | When — RX 注入 |
| `resetComm` | 是否调用 `Swc_Heartbeat_ResetCommStatus()` | `false`、`true` | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `aliveCounter` | TX 存活计数器（getter） | TX 次数 mod 16；回绕后为 0 |
| `wdgmCheckpointCount` | WdgM_CheckpointReached 调用次数 | = TX 边界数 |
| `wdgmLastSeId` | 最后一次喂狗 SE id | `3` |
| `operatingMode` | RTE `CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE` | = vehicleState |
| `fzcRxFlag` / `rzcRxFlag` | RX 指示标志（getter） | FZC/RZC 分别置位 |
| `fzcCommStatus` / `rzcCommStatus` | 通信状态（getter） | Init 后 TIMEOUT(1)，Reset 后 OK(0) |
| `fzcSmStatus` / `rzcSmStatus` | E2E SM 状态（getter） | Init 后 INIT(0)，Reset 后 VALID(1) |
| `rteFzcCommStatus` / `rteRzcCommStatus` | RTE FZC/RZC 通信状态信号（=77/134） | Reset 后 OK(0) |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: TX 调度 — Swc_Heartbeat_MainFunction**：Init 默认值 / 未初始化守卫 /
>   TX 边界时序 / 存活计数器回绕 / OperatingMode 透传 / 多阶段状态，共 8 场景。
> - **规则: RX 指示 — Swc_Heartbeat_RxIndication**：FZC / RZC / 未知 ECU，共 3 场景。
> - **规则: 通信状态复位 — Swc_Heartbeat_ResetCommStatus**：OK + SM VALID + RTE 写，共 1 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/heartbeat/setup` 存储）：设置前置心跳状态
>   （如车辆状态基线）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/heartbeat` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`cycles=1`、
> `vehicleState=RUN`、`rxEcu=0`、`resetComm=false`、`skipInit=false`）。

### 规则: TX 调度 — Swc_Heartbeat_MainFunction

| 用例 | 阶段序列 | 期望 aliveCounter | 期望 wdgmCount/SeId | 期望 operatingMode | 期望 comm/SM 状态 |
|---|---|---|---|---|---|
| init_defaults_timeout | P0: cycles=1 | 0 | 0/- | 0 | comm=TIMEOUT(1), SM=INIT(0) |
| uninitialized_main_noop | P0: cycles=10, skipInit=true | 0 | 0/- | 0 | comm=0（未初始化静态初值）, SM=0 |
| no_tx_below_boundary | P0: cycles=4 | 0 | 0/- | 0 | comm=TIMEOUT, SM=INIT |
| tx_at_exact_boundary | P0: cycles=5 | 1 | 1/3 | 1（RUN） | — |
| tx_every_5_cycles | P0: cycles=10 | 2 | 2/3 | 1（RUN） | — |
| alive_counter_wraps | P0: cycles=80 | 0（第 16 次 TX 回绕） | 16/3 | 1（RUN） | — |
| operating_mode_tracks_state | P0: cycles=5, vehicleState=2 | 1 | 1/3 | 2（DEGRADED） | — |
| multi_phase_state_change | 前置: cycles=5, vehicleState=1; P0: cycles=5, vehicleState=4 | 2 | 2/3 | 4（SAFE_STOP） | — |

### 规则: RX 指示 — Swc_Heartbeat_RxIndication

| 用例 | 阶段序列 | 期望 fzcRxFlag | 期望 rzcRxFlag |
|---|---|---|---|
| fzc_rx_sets_flag | P0: cycles=1, rxEcu=2 | 1 | 0 |
| rzc_rx_sets_flag | P0: cycles=1, rxEcu=3 | 0 | 1 |
| unknown_ecu_ignored | P0: cycles=1, rxEcu=255 | 0 | 0 |

### 规则: 通信状态复位 — Swc_Heartbeat_ResetCommStatus

| 用例 | 阶段序列 | 期望 fzc/rzcCommStatus | 期望 fzc/rzcSmStatus | 期望 rteFzc/rzcCommStatus |
|---|---|---|---|---|
| reset_comm_status_ok | P0: cycles=1, resetComm=true | OK(0) | VALID(1) | OK(0) |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_defaults_timeout` | 初始化后默认状态为 TIMEOUT 且 E2E SM 为 INIT |
> | `uninitialized_main_noop` | 未初始化时主函数不动作 |
> | `no_tx_below_boundary` | 初始化后 4 周期内未到 TX 边界不发送 |
> | `tx_at_exact_boundary` | 恰好在 5 周期 (50ms) 边界发送心跳 |
> | `tx_every_5_cycles` | 每 5 周期发送一次 (10 周期两次) |
> | `alive_counter_wraps` | 存活计数器在第 16 次发送时从 15 回绕到 0 |
> | `operating_mode_tracks_state` | TX 边界将车辆状态写入心跳 OperatingMode |
> | `multi_phase_state_change` | 车辆状态变化在后续周期透传到 OperatingMode |
> | `fzc_rx_sets_flag` | FZC 心跳指示置位 FZC RX 标志 |
> | `rzc_rx_sets_flag` | RZC 心跳指示置位 RZC RX 标志 |
> | `unknown_ecu_ignored` | 未知 ECU 心跳指示被忽略 |
> | `reset_comm_status_ok` | 复位通信状态置 OK 并强制 E2E SM 为 VALID |

## 代码路径覆盖

- `Swc_Heartbeat_Init` 全部可执行行 ✅
- `Swc_Heartbeat_MainFunction` 全部可执行行 ✅
  - 未初始化守卫（`initialized==FALSE` → return）✅
  - `tx_timer++` 与 `tx_timer >= HB_TX_CYCLES` 两侧（<5 无 TX / =5 TX）✅
  - TX 边界：Rte_Read / WdgM_CheckpointReached(3) / alive_counter++ / 回绕 /
    tx_timer=0 / Rte_Write(OperatingMode) ✅
- `Swc_Heartbeat_RxIndication` 全部分支 ✅
  - FZC 分支 / RZC 分支 / 未知 ECU（忽略）✅
- `Swc_Heartbeat_ResetCommStatus` 全部可执行行 ✅
  - comm_status=OK / E2E_Sm_Init / SM 强制 VALID / Rte_Write 两路 ✅
- UNIT_TEST 观测 getters（仅测试编译）✅ 由 harness 输出读取，全部命中

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Heartbeat.c.gcov.html` 实测（2026-08-16 全量套件 298 场景运行后，
含本 feature 12 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（70 / 70 行） |
| **分支覆盖** | **100%**（10 / 10 分支） |
| **函数覆盖** | **100%**（11 / 11 函数） |

覆盖到的函数：`Swc_Heartbeat_Init`、`Swc_Heartbeat_MainFunction`、
`Swc_Heartbeat_RxIndication`、`Swc_Heartbeat_ResetCommStatus`，以及 7 个
`#ifdef UNIT_TEST` 观测 getter（`GetAliveCounter`、`GetFzcRxFlag`、
`GetRzcRxFlag`、`GetFzcCommStatus`、`GetRzcCommStatus`、`GetFzcSmStatus`、
`GetRzcSmStatus`）。

> 下表「实测命中」为完整套件（298 场景）运行后的累积值（本容器运行期间
> 多次执行 feature 的累积：25 次 harness 调用，其中 2 次因 `skipInit` 跳过
> Init）；每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。
> 生产固件编译不定义 `UNIT_TEST`，getter 相关行不计入交付固件的有效代码。

---

## 行覆盖分析（100%，70/70）

行覆盖反映**每一行是否被执行**。70 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_Heartbeat_Init（L107-124）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L107 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init） | 23 |
| L108-109 | `alive_counter=0`、`tx_timer=0` | 全部已初始化场景 | 23 |
| L111-112 | `fzc_rx_flag=FALSE`、`rzc_rx_flag=FALSE` | 全部已初始化场景 | 23 |
| L114-115 | `fzc/rzc_comm_status=TIMEOUT` | `init_defaults_timeout`（断言 comm=TIMEOUT） | 23 |
| L117-118 | `fzc/rzc_last_alive=0` | 全部已初始化场景 | 23 |
| L120-121 | `E2E_Sm_Init(fzc/rzc_sm_state)` | 全部已初始化场景 | 23 |
| L123-124 | `initialized=TRUE` + `}` | 全部已初始化场景 | 23 |

#### Swc_Heartbeat_MainFunction（L137-173）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L137 | 函数入口 `{` | 全部场景（每周期进入） | 263 |
| L138-140 | `if (initialized == FALSE)` 守卫 + return | true 侧：`uninitialized_main_noop`（20 次）；false 侧：全部已初始化周期（243 次） | 263 |
| L143 | `tx_timer++` | 全部已初始化场景 | 243 |
| L145 | `if (tx_timer >= HB_TX_CYCLES)` | true 侧：≥5 周期场景；false 侧：`init_defaults_timeout`、`no_tx_below_boundary` | 243 |
| L146 | `uint32 vehicle_state = 0u` | TX 场景 | 45 |
| L149 | `Rte_Read(CVC_SIG_VEHICLE_STATE, ...)` | TX 场景（读到注入的 vehicleState） | 45 |
| L154 | `WdgM_CheckpointReached(3u)` | TX 场景（wdgmLastSeId=3 断言） | 45 |
| L157 | `alive_counter++` | TX 场景 | 45 |
| L158-160 | `if (alive_counter > 15) alive_counter = 0` | true 侧：`alive_counter_wraps`（80 周期第 16 次 TX，2 次命中）；false 侧：其余 TX 场景 | 45 |
| L163-164 | `tx_timer = 0` | TX 场景 | 45 |
| L166-167 | `Rte_Write(OperatingMode, vehicle_state)` + `}` | TX 场景（operatingMode 断言 = vehicleState） | 45 |
| L173 | 函数结束 `}` | 全部已初始化场景 | 243 |

#### Swc_Heartbeat_RxIndication（L180-188）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L180 | 函数入口 `{` | `fzc_rx_sets_flag`、`rzc_rx_sets_flag`、`unknown_ecu_ignored` | 6 |
| L181-182 | `ecuId==FZC` → `fzc_rx_flag=TRUE` | `fzc_rx_sets_flag`（rxEcu=2） | 2 |
| L183-184 | `ecuId==RZC` → `rzc_rx_flag=TRUE` | `rzc_rx_sets_flag`（rxEcu=3） | 2 |
| L185-187 | 未知 ECU 忽略分支 | `unknown_ecu_ignored`（rxEcu=255） | 2 |
| L188 | 函数结束 `}` | 三个 RX 场景 | 6 |

#### Swc_Heartbeat_ResetCommStatus（L197-211）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L197 | 函数入口 `{` | `reset_comm_status_ok` | 2 |
| L198-199 | `fzc/rzc_comm_status = OK` | `reset_comm_status_ok`（comm=OK 断言） | 2 |
| L205-206 | `E2E_Sm_Init(fzc/rzc_sm_state)` | `reset_comm_status_ok` | 2 |
| L207-208 | `fzc/rzc_sm_state.Status = VALID` | `reset_comm_status_ok`（SM=VALID 断言） | 2 |
| L209-210 | `Rte_Write(FZC/RZC_COMM_STATUS, OK)` | `reset_comm_status_ok`（rteCommStatus=OK 断言） | 2 |
| L211 | 函数结束 `}` | `reset_comm_status_ok` | 2 |

#### UNIT_TEST 观测 getters（L222-254，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L222-254 | 7 个 getter 返回静态心跳状态 | 全部场景（harness 输出 JSON 逐次调用） | 25（各 getter） |

> 常量/静态声明（L49-52 `_Static_assert`、L61-97 静态变量与 E2E SM 配置）为非执行行，
> 不计入行覆盖；`_Static_assert` 为编译期检查，由 Docker 构建成功隐含验证。
> genhtml 的行统计另含 6 个「带分支计数的条件行」：`L47`（`#define HB_TX_CYCLES`
> 宏展开归属行，命中 243）、`L138`（`initialized==FALSE` 守卫）、`L145`
> （`tx_timer>=HB_TX_CYCLES`）、`L158`（`alive_counter>CVC_HB_ALIVE_MAX`）、
> `L181`（`ecuId==FZC`）、`L183`（`ecuId==RZC`）。其中 `L47`/`L138`/`L145`/
> `L158` 由 TX 场景命中，`L181`/`L183` 由 RX 场景命中，故 70/70 行全部覆盖。

---

## 分支覆盖分析（100%，10/10）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `initialized == FALSE` | L138 | ✅ 两侧 | `uninitialized_main_noop`（true）/ 全部已初始化场景（false） |
| `tx_timer >= HB_TX_CYCLES` | L145 | ✅ 两侧 | ≥5 周期场景（true）/ `no_tx_below_boundary`、`init_defaults_timeout`（false） |
| `alive_counter > CVC_HB_ALIVE_MAX` | L158 | ✅ 两侧 | `alive_counter_wraps`（true）/ 其余 TX 场景（false） |
| `ecuId == CVC_ECU_ID_FZC` | L181 | ✅ 两侧 | `fzc_rx_sets_flag`（true）/ `rzc_rx_sets_flag`、`unknown_ecu_ignored`（false） |
| `ecuId == CVC_ECU_ID_RZC` | L183 | ✅ 两侧 | `rzc_rx_sets_flag`（true）/ `fzc_rx_sets_flag`、`unknown_ecu_ignored`（false） |

> 全部 5 个分支点两侧均已覆盖，无无法覆盖的分支。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（70/70） | 0 行 | — |
| 分支 | 100%（10/10） | 0 个 | — |
| 函数 | 100%（11/11） | — | — |
