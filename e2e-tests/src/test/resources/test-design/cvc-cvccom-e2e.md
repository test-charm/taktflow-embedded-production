# CVC CAN 通信 (Swc_CvcCom) E2E 测试设计

## 被测功能

**CVC ASW CAN 通信 SWC — TX 调度（心跳/0x100 车辆状态/转向制动/E-Stop 广播/车身控制/扭矩请求）与 RX 桥接（Com 故障信号 → RTE）**

覆盖链路：

```text
RTE 故障信号（estop/relayKill/motorCutoff/brakeFault/steerFault/pedalFault/fzcComm/rzcComm/torque）
  → Swc_CvcCom_TransmitSchedule（10ms 周期）
  → 心跳（ECU_ID + Operating_Mode）
  → 0x100 Vehicle_State（faultMask 8 位组合 + Torque_Limit 钳位）
  → 转向/制动命令（SAFE_STOP 起 max-brake=100）
  → E-Stop_Broadcast（Active/Source）
  → Body_Control_Cmd（0x350）
  → Torque_Request 桥（钳位 100%）

Com RX 影子（Brake_Fault/Brake_Status/Motor_Cutoff/SC_Status/Battery/Steering/Motor/FZC/RZC alive）
  → Swc_CvcCom_BridgeRxToRte（10ms 周期）
  → RTE 故障信号（brakeFault/motorCutoff/scRelayKill/batteryStatus/steeringFault/motorFaultRzc/心跳存活计数器）
```

与既有 ASW E2E（`Swc_Pedal`、`Swc_VehicleState`、`Swc_EStop`）一致，本测试通过测试专用 API
在原生测试框架内执行真实的 `Swc_CvcCom.c` 生产代码。E2E 保护已移入 `Com_MainFunction_Tx/Rx`
（Phase 2），本 SWC 只通过 `Com_SendSignal`/`Com_ReceiveSignal` 访问 CAN。

## 被测代码流程图

