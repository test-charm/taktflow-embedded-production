# CVC 车辆状态机 (Swc_VehicleState) E2E 测试设计

## 被测功能

**CVC ASW 车辆状态机（VSM）— 权威 CVC 车辆状态源**

覆盖链路：

```text
RTE 故障信号（踏板/急停/通信/电机/制动/转向/电池/SC 继电器）
  → Swc_VehicleState_MainFunction（10ms 周期）
  → 派生事件 → Swc_VehicleState_OnEvent → 状态迁移表
  → BswM_RequestMode 模式通知
  → Dem_ReportErrorStatus DTC 上报
  → Rte_Write 车辆状态回写
```

这是 CVC 的 ASIL-D 核心状态机，有 6 个状态（INIT/RUN/DEGRADED/LIMP/SAFE_STOP/SHUTDOWN）
与 17 个事件，迁移由常量二维表 `transition_table[current_state][event]` 驱动。

与首个 ASW E2E（`Swc_Pedal → Swc_CvcCom`）一致，本测试**不**走仪表盘/系统 E2E 运行器，
而是通过测试专用 API 在原生测试框架内执行真实的 `Swc_VehicleState.c` 生产代码。

---

## 被测代码流程图

```
                     ┌──────────────────┐
                     │ Swc_VehicleState_Init │
                     └────────┬─────────┘
                              │ (state=INIT, 清 latch/计数器)
                     ┌────────▼─────────┐
                     │  MainFunction     │
                     │  (每 10ms)        │
                     └────────┬─────────┘
                              │
         Step1: Rte_Read 读取全部故障信号
                              │
         Step2: INIT 保持计时 + 待定自检通过
                (self_test_pass_pending && hold>=INIT_HOLD && 心跳OK → RUN)
                              │
         Step3: 派生事件（优先级从高到低）
              [EStop] → [SC_KILL] → [CAN 超时(去抖)] → [踏板故障]
              → [电池故障] → [爬行守护] → [故障清除] → [确认读故障]
                              │
         Step4: 确认读 (ConfirmFault) — 电机切断/制动/转向/电机过流
                去抖 3 周期 + Com 新鲜读 + E2E 检查 → 触发事件
                              │
         Step5: SAFE_STOP 恢复 + 故障锁存解除
                              │
         Step6: Rte_Write 车辆状态 + 心跳运行模式
```

---

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `selfTestPass` | 自检通过事件注入（INIT→RUN 前提） | `false`、`true` | Given/When — 事件注入 |
| `estop` | 急停激活 | `false`、`true` | When — 故障注入 |
| `scRelayEnergized` | SC 继电器吸合（1=正常，0=切断） | `true`、`false` | When — 故障注入 |
| `fzcComm` | FZC 通信状态 | `OK`、`TIMEOUT` | When — 故障注入 |
| `rzcComm` | RZC 通信状态 | `OK`、`TIMEOUT` | When — 故障注入 |
| `pedalFault` | 踏板故障信号 | `0`、`1` | When — 故障注入 |
| `motorCutoff` | 电机切断请求 | `0`、`1` | When — 故障注入 |
| `brakeFault` | 制动故障 | `0`、`1` | When — 故障注入 |
| `steeringFault` | 转向故障 | `0`、`1` | When — 故障注入 |
| `batteryStatus` | 电池状态（RZC 编码） | `DISABLE_LOW`(0)、`WARN_LOW`(1)、`NORMAL`(2)、`WARN_HIGH`(3)、`DISABLE_HIGH`(4) | When — 故障注入 |
| `motorFaultRzc` | RZC 电机故障 | `0`、`1` | When — 故障注入 |
| `motorSpeed` | 电机转速 (RPM) | `0`、`60` | When — 爬行守护输入 |
| `torqueRequest` | 扭矩请求 (%) | `0`、`60` | When — 爬行守护输入 |
| `pedalPosition` | 踏板位置 | `0` | When — 爬行守护输入 |
| `cycles` | 10ms 循环次数（每阶段） | 见各用例 | When — 执行控制 |

> 平台常量（POSIX/SIL 编译）：`CVC_INIT_HOLD_CYCLES=1000`、`CVC_POST_INIT_GRACE_CYCLES=1000`、
> `CVC_FAULT_UNLATCH_CYCLES=300`、`CVC_SAFE_STOP_RECOVERY_CYCLES=200`、
> `CAN_TMO_DEBOUNCE_THRESHOLD=50`、`CVC_FAULT_CONFIRM_THRESHOLD=3`、`CVC_CREEP_DEBOUNCE_TICKS=200`。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `vehicleState` | 最终车辆状态（字符串） | `INIT`、`RUN`、`DEGRADED`、`LIMP`、`SAFE_STOP`、`SHUTDOWN` |
| `bswmMode` | 最后请求的 BswM 模式 | `STARTUP`、`RUN`、`DEGRADED`、`SAFE_STOP`、`SHUTDOWN` |
| `dtcNames` | 上报的 DTC 名称（去重、逗号分隔） | `BRAKE_FAULT_RX`、`STEERING_FAULT_RX`、`MOTOR_CUTOFF_RX`、`BATT_UNDERVOLT`、`CREEP_FAULT` |
| `stateTrace` | 状态迁移轨迹（逗号分隔） | `INIT,RUN`、`INIT,RUN,SAFE_STOP,INIT,RUN` |

---

## 测试用例

> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/setup` 存储）：使车辆到达被测前置状态
>   （自检通过 + 保持周期 → RUN；再等待后 INIT 宽限过期）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/vehicle-state` body）：触发状态迁移的最后动作，
>   服务端按「前置 + 刺激」顺序执行。
> 下表 `P0..P4` 表示**刺激阶段**序列；未列出的因子取默认正常值
> （`scRelayEnergized=true`、`fzcComm=OK`、`rzcComm=OK`、`batteryStatus=NORMAL`、其余故障=0）。
> 「前置阶段」为 `{selfTestPass=true, 1005} [+ {1005} 等待宽限]`（爬行/恢复用例省略等待段）。

| 用例 | 阶段序列 | 期望 vehicleState | 期望 bswmMode | 期望 dtcNames |
|---|---|---|---|---|
| init_to_run_after_self_test_and_heartbeats | P0: selfTestPass=true, 1005 | RUN | RUN | — |
| init_stays_init_without_self_test_pass | P0: 1005 | INIT | STARTUP | — |
| run_to_safe_stop_on_estop | P0: selfTestPass=true,1005; P1: estop=true,5 | SAFE_STOP | SAFE_STOP | — |
| run_to_degraded_on_pedal_fault | P0: selfTestPass=true,1005; P1: 1005; P2: pedalFault=1,5 | DEGRADED | DEGRADED | — |
| run_to_limp_on_battery_critical | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=DISABLE_LOW,1 | LIMP | DEGRADED | BATT_UNDERVOLT |
| run_to_safe_stop_on_dual_can_timeout | P0: selfTestPass=true,1005; P1: 1005; P2: fzcComm=TIMEOUT,rzcComm=TIMEOUT,55 | SAFE_STOP | SAFE_STOP | — |
| run_to_safe_stop_on_brake_fault | P0: selfTestPass=true,1005; P1: 1005; P2: brakeFault=1,5 | SAFE_STOP | SAFE_STOP | BRAKE_FAULT_RX |
| run_to_safe_stop_on_steering_fault | P0: selfTestPass=true,1005; P1: 1005; P2: steeringFault=1,5 | SAFE_STOP | SAFE_STOP | STEERING_FAULT_RX |
| run_to_degraded_on_motor_cutoff | P0: selfTestPass=true,1005; P1: 1005; P2: motorCutoff=1,5 | DEGRADED | DEGRADED | MOTOR_CUTOFF_RX |
| run_to_safe_stop_on_creep | P0: selfTestPass=true,1005; P1: torqueRequest=60,205 | SAFE_STOP | SAFE_STOP | CREEP_FAULT |
| degraded_to_run_on_fault_clear | P0: selfTestPass=true,1005; P1: 1005; P2: pedalFault=1,5; P3: 5 | RUN | RUN | — |
| safe_stop_recovery_to_run | P0: selfTestPass=true,1005; P1: estop=true,5; P2: 520; P3: selfTestPass=true,1005 | RUN | RUN | — |
| run_to_shutdown_on_sc_kill | P0: selfTestPass=true,1005; P1: 1005; P2: scRelayEnergized=false,5 | SHUTDOWN | SHUTDOWN | — |
| run_to_safe_stop_on_dual_pedal_fault | P0: selfTestPass=true,1005; P1: 1005; P2: pedalFaultDual=true,1 | SAFE_STOP | SAFE_STOP | — |
| run_to_safe_stop_on_sustained_battery_crit | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=0,1; P3: batteryStatus=0,1 | SAFE_STOP | SAFE_STOP | BATT_UNDERVOLT |
| run_to_safe_stop_on_single_can_timeout | P0: selfTestPass=true,1005; P1: 1005; P2: fzcComm=TIMEOUT,55 | SAFE_STOP | SAFE_STOP | — |
| run_to_degraded_on_battery_warn | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=WARN_LOW,1 | DEGRADED | DEGRADED | — |
| brake_com_disagree_keeps_run | P0: selfTestPass=true,1005; P1: 1005; P2: brakeFault=1,comBrakeFault=0,5 | RUN | RUN | — |
| motor_pdu_timeout_to_degraded | P0: selfTestPass=true,1005; P1: 1005; P2: motorPduTimedOut=true,5 | DEGRADED | DEGRADED | MOTOR_OVERCURRENT |
| safe_stop_recovery_interrupted | P0: selfTestPass=true,1005; P1: estop=true,1; P2: 350; P3: estop=true,1; P4: 520; P5: selfTestPass=true,1005 | RUN | RUN | — |
| degraded_to_safe_stop_on_motor_cutoff | P0: selfTestPass=true,1005; P1: 1005; P2: pedalFault=1,5; P3: motorCutoff=1,5 | SAFE_STOP | SAFE_STOP | MOTOR_CUTOFF_RX |
| limp_to_safe_stop_on_motor_cutoff | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=0,1; P3: motorCutoff=1,5 | SAFE_STOP | SAFE_STOP | BATT_UNDERVOLT,MOTOR_CUTOFF_RX |