```
                    ┌───────────────────┐
                    │ Swc_CvcCom_Init    │
                    │ (Initialized=TRUE) │
                    └────────┬──────────┘
                             │
              ┌──────────────▼──────────────┐
              │ TransmitSchedule(timeMs)     │
              └──────────────┬──────────────┘
                             │
        Step1: Initialized != TRUE？ ──Y──→ return（未初始化空转）
                             │N
        Step2: 心跳 TX
               · Com_Heartbeat.EcuId = CVC_ECU_ID_CVC
               · Com_Heartbeat.OperatingMode = Swc_VehicleState_GetState()
                             │
        Step3: 0x100 Vehicle_State faultMask 组合
               · estopActive≠0    → 0x01
               · scRelayKill==0   → 0x02（0=继电器切断）
               · motorCutoff≠0    → 0x04
               · brakeFault≠0     → 0x08
               · steeringFault≠0  → 0x10
               · pedalFault≠0     → 0x20
               · fzcComm==TIMEOUT → 0x40
               · rzcComm==TIMEOUT → 0x80
               · torque>100 → 钳位 100
               · Rte_Write(FaultMask)
               · Com_0x100.Mode/FaultMask/TorqueLimit
                             │
        Step4: 转向/制动命令
               · vs>=SAFE_STOP → brake=100（max），否则 0
               · steer=0（居中）
               · Com_Steer_Angle_Cmd / Com_Brake_Force_Cmd
                             │
        Step5: E-Stop 广播桥
               · estopActive = (estop_val≠0) ? 1 : 0
               · Com_EStop_Broadcast.Active / Source(=1)
                             │
        Step6: Body_Control_Cmd（0x350）全 0 + Torque_Request 桥（钳位 100%）
                             │
              ┌──────────────▼──────────────┐
              │ BridgeRxToRte()              │
              └──────────────┬──────────────┘
                             │
        Step7: Initialized != TRUE？ ──Y──→ return（未初始化空转）
                             │N
        Step8: 制动故障 = (bf_event≠0) ? bf_event : bf_status
               · Com_Brake_Fault.FaultType (0x210 事件帧)
               · Com_Brake_Status.BrakeFaultStatus (0x201 周期状态)
                             │
        Step9: RX 桥接
               · Motor_Cutoff → RTE
               · SC_Status.RelayEnergized → RTE(SC_RELAY_KILL)
               · Battery_Status.Level → RTE
               · Steering_Status.SteerFault → RTE
               · Motor_Status.MotorFault → RTE
               · FZC/RZC_Heartbeat.E2E_Alive → RTE（存活计数器）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_CvcCom_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | TransmitSchedule 调用次数 | `1` | When — 执行控制 |
| `bridgeRx` | 是否调用 `Swc_CvcCom_BridgeRxToRte()` | `false`（仅 TX）、`true`（TX+RX 桥接） | When — 执行控制 |
| `vehicleState` | `Swc_VehicleState_GetState()` 返回值 | `1`（RUN）、`2`（DEGRADED）、`4`（SAFE_STOP，边界）、`5`（SHUTDOWN） | When — 状态注入 |
| `estop` | RTE `CVC_SIG_ESTOP_ACTIVE` | `0`、`1` | When — 故障注入 |
| `relayKill` | RTE `CVC_SIG_SC_RELAY_KILL` | `1`（正常=继电器吸合）、`0`（切断，边界） | When — 故障注入 |
| `motorCutoff` | RTE `CVC_SIG_MOTOR_CUTOFF` | `0`、`1` | When — 故障注入 |
| `brakeFault` | RTE `CVC_SIG_BRAKE_FAULT` | `0`、`1` | When — 故障注入 |
| `steerFault` | RTE `CVC_SIG_STEERING_FAULT` | `0`、`1` | When — 故障注入 |
| `pedalFault` | RTE `CVC_SIG_PEDAL_FAULT` | `0`、`1` | When — 故障注入 |
| `fzcComm` | RTE `CVC_SIG_FZC_COMM_STATUS` | `0`（OK）、`1`（TIMEOUT，边界） | When — 故障注入 |
| `rzcComm` | RTE `CVC_SIG_RZC_COMM_STATUS` | `0`（OK）、`1`（TIMEOUT，边界） | When — 故障注入 |
| `torque` | RTE `CVC_SIG_TORQUE_REQUEST` | `0`、`60`（正常）、`150`（>100，钳位） | When — 数据注入 |
| `rxBrakeEvent` | Com `Brake_Fault.FaultType`（0x210） | `0`、`1`（事件帧优先） | When — RX 注入 |
| `rxBrakeStatus` | Com `Brake_Status.BrakeFaultStatus`（0x201） | `0`、`1` | When — RX 注入 |
| `rxMotorCutoff` | Com `Motor_Cutoff_Req.RequestType` | `0`、`1` | When — RX 注入 |
| `rxScRelay` | Com `SC_Status.RelayEnergized` | `1`（吸合/正常）、`0`（切断） | When — RX 注入 |
| `rxBattery` | Com `Battery_Status.Level` | `0`、`1` | When — RX 注入 |
| `rxSteerFault` | Com `Steering_Status.SteerFaultStatus` | `0`、`1` | When — RX 注入 |
| `rxMotorFault` | Com `Motor_Status.MotorFaultStatus` | `0`、`1` | When — RX 注入 |
| `rxFzcAlive` | Com `FZC_Heartbeat.E2E_Alive` | `0`、`5` | When — RX 注入 |
| `rxRzcAlive` | Com `RZC_Heartbeat.E2E_Alive` | `0`、`9` | When — RX 注入 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `heartbeatEcuId` | Com `CVC_Heartbeat.EcuId` | `1`（CVC_ECU_ID_CVC） |
| `heartbeatMode` | Com `CVC_Heartbeat.OperatingMode` | = vehicleState |
| `vehicleStateMode` | Com `Vehicle_State.Mode`（0x100） | = vehicleState |
| `faultMask` | Com `Vehicle_State.FaultMask`（0x100） | 8 位组合 |
| `torqueLimit` | Com `Vehicle_State.TorqueLimit`（0x100） | 钳位后的扭矩 |
| `steerAngleCmd` | Com `Steer_Command.SteerAngleCmd` | `0`（居中） |
| `brakeForceCmd` | Com `Brake_Command.BrakeForceCmd` | `0` / `100` |
| `estopBroadcastActive` | Com `EStop_Broadcast.Active`（0x001） | `0` / `1` |
| `estopBroadcastSource` | Com `EStop_Broadcast.Source` | `1`（CVC） |
| `torqueCommandPct` | Com `Torque_Request.CommandPct` | 钳位后的扭矩 |
| `body*` | Com `Body_Control_Cmd`（0x350）5 信号 | `0`（全 0，Phase 2 前固定值） |
| `rteBrakeFault` | RTE `CVC_SIG_BRAKE_FAULT`（桥接后） | `0` / `1` |
| `rteMotorCutoff` | RTE `CVC_SIG_MOTOR_CUTOFF`（桥接后） | `0` / `1` |
| `rteScRelayKill` | RTE `CVC_SIG_SC_RELAY_KILL`（桥接后） | `0` / `1` |
| `rteBattery` | RTE `CVC_SIG_BATTERY_STATUS`（桥接后） | `0` / `1` |
| `rteSteerFault` | RTE `CVC_SIG_STEERING_FAULT`（桥接后） | `0` / `1` |
| `rteMotorFaultRzc` | RTE `CVC_SIG_MOTOR_FAULT_RZC`（桥接后） | `0` / `1` |
| `rteFzcAlive` | RTE `CVC_SIG_FZC_HEARTBEAT_E2E_ALIVE`（桥接后） | `0` / `5` |
| `rteRzcAlive` | RTE `CVC_SIG_RZC_HEARTBEAT_E2E_ALIVE`（桥接后） | `0` / `9` |
| `rteFaultMask` | RTE `CVC_SIG_FAULT_MASK`（TX 写回） | 与 faultMask 一致 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: TX 调度 — Swc_CvcCom_TransmitSchedule**：心跳 / 0x100 faultMask / 扭矩钳位 /
>   制动覆盖 / E-Stop 广播 / 多阶段，共 14 场景。
> - **规则: RX 桥接 — Swc_CvcCom_BridgeRxToRte**：未初始化守卫 / 制动双源 / 故障与存活计数器，
>   共 4 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/cvccom/setup` 存储）：设置前置 CAN 状态
>   （如健康 RUN 基线）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/cvccom` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`vehicleState=RUN`、
> `relayKill=1`（继电器吸合）、其余故障=0、`bridgeRx=false`、`skipInit=false`）。

### 规则: TX 调度 — Swc_CvcCom_TransmitSchedule

| 用例 | 阶段序列 | 期望 heartbeatEcuId/Mode | 期望 faultMask | 期望 brakeForceCmd | 期望 estopActive | 期望 rte* 桥接 |
|---|---|---|---|---|---|---|
| uninitialized_tx_noop | P0: skipInit=true,rxMotorCutoff=1 | 0/0（未初始化空转） | 0 | 0 | 0 | — |
| healthy_run_tx | P0: vehicleState=1,relayKill=1 | 1/1 | 0 | 0 | 0 | — |
| estop_active_mask01 | P0: estop=1,relayKill=1 | 1/1 | 0x01 | 0 | 1 | rteFaultMask=1 |
| relay_killed_mask02 | P0: relayKill=0 | 1/1 | 0x02 | 0 | 0 | rteFaultMask=2 |
| motor_brake_mask0c | P0: motorCutoff=1,brakeFault=1 | 1/1 | 0x0C | 0 | 0 | rteFaultMask=12 |
| steer_pedal_mask30 | P0: steerFault=1,pedalFault=1 | 1/1 | 0x30 | 0 | 0 | rteFaultMask=48 |
| comm_timeout_maskc0 | P0: fzcComm=1,rzcComm=1 | 1/1 | 0xC0 | 0 | 0 | rteFaultMask=192 |
| all_faults_maskff | P0: estop=1,relayKill=0,motorCutoff=1,brakeFault=1,steerFault=1,pedalFault=1,fzcComm=1,rzcComm=1 | 1/1 | 0xFF | 0 | 1 | rteFaultMask=255 |
| torque_clamped_100 | P0: torque=150 | 1/1 | 0 | 0 | 0 | torqueLimit=100 |
| torque_passthrough_60 | P0: torque=60 | 1/1 | 0 | 0 | 0 | torqueLimit=60 |
| safe_stop_brake_override | P0: vehicleState=4(SAFE_STOP) | 1/4 | 0 | 100 | 0 | — |
| shutdown_brake_override | P0: vehicleState=5(SHUTDOWN) | 1/5 | 0 | 100 | 0 | — |
| degraded_mode_passthrough | P0: vehicleState=2(DEGRADED) | 1/2 | 0 | 0 | 0 | — |
| multi_phase_state | 前置: vehicleState=1; P0: vehicleState=4 | 1/4 | 0 | 100 | 0 | — |

### 规则: RX 桥接 — Swc_CvcCom_BridgeRxToRte

| 用例 | 阶段序列 | 期望 heartbeatEcuId/Mode | 期望 faultMask | 期望 brakeForceCmd | 期望 estopActive | 期望 rte* 桥接 |
|---|---|---|---|---|---|---|
| uninitialized_rx_noop | P0: skipInit=true,bridgeRx=true,rxMotorCutoff=1 | — | — | — | — | rteMotorCutoff=0（桥接未运行） |
| rx_bridge_brake_status | P0: bridgeRx=true,rxBrakeEvent=0,rxBrakeStatus=1 | — | — | — | — | rteBrakeFault=1 |
| rx_bridge_brake_event | P0: bridgeRx=true,rxBrakeEvent=1,rxBrakeStatus=0 | — | — | — | — | rteBrakeFault=1（事件优先） |
| rx_bridge_all | P0: bridgeRx=true,rxMotorCutoff=1,rxScRelay=0,rxBattery=1,rxSteerFault=1,rxMotorFault=1,rxFzcAlive=5,rxRzcAlive=9 | — | — | — | — | 全 1/0/5/9 |

> 默认 `relayKill=1`（继电器吸合）用于避免误触发 0x02 位；`estop_active_mask01`
> 用例同时验证 `estopBroadcastActive=1`（0x001 广播桥）。

## 代码路径覆盖

- `Swc_CvcCom_Init` 全部可执行行 ✅
- `TransmitSchedule` 全部可执行行 ✅
  - 未初始化守卫（`Initialized != TRUE` → return）✅
  - 心跳 TX（ECU_ID + OperatingMode 透传 VSM 状态）✅
  - 0x100 faultMask 8 位组合（每一位 0/1 两侧）✅
  - 扭矩钳位（`>100` → 100；`0..100` 透传）✅
  - `Rte_Write(CVC_SIG_FAULT_MASK)` 写回 ✅
  - 转向/制动：`SAFE_STOP/SHUTDOWN` → brake=100；RUN/DEGRADED → brake=0 ✅
  - E-Stop 广播桥（estop≠0 → Active=1/Source=1）✅
  - Body_Control_Cmd 全 0 透传 ✅
  - Torque_Request 桥钳位 ✅
- `BridgeRxToRte` 全部可执行行 ✅
  - 未初始化守卫 ✅
  - 制动故障双源合并（事件帧优先，周期状态兜底）✅
  - Motor_Cutoff / SC_Relay / Battery / Steering / Motor_RZC 桥接 ✅
  - FZC/RZC 心跳存活计数器桥接 ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_CvcCom.c.gcov.html` 实测（2026-08-15 全量套件 82 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（146 / 146 行） |