> **分支覆盖补充场景（S23-S42）**——目标为行覆盖之外的分支侧（B/C/D/E 类）：
>
> **B 类（心跳/SC kill 时序短路侧）：**
> | 用例 | 阶段序列 | 期望 vehicleState | 覆盖分支 |
> |---|---|---|---|
> | fzc_timeout_blocks_init_to_run | P0: selfTestPass=true,fzcComm=TIMEOUT,1005 | INIT | L593/L608 false 侧（fzc 心跳阻断 RUN） |
> | rzc_timeout_blocks_init_to_run | P0: selfTestPass=true,rzcComm=TIMEOUT,1005 | INIT | L593/L609 false 侧（rzc 心跳阻断 RUN） |
> | sc_kill_silent_during_grace | P0: selfTestPass=true,1005; P1: scRelayEnergized=false,5 | RUN | L653/L654 false 侧（宽限期内 SC kill 静默） |
>
> **C 类（电池枚举补全）：**
> | 用例 | 阶段序列 | 期望 vehicleState | 覆盖分支 |
> |---|---|---|---|
> | battery_high_crit_to_limp | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=DISABLE_HIGH,1 | LIMP | L735 `==4` 侧（DISABLE_HIGH） |
> | battery_high_warn_to_degraded | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=WARN_HIGH,1 | DEGRADED | L740 `==3` 侧（WARN_HIGH） |
>
> **D 类（SAFE_STOP 恢复窗口内瞬时信号再现 → 恢复计数清零）：**
> 统一模式：`Given RUN` → `estop` 触发 SAFE_STOP → 等待 300 周期故障锁存解除 → 注入 1 周期
> 瞬时信号（恢复计数被清零，`safe_stop_clear_count=0`）→ 故障清除 + 200 周期恢复 → 回 RUN。
> | 用例 | 注入信号（恢复窗口内 1 周期） | 期望 vehicleState | 覆盖分支 |
> |---|---|---|---|
> | recovery_reset_by_motor_cutoff | motorCutoff=1 | RUN | L941 false 侧 |
> | recovery_reset_by_motor_fault_rzc | motorFaultRzc=1 | RUN | L942 false 侧 |
> | recovery_reset_by_brake_fault | brakeFault=1 | RUN | L943 false 侧 |
> | recovery_reset_by_steering_fault | steeringFault=1 | RUN | L944 false 侧 |
> | recovery_reset_by_pedal_fault | pedalFault=1 | RUN | L945 false 侧 |
> | recovery_reset_by_sc_relay_kill | scRelayEnergized=false | RUN | L946 false 侧 + L897 true 侧 |
> | recovery_reset_by_rzc_timeout | rzcComm=TIMEOUT | RUN | L948 false 侧 |
> | recovery_reset_by_battery_crit | batteryStatus=DISABLE_LOW | RUN | L949 false 侧 |
>
> **E 类（其余短路/时序分支，S36-S41）：**
> | 用例 | 阶段序列 | 期望 vehicleState | 覆盖分支 |
> |---|---|---|---|
> | self_test_pass_in_run_noop | P0: selfTestPass=true,1005; P1: 1005; P2: selfTestPass=true,5 | RUN | L358 `current_state==INIT` false 侧 |
> | single_can_timeout_keeps_limp | P0: selfTestPass=true,1005; P1: 1005; P2: batteryStatus=0,1; P3: fzcComm=TIMEOUT,55 | LIMP | L682 `state==RUN/DEGRADED` false 侧（LIMP 下单超时无迁移） |
> | pedal_pressed_skips_creep | P0: selfTestPass=true,1005; P1: pedalPosition=60,torqueRequest=60,205 | RUN | L769 `pedal_position<50` false 侧（踩踏板非爬行） |
> | motor_spinning_skips_creep | P0: selfTestPass=true,1005; P1: motorSpeed=60,torqueRequest=60,205 | RUN | L776 `motor_speed<50` false 侧（电机已转非爬行） |
> | brake_fault_blocks_fault_clear | P0: selfTestPass=true,1005; P1: 1005; P2: pedalFault=1,5; P3: brakeFault=1,2; P4: 5 | RUN | L813 `brake_fault==0` false 侧（DEGRADED 中制动故障阻止清除） |
> | sc_kill_in_init_noop | P0: scRelayEnergized=false,1005 | INIT | L653 `current_state!=INIT` false 侧（INIT 下 SC 切断不触发） |

> 周期数说明：`1005 = 1000（保持/宽限）+ 5 余量`；`55 = 50（CAN 去抖）+ 5`；
> `205 = 200（爬行去抖）+ 5`；`520 = 300（故障解锁）+ 200（恢复）+ 20 余量`；
> `350 = 300（故障解锁）+ 50（恢复计数递增中）`；
> 电池 CRIT 仅注入 `1` 个周期：RUN→LIMP 为即时事件，持续注入会进一步 LIMP→SAFE_STOP（升锁，
> 这正是 S15 的验证目标）。