| **分支覆盖** | **100%**（34 / 34 分支） |
| **函数覆盖** | **100%**（3 / 3） |

覆盖到的函数：`Swc_CvcCom_Init`、`Swc_CvcCom_TransmitSchedule`、`Swc_CvcCom_BridgeRxToRte`。

> 下表「实测命中」为完整套件（82 个场景：踏板/状态机/E-Stop/CvcCom）运行后的累积值，
> 供参考；每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（100%，146/146）

行覆盖反映**每一行是否被执行**。146 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_CvcCom_Init（L55-58）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L56-58 | 函数体：`CvcCom_Initialized = TRUE` | 全部场景（每个 harness 运行先 Init，skipInit 场景除外） | 56 |

#### Swc_CvcCom_TransmitSchedule（L72-213）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L73-74 | 函数入口 + `(void)currentTimeMs` | 全部场景（每周期进入） | 1721 |
| L76-79 | `if (CvcCom_Initialized != TRUE)` 守卫 + return | `uninitialized_tx_noop`（skipInit=true，true 侧 2 次）+ 其余场景（false 侧 1719 次） | 1721 |
| L82-90 | 心跳 TX（ECU_ID + OperatingMode） | 全部已初始化场景 | 1719 |
| L93-97 | 0x100 txBuf/变量声明 + `faultMask=0` | 全部已初始化场景 | 1719 |
| L99 | `for (j=0; j<8; j++)` 清 txBuf | 全部已初始化场景（循环体+退出两侧） | 1719 |
| L102 | `txBuf[2] = Swc_VehicleState_GetState()` | 全部已初始化场景 | 1719 |
| L105-106 | `estop≠0 → 0x01` | true 侧：`estop_active_mask01`、`all_faults_maskff`；false 侧：全部其余 | 1719 |
| L107-108 | `relayKill==0 → 0x02` | true 侧：`relay_killed_mask02`、`all_faults_maskff`；false 侧：其余（relayKill=1） | 1719 |
| L109-110 | `motorCutoff≠0 → 0x04` | true 侧：`motor_brake_mask0c`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L111-112 | `brakeFault≠0 → 0x08` | true 侧：`motor_brake_mask0c`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L113-114 | `steerFault≠0 → 0x10` | true 侧：`steer_pedal_mask30`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L115-116 | `pedalFault≠0 → 0x20` | true 侧：`steer_pedal_mask30`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L117-118 | `fzcComm==TIMEOUT → 0x40` | true 侧：`comm_timeout_maskc0`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L119-120 | `rzcComm==TIMEOUT → 0x80` | true 侧：`comm_timeout_maskc0`、`all_faults_maskff`；false 侧：其余 | 1719 |
| L121 | `txBuf[3] = faultMask` | 全部已初始化场景 | 1719 |
| L125-129 | 扭矩读取 + `>100` 钳位 | true 侧：`torque_clamped_100`（torque=150）；false 侧：全部其余 | 1719 |
| L133 | `Rte_Write(CVC_SIG_FAULT_MASK)` | 全部已初始化场景 | 1719 |
| L136-142 | 0x100 Mode/FaultMask/TorqueLimit Com 信号 | 全部已初始化场景 | 1719 |
| L148-151 | 转向/制动变量声明 | 全部已初始化场景 | 1719 |
| L153-160 | `vs >= SAFE_STOP` → brake=100，否则 0 | true 侧：`safe_stop_brake_override`、`shutdown_brake_override`、`multi_phase_state`（406 次）；false 侧：RUN/DEGRADED（1313 次） | 1719 |
| L162-164 | `tx_brake != 0` → CVCCOM_DIAG（SIL 关闭时空转） | true 侧：SAFE_STOP/SHUTDOWN 场景（406 次） | 406 |
| L165-166 | `Com_SendSignal` SteerAngleCmd / BrakeForceCmd | 全部已初始化场景 | 1719 |
| L171-179 | E-Stop 广播桥（Active/Source） | `estop_active_mask01`、`all_faults_maskff`（Active=1）；其余（Active=0）；Source 恒为 1 | 1719 |
| L184-196 | Body_Control_Cmd 0x350 全 0 信号 | 全部已初始化场景 | 1719 |
| L203-212 | Torque_Request 桥（钳位） | `torque_clamped_100`（true 侧 2 次）；`torque_passthrough_60` 及全部（false 侧） | 1719 |

#### Swc_CvcCom_BridgeRxToRte（L222-307）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L223-225 | 函数入口 + 局部变量 | 全部 bridgeRx 场景 + uninitialized_rx_noop | 10 |
| L227-230 | `if (CvcCom_Initialized != TRUE)` 守卫 + return | `uninitialized_rx_noop`（skipInit=true + bridgeRx=true，true 侧 2 次）+ RX 桥接场景（false 侧 8 次） | 10 |
| L232-238 | 默认值（SC 继电器 0x80、电池 2、转向/电机 0） | 全部 bridgeRx=true 场景 | 8 |
| L252-258 | 制动故障双源合并 `(bf_event≠0)?bf_event:bf_status` | true 侧：`rx_bridge_brake_event`（事件帧优先）；false 侧：`rx_bridge_brake_status`（周期状态兜底） | 8 |
| L259 | Motor_Cutoff_Req 读取 | `rx_bridge_all` | 8 |
| L271-277 | SC_Status.RelayEnergized → Rte_Write(SC_RELAY_KILL) | `rx_bridge_all`（rxScRelay=0 → 0）；`rx_bridge_brake_status/event`（rxScRelay=1 → 1） | 8 |
| L278 | Battery_Status.Level 读取 | `rx_bridge_all`（rxBattery=1） | 8 |
| L279 | Steering_Status.SteerFault 读取 | `rx_bridge_all`（rxSteerFault=1） | 8 |
| L280 | Motor_Status.MotorFault 读取 | `rx_bridge_all`（rxMotorFault=1） | 8 |
| L285-292 | FZC/RZC 心跳 E2E 存活计数器桥接 | `rx_bridge_all`（rxFzcAlive=5、rxRzcAlive=9） | 8 |
| L297-302 | 故障信号 Rte_Write（brake/motorCutoff/battery/steer/motorRzc） | 全部 bridgeRx=true 场景 | 8 |
| L307 | 函数结束 | 全部 bridgeRx=true 场景 | 8 |