---

## 代码路径覆盖

- 6 个状态的到达：INIT、RUN、DEGRADED、LIMP、SAFE_STOP、SHUTDOWN 全部覆盖 ✅
- 状态迁移表关键边：INIT→RUN、RUN→DEGRADED、RUN→LIMP、RUN→SAFE_STOP、
  DEGRADED→RUN、DEGRADED→SAFE_STOP（电机切断）、LIMP→SAFE_STOP（电机切断）、
  SAFE_STOP→INIT→RUN、RUN→SHUTDOWN 全部覆盖 ✅
- 事件派生：SELF_TEST_PASS（待定守卫）、ESTOP、CAN_TIMEOUT_DUAL、CAN_TIMEOUT_SINGLE、
  PEDAL_FAULT_SINGLE、PEDAL_FAULT_DUAL（harness 注入）、BATTERY_CRIT、BATTERY_WARN、
  BRAKE_FAULT（确认读）、STEERING_FAULT（确认读）、MOTOR_CUTOFF（确认读）、MOTOR_OVERCURRENT（PDU 超时）、
  CREEP_FAULT、FAULT_CLEARED、SC_KILL 全部覆盖 ✅
- 分支：EStop 非宽限门控、故障确认去抖（3 周期）及 Com 不一致拒绝、故障锁存解除（300 周期）、
  SAFE_STOP 恢复（200 周期）及恢复中断、爬行守护（200 周期）、单/双 CAN 超时去抖全部覆盖 ✅