> SIL_DIAG 诊断块（L260-270）为 `#ifdef SIL_DIAG` 编译期排除，harness 不定义该宏，
> 该块不计入可执行行（生产固件同样不启用）。

---

## 分支覆盖分析（100%，34/34）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `CvcCom_Initialized != TRUE`（TX） | L76 | ✅ 两侧 | `uninitialized_tx_noop` 覆盖 true 侧，其余场景覆盖 false 侧 |
| `for (j<8)` | L99 | ✅ 两侧 | 循环体 + 循环退出 |
| `estop != 0` | L106 | ✅ 两侧 | 激活/未激活 |
| `relayKill == 0` | L108 | ✅ 两侧 | 切断/吸合 |
| `motorCutoff != 0` | L110 | ✅ 两侧 | 有/无故障 |
| `brakeFault != 0` | L112 | ✅ 两侧 | 有/无故障 |
| `steerFault != 0` | L114 | ✅ 两侧 | 有/无故障 |
| `pedalFault != 0` | L116 | ✅ 两侧 | 有/无故障 |
| `fzcComm == TIMEOUT` | L118 | ✅ 两侧 | 超时/正常 |
| `rzcComm == TIMEOUT` | L120 | ✅ 两侧 | 超时/正常 |
| `torque > 100` | L128 | ✅ 两侧 | 超限/正常 |
| `vs >= SAFE_STOP` | L153 | ✅ 两侧 | 安全停止/正常运行 |
| `tx_brake != 0` | L162 | ✅ 两侧 | 最大制动/正常 |
| `estop_val != 0`（广播三元） | L176 | ✅ 两侧 | 激活/未激活 |
| `torque_val > 100` | L207 | ✅ 两侧 | 超限/正常 |
| `CvcCom_Initialized != TRUE`（RX） | L227 | ✅ 两侧 | `uninitialized_rx_noop` 覆盖 true 侧，RX 桥接场景覆盖 false 侧 |
| `bf_event != 0`（制动双源三元） | L257 | ✅ 两侧 | 事件帧优先/周期状态兜底 |

> 全部 17 个分支点两侧均已覆盖，无无法覆盖的分支。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（146/146） | 0 行 | — |
| 分支 | 100%（34/34） | 0 个 | — |
| 函数 | 100%（3/3） | — | — |