---

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_VehicleState.c.gcov.html` 实测：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **96.2%**（405 / 421 行） |
| **分支覆盖** | **96.6%**（197 / 204 分支） |
| **函数覆盖** | **100%**（5 / 5） |

覆盖到的函数：`Swc_VehicleState_Init`、`Swc_VehicleState_GetState`、`Swc_VehicleState_OnEvent`、
`Swc_VehicleState_ConfirmFault`、`Swc_VehicleState_MainFunction`。

> 下表「实测命中」为单次完整套件运行（12 个踏板场景 + 41 个状态机场景）的累积值，供参考；
> 每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

行覆盖与分支覆盖分别说明如下。

---

## 行覆盖分析（96.2%，405/421）

行覆盖反映**每一行是否被执行**。未覆盖的 16 行全部属于防御性守卫或状态机结构不可达
（见「无法覆盖的行」），不存在可通过补充场景覆盖的行级缺口——原行级缺口已通过 S14-S22 清零。

### 逐函数代码行覆盖映射

下表按函数/代码块列出每个可执行代码块的覆盖情况、由哪些场景覆盖、以及实测命中次数
（`MainFunction` 累计调用 38952 次）。

#### 辅助常量与状态表（L34-284）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L50 | `VSM_DIAG` 空宏（非 SIL 编译路径） | 全部场景 | 135 |
| L64-71 | `state_to_bswm_mode` 状态→BswM 模式映射表 | 全部场景（BswM 通知） | — |
| L79-200 | `transition_table[6][17]` 状态迁移常量表 | 全部场景（OnEvent 查表） | — |
| L263-281 | 确认读常量（`CVC_FAULT_CONFIRM_THRESHOLD`、Com 信号 ID 等） | S7/S8/S9（确认读路径） | — |

#### Swc_VehicleState_Init（L291-318）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L295 | `current_state = CVC_STATE_INIT` | 全部场景（harness 每次调用 Init） | 20 |
| L296-317 | 清 `initialized/self_test_pass_pending/init_hold_counter/safe_stop_clear_count/can_tmo_debounce/creep_debounce_count`、清 `fault_confirm_count[4]`、清 `fault_latched[9]`/`fault_unlatch_count[9]` | 全部场景 | 39 |

#### Swc_VehicleState_GetState（L325-328）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L327 | `return current_state` | 全部场景（harness 每次周期后 `record_state()` 读取） | 39 |

#### Swc_VehicleState_OnEvent（L339-413）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L358-363 | SELF_TEST_PASS 在 INIT 的待定守卫（`self_test_pass_pending = TRUE` 后 return） | S1-S13 前置、S14-S20 前提前置、S12/S20 恢复段 | 58（其中 pending=TRUE 21 次） |
| L366 | `next_state = transition_table[current_state][event]` 查表 | 全部场景（每次有效/无效事件） | 37 |
| L369-372 | `next_state == INVALID` 拒绝（不迁移） | S2（未自检）、S4-S20 无效迁移 | 37 |
| L379 | `current_state = next_state` 执行迁移 | 全部有效迁移 | 19 |
| L389-409 | 进入 SAFE_STOP 时锁存故障（`fault_latched[latch_idx]=TRUE`） | S3/S6/S7/S8/S14/S15（EStop/CAN双超时/制动/转向/双踏板/电池升锁） | 10 |
| L394 | `case EVT_ESTOP → LATCH_IDX_ESTOP` | S3（急停） | — |
| L396 | `case EVT_MOTOR_CUTOFF → LATCH_IDX_MOTOR_CUTOFF` | S21（DEGRADED 下电机切断）、S22（LIMP 下电机切断）→ SAFE_STOP | — |
| L397 | `case EVT_BRAKE_FAULT → LATCH_IDX_BRAKE` | S7（制动） | — |
| L398 | `case EVT_STEERING_FAULT → LATCH_IDX_STEERING` | S8（转向） | — |
| L399 | `case EVT_PEDAL_FAULT_DUAL → LATCH_IDX_PEDAL_DUAL` | S14（双踏板故障注入） | — |
| L400 | `case EVT_CAN_TIMEOUT_DUAL → LATCH_IDX_CAN_DUAL` | S6（双 CAN 超时） | — |
| L401 | `case EVT_BATTERY_CRIT → LATCH_IDX_BATTERY_CRIT` | S15（持续电池临界 → LIMP→SAFE_STOP 升锁） | — |
| L412 | `BswM_RequestMode(CVC_ECU_ID_CVC, 状态对应模式)` | 全部有效迁移 | 19 |

#### Swc_VehicleState_ConfirmFault（L431-477）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L436-439 | `rte_value != 0` → 去抖计数递增；否则清零 | S7/S8/S9（故障激活）+ 各场景无故障清零 | 1320 |
| L440-443 | 连续 3 周期达 `CVC_FAULT_CONFIRM_THRESHOLD` | S7/S8/S9（制动/转向/电机切断确认） | 25 |
| L445-453 | Com 新鲜读（`Com_ReceiveSignal` 复核非 0） | S7/S8/S9（Com 一致）、S18（`comBrakeFault=0` 不一致 → `confirmed=FALSE`） | 5 |
| L450-452 | Com 读返回 0 → `confirmed = FALSE`（不一致分支） | S18（`brakeFault=1` 但 `comBrakeFault=0`，保持 RUN 不迁移） | 3 |
| L460-468 | 确认通过 → `Dem_ReportErrorStatus(FAILED)` + `OnEvent` | S7（BRAKE_FAULT_RX）、S8（STEERING_FAULT_RX）、S9（MOTOR_CUTOFF_RX）、S19（MOTOR_OVERCURRENT） | 5 |
| L470-476 | 未达阈值时清零（`fault_confirm_count=0`） | 各场景正常通过路径 | — |

#### Swc_VehicleState_MainFunction（L491-978）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L519-531 | `Rte_Read` 读取全部 13 个故障信号 | 全部场景 | 38952 |
| L536-538 | `Com_GetRxPduQuality(MOTOR_STATUS) == TIMED_OUT` → `motor_fault_rzc = 1`（fail-closed） | S19（`motorPduTimedOut=true`，5 周期 → DEGRADED + DTC MOTOR_OVERCURRENT） | 5 |
| L573-577 | INIT 保持计时器递增（`init_hold_counter`） | S1-S13 初始前置、S14-S20 前提前置 | 22005 |
| L598-610 | 待定自检通过 + 保持期满 + 心跳 OK（`fzc_comm==OK && rzc_comm==OK`） | S1 及 S3-S13 前置、S12/S20 恢复段 | 22005 |
| L611-635 | `INIT → RUN`：清 pending、重置确认/爬行计数、置后 INIT 宽限、`BswM_RequestMode(RUN)` | S1、S3-S13、S12/S20 恢复段 | 21 |
| L643-646 | EStop 激活 → `OnEvent(EVT_ESTOP)` | S3（急停）、S12/S20（恢复前置） | 12 |
| L653-658 | SC 继电器切断（非 INIT、非宽限）→ `OnEvent(EVT_SC_KILL)` | S13（SC 切断） | 15 |
| L667-674 | CAN 超时去抖计数（fzc 或 rzc TIMEOUT） | S6（双超时 50 周期去抖）、S16（单超时 50 周期去抖） | 21382 |
| L676-680 | 达 `CAN_TMO_DEBOUNCE_THRESHOLD` 且双超时 → `OnEvent(EVT_CAN_TIMEOUT_DUAL)` | S6（55 周期双超时） | 6 |
| L682-685 | 达阈值且单侧超时（RUN/DEGRADED）→ `OnEvent(EVT_CAN_TIMEOUT_SINGLE)` | S16（仅 `fzcComm=1` 55 周期 → SAFE_STOP） | 1 |
| L711-715 | 踏板故障（RUN + 宽限期过）→ `OnEvent(EVT_PEDAL_FAULT_SINGLE)` | S4（踏板故障）、S11（DEGRADED 前置） | 2 |
| L730-738 | 电池 CRIT（status 0/4）→ DTC `BATT_UNDERVOLT` + `OnEvent(EVT_BATTERY_CRIT)` | S5（batteryStatus=0 → LIMP）、S15（持续 → SAFE_STOP） | 3 |
| L741-743 | 电池 WARN（status 1/3）→ `OnEvent(EVT_BATTERY_WARN)` | S17（`batteryStatus=1` → DEGRADED） | 1 |
| L765-780 | 爬行守护：扭矩>阈值且（已计数或转速<阈值）→ 计数 | S10（torqueRequest=60，200 周期去抖） | 200 |
| L782-786 | 爬行计数达 `CVC_CREEP_DEBOUNCE_TICKS` → DTC `CREEP_FAULT` + `OnEvent(EVT_CREEP_FAULT)` | S10（205 周期 → SAFE_STOP） | 1 |
| L793-797 | 无扭矩 → 爬行计数清零 | 各场景正常驾驶路径 | — |
| L809-816 | DEGRADED 且全部降级故障清除 → `OnEvent(EVT_FAULT_CLEARED)` | S11（故障清除 5 周期 → RUN） | 1 |
| L833-835 | INIT 状态下抑制 ConfirmFault | 初始前置阶段 | 71823 |
| L838-856 | 后 INIT 宽限递减（1000 周期）→ 过期重置心跳通信状态 | 各场景前置等待段 | 17570 |
| L865-886 | 非抑制时 4 路 `ConfirmFault`（电机切断/电机过流/制动/转向） | S7/S8/S9 | 330 |
| L895-904 | SAFE_STOP 下 8 个锁存瞬时信号值组装 | S3/S6/S7/S8/S12/S14/S15 | 1087 |
| L909-934 | 锁存解除计数（每锁存 300 周期） | S12/S20（SAFE_STOP 恢复解锁） | 10870 |
| L937-958 | 全部锁存清除且瞬时信号全清 → 恢复计数 200 → `SAFE_STOP → INIT` + `BswM_RequestMode(STARTUP)` | S12/S20（恢复 200 周期） | 2 |
| L962-965 | 恢复检查负向分支：瞬时信号不全清 → 复位恢复计数 | S20（恢复途中 `cycles=350` 时重新注入 `estop`，恢复计数归零后再续） | 7 |
| L974-977 | `Rte_Write` 车辆状态 + 心跳运行模式 | 全部场景（每周期） | 38952 |

### 无法覆盖的行（16 行，占 3.8%）

剩余未覆盖行全部属于防御性守卫（A 类 12 行）或状态机结构不可达（B 类 4 行），
**不存在可通过场景补充的行级缺口**。详细说明与分支级不可覆盖点统一见
「[无法覆盖的代码](#无法覆盖的代码行与分支全部不可达)」。

> **说明**：`EVT_MOTOR_CUTOFF` 的锁存 case（L396）曾被误判为 B 类不可达。实际上 transition_table
> 中仅 **RUN** 状态将电机切断映射到 DEGRADED（fail-silent，L113）；**DEGRADED**（L133）与 **LIMP**（L153）
> 状态映射到 SAFE_STOP，会命中该锁存 case。已通过 S21（先 pedal fault 到 DEGRADED，再注入 motor cutoff）
> 与 S22（先 battery CRIT 到 LIMP，再注入 motor cutoff）覆盖全部三条状态相关的迁移边。

### 行覆盖补充场景（S14-S22）

S14-S22 覆盖了原「仅间接覆盖」的行级分支（双踏板故障、电池升锁、单 CAN 超时、电池告警、
制动 Com 不一致、PDU 超时、恢复中断、DEGRADED/LIMP 电机切断）：

| 场景 | 刺激阶段（When） | 覆盖的代码行 | 覆盖的分支 |
|---|---|---|---|
| S14 双踏板故障 | `{cycles:1, pedalFaultDual:true}` | L399 | `EVT_PEDAL_FAULT_DUAL` latch（RUN→SAFE_STOP） |
| S15 持续电池临界 | `{cycles:1, batteryStatus:0}` ×2 | L401 | `EVT_BATTERY_CRIT` latch（LIMP→SAFE_STOP 升锁） |
| S16 单侧 CAN 超时 | `{cycles:55, fzcComm:1}` | L682-685 | `EVT_CAN_TIMEOUT_SINGLE`（仅 fzc 超时） |
| S17 电池告警 | `{cycles:1, batteryStatus:1}` | L741-743 | `EVT_BATTERY_WARN`（RUN→DEGRADED） |
| S18 制动 Com 不一致 | `{cycles:5, brakeFault:1, comBrakeFault:0}` | L450-452 | `ConfirmFault` Com 新鲜读不一致 → `confirmed=FALSE`（保持 RUN） |
| S19 Motor PDU 超时 | `{cycles:5, motorPduTimedOut:true}` | L536-538 | `Com_GetRxPduQuality==TIMED_OUT` → fail-closed `motor_fault_rzc=1` → DEGRADED |
| S20 恢复中断 | `{cycles:1, estop} {350} {1, estop} {520} {1005, selfTestPass}` | L962-965 | SAFE_STOP 恢复负向分支（恢复计数归零后仍可恢复） |
| S21 DEGRADED 下电机切断 | `{cycles:5, pedalFault:1} {cycles:5, motorCutoff:1}` | L396 | `EVT_MOTOR_CUTOFF` latch（DEGRADED→SAFE_STOP） |
| S22 LIMP 下电机切断 | `{cycles:1, batteryStatus:0} {cycles:5, motorCutoff:1}` | L396 + L153 迁移边 | `EVT_MOTOR_CUTOFF` latch（LIMP→SAFE_STOP 迁移表边） |

> 其中 S14/S18/S19 依赖 harness 增强：`pedalFaultDual`（事件注入，弥补生产无触发源）、
> `comBrakeFault`/`comMotorCutoff`（Com shadow 解耦）、`motorPduTimedOut`（RX PDU 超时质量）。
> S14 的 `EVT_PEDAL_FAULT_DUAL` 在 `MainFunction` 中无派生源（生产仅派生 SINGLE），由 harness
> 直接注入 `Swc_VehicleState_OnEvent(EVT_PEDAL_FAULT_DUAL)` 模拟 Swc_Pedal 双传感器合理性故障。

---

## 分支覆盖分析（96.6%，197/204）

行覆盖率不能反映 `&&`/`||` 短路、多值枚举等**同一行内多个判断侧**的覆盖情况。
`./gradlew cucumber` 生成的 `Swc_VehicleState.c.gcov.html` 提供 lcov `BRDA:` 分支数据，
实测 204 个分支、197 个覆盖（96.6%）。S23-S42 共覆盖 B/C/D/E 类 35 个分支，
剩余 7 个分支全部不可达。

### B 类：时序/心跳短路下未走到的一侧（S23-S25、S41）

| 分支 | 位置 | 覆盖场景 | 说明 |
|---|---|---|---|
| `hb_ok` 的 `(fzc&&rzc)==OK` 不为真侧 | L593 | S23（fzc 超时）、S24（rzc 超时） | 心跳双 OK 被阻断时 `hb_ok=0` |
| `fzc_comm==OK` 为假侧 | L608 | S23（fzcComm=TIMEOUT） | 自检通过但 FZC 心跳超时 → 保持 INIT |
| `rzc_comm==OK` 为假侧 | L609 | S24（rzcComm=TIMEOUT） | 同上，RZC 侧 |
| `sc_relay_kill==0 && state!=INIT && grace==0` 的 `grace!=0` 侧 | L653/L654 | S25（自检后立即 sc kill） | 宽限期内 SC kill 静默，保持 RUN |
| SC_KILL `current_state != INIT` 的 false 侧 | L653 | S41（INIT 下 SC kill） | INIT 下 SC 切断不触发迁移 |

### C 类：多值枚举只测了一个值（S26-S27）

| 分支 | 位置 | 覆盖场景 | 说明 |
|---|---|---|---|
| `battery_status==0 \|\| ==4` 的 `==4` 侧 | L735 | S26（batteryStatus=DISABLE_HIGH） | 与 `==0`（DISABLE_LOW）同行为 → LIMP |
| `battery_status==1 \|\| ==3` 的 `==3` 侧 | L740 | S27（batteryStatus=WARN_HIGH） | 与 `==1`（WARN_LOW）同行为 → DEGRADED |

### D 类：SAFE_STOP 恢复窗口内瞬时信号再现（S28-S35）

| 分支 | 位置 | 覆盖场景 | 说明 |
|---|---|---|---|
| 恢复检查 `(estop==0 && motor_cutoff==0 && ...)` 的 `motor_cutoff==0` 假侧 | L941 | S28 | 恢复计数进行中 motor_cutoff 再现 → `safe_stop_clear_count=0` |
| `motor_fault_rzc==0` 假侧 | L942 | S29 | 同上，RZC 电机故障 |
| `brake_fault==0` 假侧 | L943 | S30 | 同上，制动故障 |
| `steering_fault==0` 假侧 | L944 | S31 | 同上，转向故障 |
| `pedal_fault==0` 假侧 | L945 | S32 | 同上，踏板故障 |
| `sc_relay_kill!=0` 假侧 + `(sc_relay_kill==0)?1:0` true 侧 | L897/L946 | S33 | 同上，SC 继电器切断 |
| `rzc_comm==OK` 假侧 | L948 | S34 | 同上，RZC 通信超时 |
| `battery_status==2` 假侧 | L949 | S35 | 同上，电池非 NORMAL |

> **说明**：这 8 个瞬时信号原本互斥（一次只走一个 `&&` 短路分支）。S28-S35 采用统一模式——先
> `estop` 触发 SAFE_STOP，等 300 周期锁存解除后，在恢复窗口内注入 1 周期目标信号，使恢复计数清零，
> 再清除信号等待 200 周期恢复回 RUN。`estop==0` 侧（L940）与 `fzc_comm==OK` 假侧（L947）已由
> 既有场景覆盖，无需新增。

### E 类：其余短路/时序分支（S36-S40、S42）

| 分支 | 位置 | 覆盖场景 | 说明 |
|---|---|---|---|
| `SELF_TEST_PASS && state==INIT` 的 `state!=INIT` 侧 | L358 | S36 | RUN 下再次注入自检通过 → 无效果（保持 RUN） |
| 单超时 `state==RUN \|\| state==DEGRADED` 的 `state==DEGRADED` 为 true 侧 | L682 | S42 | DEGRADED 持续 pedalFault 下注入单超时 → SAFE_STOP（`state==RUN` 短路后才评估 `state==DEGRADED`） |
| 爬行守护 `pedal_position<50` 的 false 侧 | L769 | S38 | 踩踏板（pedalPosition≥50）非爬行 → 守护跳过 |
| 爬行守护 `motor_speed<50` 的 false 侧 | L776 | S39 | 电机已转（motorSpeed≥50）非爬行 → 守护跳过 |
| 故障清除 `brake_fault==0` 的 false 侧 | L813 | S40 | DEGRADED 中制动故障（未确认）阻止 FAULT_CLEARED |

### 剩余未覆盖分支（7 个，全部不可达）

7 个未覆盖分支全部不可达，与行级不可覆盖点同源，统一见「[无法覆盖的代码](#无法覆盖的代码行与分支全部不可达)」：

- **A 类防御守卫 4 个**（L344/L348/L352/L513）
- **B 类结构不可达 3 个**：L395（SC_KILL latch）、L630（INIT→RUN grace）、L696（CAN_RESTORED）

---

## 无法覆盖的代码（行与分支，全部不可达）

行级（16 行）与分支级（8 个）的不可覆盖点源自同一批底层代码，统一说明如下。
行与分支的未覆盖**不是**可补场景的功能缺口——S14-S22 已清零行级缺口、S23-S41 已清零分支级缺口，
剩余均无法从 E2E 层触发。

### A. 防御性守卫（行 + 分支均不可覆盖，属 ASIL-D 安全护栏）

| 代码块 | 行覆盖 | 分支覆盖 | 无法覆盖的理由 |
|---|---|---|---|
| `OnEvent`：`initialized != TRUE` 返回 | ✗（L345-347） | ✗（L344） | harness 恒先 Init 再调用，永不未初始化 |
| `OnEvent`：`event >= CVC_EVT_COUNT` 返回 | ✗（L349-351） | ✗（L348） | 只注入合法事件，无法从 REST API 注入越界事件 |
| `OnEvent`：`current_state >= CVC_STATE_COUNT` 返回 | ✗（L353-355） | ✗（L352） | 状态恒在 0-5 |
| `MainFunction`：`initialized != TRUE` 返回 | ✗（L514-516） | ✗（L513） | harness 在 MainFunction 前调用 Init |

> 行级小计 12 行、分支级小计 4 个。均已由 `test_Swc_VehicleState_asild.c` 单元测试覆盖。

### B. 状态机结构决定的不可达分支（行 + 分支均不可覆盖）

| 代码块 | 行覆盖 | 分支覆盖 | 无法覆盖的理由 |
|---|---|---|---|
| `case EVT_SC_KILL → LATCH_IDX_SC_KILL` | ✗（L395） | ✗（L395） | SC_KILL 迁移目标恒为 SHUTDOWN（transition_table 各行均映射 SHUTDOWN，TSR-035 外部覆盖），永不进入 `next_state==SAFE_STOP` 锁存块 |
| LIMP 下通信恢复 `EVT_CAN_RESTORED` | ✗（L697-699） | ✗（L696） | **时序互斥**：该分支仅在 `post_init_grace_counter != 0`（宽限期）执行，但进入 LIMP 需 `battery CRIT` 而电池处理（L733）要求 `grace == 0`——宽限期内无法进入 LIMP |

> 行级小计 4 行、分支级小计 2 个。均已由单元测试覆盖
> （`test_Any_to_SAFE_STOP_on_SC_kill`、`test_LIMP_to_DEGRADED_on_CAN_restored`）。

### C. 行可达但分支侧不可达（短路/时序保护）

| 代码块 | 行覆盖 | 未覆盖分支侧 | 原因 |
|---|---|---|---|
| INIT→RUN 转换块 `post_init_grace_counter > 0` | ✓（L630） | false 侧 | 该块仅在 INIT→RUN 转换瞬间执行，此时 grace 恒为刚设置值（>0），false 侧不可能到达 |

> 行均可达，仅特定短路侧不可达；分支级小计 1 个。
> `state==RUN \|\| state==DEGRADED`（L682）的 DEGRADED 侧原被误判为不可达，实为短路保护——
> S16 走 `state==RUN` true 短路（跳过 DEGRADED 求值）；S42（DEGRADED 持续 + 单超时）覆盖
> `state==DEGRADED` true 侧、S37（LIMP 单超时）覆盖 false 侧，L682 已全部分支覆盖。

### 小结

| 类别 | 行未覆盖 | 分支未覆盖 |
|---|---|---|
| A. 防御性守卫 | 12 行 | 4 个 |
| B. 结构不可达 | 4 行 | 2 个 |
| C. 行可达分支侧不可达 | 0 行 | 1 个 |
| **合计** | **16 行（3.8%）** | **7 个（3.4%）** |

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 96.2%（405/421） | 16 行 | A 防御守卫 12 行 + B 结构不可达 4 行 |
| 分支 | 96.6%（197/204） | 7 个 | A 防御守卫 4 个 + B 结构不可达 2 个 + C 短路侧 1 个 |
| 函数 | 100%（5/5） | — | — |
