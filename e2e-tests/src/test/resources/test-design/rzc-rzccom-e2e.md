# RZC CAN 通信 (Swc_RzcCom) E2E 测试设计

## 被测功能

**RZC ASW CAN 通信 SWC — E2E 发送保护（CRC-8 0x1D + 4-bit alive 计数器）、
E2E 接收校验（CRC 校验 + alive 单调检查，3 次连续失败 → 扭矩安全默认值 0）、
RX 周期处理（0x001 E-stop 广播、0x100 Vehicle_State + Torque、100ms 扭矩超时）、
TX 周期调度（0x012 心跳、0x300 电机状态、0x301 电机电流、0x302 电机温度、
0x303 电池状态）**

覆盖链路：

```text
测试 API 注入（E2E 缓冲区 / RTE 信号）
  → Swc_RzcCom_Init()（16 槽 alive 计数器、失败计数、超时清零）
  → Swc_RzcCom_E2eProtect（TX：dataId 选择 → alive 写入 byte1 低半字节
                          → CRC-8 写入 byte0 → alive 递增回绕）
  → Swc_RzcCom_E2eCheck（RX：CRC 校验 → alive 单调检查
                          → 失败计数 / 重同步 / 清零）
  → Swc_RzcCom_Receive（10ms 周期：未初始化守卫 → E-stop 直通
                          → E2E 失败≥3 → 扭矩=0 + DEM CAN_BUS_OFF
                          → 扭矩变更检测 → 100ms 超时强制扭矩=0）
  → Swc_RzcCom_TransmitSchedule（10ms 周期：TxScheduleCycle 递增回绕 1000
                          → 心跳 fault_status 组合 → 电机状态 5 信号
                          → 电机电流 5 信号 → cycle%10==3 电机温度
                          → cycle%20==7 电池状态）
```

与既有 ASW E2E（RZC `Swc_Motor`/`Swc_Battery`/`Swc_TempMonitor`、CVC
`Swc_CvcCom`）一致，本测试通过测试专用 API 在原生测试框架内执行真实的
`Swc_RzcCom.c` 生产代码。E2E 数据缓冲区与 RTE 信号经 mock 注入，输出经
RTE / Com / DEM mock 观察。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_RzcCom_Init              │
│ Tx/RxAlive[16]=0             │
│ RxFailCount[16]=0            │
│ TorqueTimeout=0 / LastTorque=0│
│ TxScheduleCycle=0 / Init=TRUE│
└─────────────┬────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Swc_RzcCom_E2eProtect(pduId, │
│   data, length)              │
└─────────────┬────────────────┘
              │
 data==NULL 或 length<2? ──Y──→ return E_NOT_OK
              │N
 pduId >= 16? ──────────Y──→ return E_NOT_OK
              │N
 dataId = GetTxDataId(pduId)     [TX: HB=0x04 MS=0x0E MC=0x0F MT=0x00 BAT=0x13]
 alive = TxAlive[pduId]
 data[1] = (data[1]&0xF0)|alive  [写入 alive 到 byte1 低半字节]
 data[0] = Crc8(&data[1],len-1,dataId)  [CRC-8 多项式 0x1D 写入 byte0]
 alive++ ; >15 → 0              [回绕]
 TxAlive[pduId] = alive
 return E_OK

┌──────────────────────────────┐
│ Swc_RzcCom_E2eCheck(pduId,   │
│   data, length)              │
└─────────────┬────────────────┘
              │
 data==NULL 或 length<2? ──Y──→ return E_NOT_OK
              │N
 pduId >= 16? ──────────Y──→ return E_NOT_OK
              │N
 dataId = GetTxDataId(pduId); ==0? → GetRxDataId(pduId)
                                 [RX: ESTOP=0x01 VEHSTATE=0x05]
 rx_crc = data[0]
 calc   = Crc8(&data[1],len-1,dataId)
 rx_crc != calc? ──Y──→ RxFailCount[pduId]++ ; return E_NOT_OK
              │N
 rx_alive = data[1] & 0x0F
 expected = RxAlive[pduId]+1; >15 → 0
 rx_alive != expected? ──Y──→ RxFailCount[pduId]++
                               RxAlive[pduId] = rx_alive (重同步)
                               return E_NOT_OK
              │N
 RxAlive[pduId]=rx_alive; RxFailCount[pduId]=0
 return E_OK

┌──────────────────────────────┐
│ Swc_RzcCom_Receive (10ms)    │
└─────────────┬────────────────┘
              │
 Init != TRUE? ──────────Y──→ return（未初始化空转）
              │N
 Swc_RzcSafety_NotifyCanRx()
 estop = Rte_Read(ESTOP_ACTIVE); !=0? ──Y──→ Rte_Write(ESTOP_ACTIVE,1)
 RxFailCount[VEHICLE_TORQUE] >= 3? ──Y──→ Rte_Write(TORQUE_CMD,0)
                                          Dem_Report(CAN_BUS_OFF,FAILED)
                                          return
              │N
 torque = Rte_Read(TORQUE_CMD)
 torque != LastTorqueRaw? ──Y──→ new=TRUE; LastTorqueRaw=torque; Timeout=0
              │N
 Timeout < 0xFFFF? ──Y──→ Timeout++
 Timeout >= RZCCOM_TORQUE_TIMEOUT(10)? ──Y──→ Rte_Write(TORQUE_CMD,0)

┌──────────────────────────────┐
│ Swc_RzcCom_TransmitSchedule  │
└─────────────┬────────────────┘
              │
 Init != TRUE? ──────────Y──→ return（未初始化空转）
              │N
 TxScheduleCycle++ ; >=1000 → 0
 心跳: state_fault=((faultMask&0x0F)<<4)|(vehState&0x0F)
       Com_Send(HB_ECU_ID=RZC_ECU_ID=3, HB_FAULT_STATUS=state_fault)
 电机状态: 5×Com_Send(torqueEcho,speed,dir,enable,fault)
 电机电流: dirReverse=(dir==2); enable/overcurrent 归一化
       Com_Send(curMa, dirReverse, enable, overcurrent, torqueEcho&0xFF)
 cycle%10==3? ──Y──→ 电机温度 3×Com_Send(temp1,temp2,derating)
 cycle%20==7? ──Y──→ 电池状态 2×Com_Send(battMv,battLevel)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 要执行的 Swc_RzcCom 动作 | `init` / `e2eProtect` / `e2eCheck` / `receive` / `tx` | When — 执行控制 |
| `pduId` | E2E PDU 索引 | TX: 0..4（各有 dataId）、15（未知→默认 0x00）、16+（越界）<br>RX: 0=E-stop、7=Vehicle_Torque | When — 数据注入 |
| `data` | E2E 8 字节缓冲区（hex） | 合法 hex 串 / `null`（NULL_PTR） | When — 数据注入 |
| `length` | E2E 载荷长度 | `1`（<2 非法）、`8`（合法） | When — 数据注入 |
| `skipInit` | 跳过 Swc_RzcCom_Init | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | receive/tx 周期调用次数 | `1`、`10`/`11`（超时边界）、`3`（温度档）、`7`（电池档） | When — 执行控制 |
| `estop` | RTE RZC_SIG_ESTOP_ACTIVE | `0`、`1` | When — 数据注入 |
| `vehicleState` | RTE 车辆状态 | `1`(RUN)、`4`(SAFE_STOP) | When — 数据注入 |
| `torqueCmd` | RTE 扭矩指令 | `0`、`50`、`60`（新指令） | When — 数据注入 |
| `faultMask` | RTE 故障掩码 | `0x00`、`0x0A` | When — 数据注入 |
| `torqueEcho` | RTE 扭矩回读 | `0`、`42` | When — 数据注入 |
| `speedRpm` | RTE 编码器转速 | `0`、`100` | When — 数据注入 |
| `motorDir` | RTE 电机方向 | `0`(FWD)、`1`(REV)、`2`(STOP→reverse 位) | When — 数据注入 |
| `motorEnable` | RTE 使能 | `0`、`1` | When — 数据注入 |
| `motorFault` | RTE 电机故障 | `0`、`1` | When — 数据注入 |
| `currentMa` | RTE 相电流 | `0`、`500` | When — 数据注入 |
| `overcurrent` | RTE 过流标志 | `0`、`1` | When — 数据注入 |
| `temp1Dc`/`temp2Dc` | RTE 绕组温度 1/2 | `0`、`250`、`350` | When — 数据注入 |
| `deratingPct` | RTE 降额百分比 | `100`、`75` | When — 数据注入 |
| `batteryMv` | RTE 电池电压 | `0`、`12000` | When — 数据注入 |
| `batteryStatus` | RTE 电池状态 | `0`、`2` | When — 数据注入 |

> E2E alive 计数器是**模块内部状态**：TX alive 由 e2eProtect 递增（首调用 0），
> RX alive 由 e2eCheck 维护（初始 0，期望下一个 = 当前+1，回绕 15→0）。测试
> 通过多次 e2eProtect / e2eCheck 操作驱动其取值，而非直接注入。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[]` | 每阶段动作结果数组 | 见各用例 |
| `results[i].ret` | e2eProtect/e2eCheck 返回码 | `0`=E_OK、`1`=E_NOT_OK |
| `results[i].data` | e2eProtect 后 8 字节缓冲区（hex） | `data[0]`=CRC、`data[1]` 低半字节=alive |
| `torqueCmd` | RTE RZC_SIG_TORQUE_CMD | 扭矩值或超时/失败后的 `0` |
| `estopActive` | RTE RZC_SIG_ESTOP_ACTIVE | `0`/`1` |
| `demBusOff` | `Dem_ReportErrorStatus` RZC_DTC_CAN_BUS_OFF | `1`=FAILED、`-1`=未报告 |
| `hbEcuId` | Com 心跳 ECU ID | `3`（RZC_ECU_ID） |
| `hbFaultStatus` | Com 心跳故障状态 | `((faultMask&0x0F)<<4)|(vehState&0x0F)` |
| `mstat*` | Com 电机状态 5 信号 | 回读扭矩/转速/方向/使能/故障 |
| `cur*` | Com 电机电流 5 信号 | 相电流/reverse/使能/过流/扭矩回读 |
| `temp1C`/`temp2C`/`deratingPct` | Com 电机温度 3 信号 | cycle%10==3 时写入 |
| `batteryMv`/`batteryLevel` | Com 电池状态 2 信号 | cycle%20==7 时写入 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: E2E 发送保护**：合法 PDU 的 CRC/alive、alive 递增回绕、非法参数、
>   各 TX dataId 映射，共 8 场景。
> - **规则: E2E 接收校验**：合法帧通过、CRC 损坏拒绝、alive 重放拒绝、RX dataId
>   回退（含默认 0x00）、alive 回绕、E-stop dataId 缺陷暴露、非法参数，共 8 场景。
> - **规则: RX 周期处理**：未初始化 / E-stop / E2E 失败安全默认 / 扭矩超时与
>   0xFFFF 饱和，共 9 场景。
> - **规则: TX 周期调度**：未初始化 / 心跳与电机状态 / 电流方向与过流 /
>   温度与电池定时发射 / 周期回绕，共 7 场景。
>
> 每个用例经 `POST /api/test/asw/rzc/rzccom` 一次运行驱动真实
> `Swc_RzcCom.c`；同一 POST 内的多个阶段顺序执行、模块状态（alive 计数器、
> 失败计数、TorqueTimeout、TxScheduleCycle）跨阶段保留。

### 规则: E2E 发送保护

| 用例 | 阶段序列 | 期望 results[i].ret | 期望 results[i].data（byte0=CRC, byte1 低半=alive） |
|---|---|---|---|
| protect_heartbeat_pdu0 | P0: e2eProtect pduId=0 data=0000000000000000 | 0 | `6900000000000000` |
| protect_motor_status_pdu1 | P0: e2eProtect pduId=1 data=0000000000000000 | 0 | `0100000000000000` |
| protect_motor_current_pdu2 | P0: e2eProtect pduId=2 data=0000000000000000 | 0 | `5c00000000000000` |
| protect_battery_pdu4 | P0: e2eProtect pduId=4 data=0000000000000000 | 0 | `5e00000000000000` |
| protect_alive_increments | P0: e2eProtect pduId=0 ×1<br>P1: e2eProtect pduId=0 ×1 | 0, 0 | `6900000000000000`, `3401000000000000` |
| protect_alive_wraps_at_15 | P0..P15: e2eProtect pduId=0 ×16<br>P16: e2eProtect pduId=0 ×1 | 全部 0 | 第 16 次 alive=15（`..0f..`）、第 17 次回绕 alive=0 |
| protect_rejects_invalid | P0: e2eProtect pduId=0 data=null<br>P1: e2eProtect pduId=0 len=1<br>P2: e2eProtect pduId=16 | 1, 1, 1 | — |

### 规则: E2E 接收校验

| 用例 | 阶段序列 | 期望 results[i].ret |
|---|---|---|
| check_valid_after_two_protects | P0: e2eProtect pduId=0（alive=0）<br>P1: e2eProtect pduId=0（alive=1）<br>P2: e2eCheck pduId=0 data=3401000000000000 | 0, 0, 0 |
| check_rejects_corrupt_crc | P0: e2eCheck pduId=0 data=0001000000000000 | 1 |
| check_rejects_alive_replay | P0: e2eProtect pduId=0 ×2<br>P1: e2eCheck pduId=0 data=3401000000000000（alive=1 通过）<br>P2: e2eCheck pduId=0 data=3401000000000000（重放，期望 2） | 0, 0, 1 |
| check_valid_vehicle_torque_pdu | P0: e2eCheck pduId=7 data=6901000000000000（dataId=0x05, alive=1） | 0 |
| check_rejects_invalid | P0: e2eCheck pduId=0 data=null<br>P1: e2eCheck pduId=0 len=1<br>P2: e2eCheck pduId=16 | 1, 1, 1 |

### 规则: RX 周期处理

| 用例 | 阶段序列 | 期望 torqueCmd | 期望 estopActive | 期望 demBusOff |
|---|---|---|---|---|
| receive_uninitialized_noop | P0: receive estop=0 torqueCmd=50 cycles=1 skipInit=true | 50 | 0 | -1 |
| receive_estop_active | P0: receive estop=1 torqueCmd=50 cycles=1 | 50 | 1 | -1 |
| receive_estop_inactive | P0: receive estop=0 torqueCmd=50 cycles=1 | 50 | 0 | -1 |
| receive_e2e_fail_closed | P0-P2: e2eCheck pduId=7 data=0000000000000000 ×3<br>P3: receive estop=0 torqueCmd=50 cycles=1 | 0 | 0 | 1 |
| receive_e2e_under_threshold | P0-P1: e2eCheck pduId=7 data=0000000000000000 ×2<br>P2: receive estop=0 torqueCmd=50 cycles=1 | 50 | 0 | -1 |
| receive_torque_timeout | P0: receive estop=0 torqueCmd=50 cycles=11 | 0 | 0 | -1 |
| receive_torque_timeout_boundary | P0: receive estop=0 torqueCmd=50 cycles=10 | 50 | 0 | -1 |
| receive_new_torque_resets_timeout | P0: receive torqueCmd=50 cycles=6<br>P1: receive torqueCmd=60 cycles=1<br>P2: receive torqueCmd=60 cycles=6 | 60 | 0 | -1 |

### 规则: TX 周期调度

| 用例 | 阶段序列 | 期望 Com 信号 |
|---|---|---|
| tx_uninitialized_noop | P0: tx vehicleState=1 faultMask=0 torqueEcho=42 skipInit=true cycles=1 | hbEcuId=0, mstatTorqueEcho=0 |
| tx_heartbeat_motor_status | P0: tx vehicleState=1 faultMask=0 torqueEcho=42 speedRpm=100 motorDir=1 motorEnable=1 motorFault=0 currentMa=500 overcurrent=0 cycles=1 | hbEcuId=3, hbFaultStatus=1, mstatTorqueEcho=42, mstatSpeedRpm=100, mstatDirection=1, mstatEnable=1, mstatFault=0, curMa=500, curDirReverse=0 |
| tx_fault_status_composition | P0: tx vehicleState=4 faultMask=0x0A cycles=1 | hbFaultStatus=164 |
| tx_motor_current_reverse_overcurrent | P0: tx motorDir=2 overcurrent=1 motorEnable=1 cycles=1 | curDirReverse=1, curOvercurrent=1, curEnable=1 |
| tx_motor_temp_at_cycle3 | P0: tx temp1Dc=250 temp2Dc=350 deratingPct=75 cycles=3 | temp1C=250, temp2C=350, deratingPct=75 |
| tx_battery_at_cycle7 | P0: tx batteryMv=12000 batteryStatus=2 cycles=7 | batteryMv=12000, batteryLevel=2 |

> 温度/电池信号按 `TxScheduleCycle%10==3` / `%20==7` 定时发射：cycle 计数从 0
> 开始、每次 TransmitSchedule 调用前置递增，故 `cycles=3` 时第 3 次调用命中
> 温度档、`cycles=7` 时第 7 次调用命中电池档。

## 代码路径覆盖

- `RzcCom_Crc8` ✅
  - CRC 初始化=dataId、逐字节/逐位移位、`crc&0x80` 异或多项式双侧 ✅
- `RzcCom_GetTxDataId` ✅
  - TX 5 个 PDU 的 dataId 映射 + default 0x00 ✅（`protect_*` 各 PDU 用例）
- `RzcCom_GetRxDataId` ✅
  - ESTOP 0x01、VEHICLE_TORQUE 0x05、default 0x00 ✅（`check_valid_vehicle_torque_pdu`）
- `Swc_RzcCom_Init` ✅
  - 16 槽 alive/失败计数清零、超时/周期计数清零、Init=TRUE ✅
- `Swc_RzcCom_E2eProtect` ✅
  - NULL/short 拒绝、pduId 越界拒绝、dataId 选择、alive 写入、CRC 写入、递增回绕 ✅
- `Swc_RzcCom_E2eCheck` ✅
  - NULL/short 拒绝、pduId 越界拒绝、TX→RX dataId 回退、CRC 失败、alive 重放失败 + 重同步、成功清零 ✅
- `Swc_RzcCom_Receive` ✅
  - 未初始化守卫、NotifyCanRx、E-stop 直通、E2E 失败≥3 安全默认 + DEM、扭矩变更检测、超时清零、超时边界强制扭矩 0 ✅
- `Swc_RzcCom_TransmitSchedule` ✅
  - 未初始化守卫、cycle 回绕、心跳组合、电机状态、电机电流（reverse/过流）、温度/电池定时 ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_RzcCom.c.gcov.html` 实测（2026-08-16 全量套件 286 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **99.3%**（284 / 286 行） |
| **分支覆盖** | **98.7%**（75 / 76 分支） |
| **函数覆盖** | **100%**（8 / 8） |

覆盖到的函数（实测调用次数）：
`RzcCom_Crc8`（247）、`RzcCom_GetTxDataId`（247）、
`RzcCom_GetRxDataId`（41）、`Swc_RzcCom_Init`（171）、
`Swc_RzcCom_E2eProtect`（186）、`Swc_RzcCom_E2eCheck`（97）、
`Swc_RzcCom_Receive`（140234）、`Swc_RzcCom_TransmitSchedule`（5084）。

> 下表「实测命中」为全量套件（286 场景，含 254 个既有场景 + 32 个本 SWC
> 场景）一次干净运行后的累积值；每次容器重建后会重新累积，具体数字可能
> 不同，但覆盖关系不变。

---

## 行覆盖分析（99.3%，284/286）

行覆盖反映**每一行是否被执行**。2 行未覆盖，为**结构性不可达的防御性
代码**（见下方「未覆盖行说明」）。其余 284 行全部覆盖，逐行映射如下。

### 逐函数代码行覆盖映射

#### RzcCom_Crc8（L119-145）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L121-123 | 函数入口与局部变量 `crc`/`i`/`bit` | 全部 E2E 保护/校验用例（每次 CRC 计算） | 247 |
| L125 | `crc = dataId` | 同上 | 247 |
| L127 | `for (i < length)`（含 false 侧） | 全部长度 ≥2 的 E2E 用例（每次迭代） | 1976/1976 |
| L129 | `crc ^= data[i]` | 同上 | 1729 |
| L131 | `for (bit < 8)` | 同上（每字节 8 位） | 15561 |
| L133 | `if (crc & 0x80)`（双侧） | CRC 高位为 1（异或多项式）与为 0（仅移位）均出现 | 13832/13832 |
| L135 | `crc ^= RZCCOM_CRC8_POLY` | `crc&0x80` 为真的字节位（L134 命中 6051） | 6051 |
| L139 | `crc = crc << 1`（仅移位） | `crc&0x80` 为假的字节位（L137 命中 7781） | 7781 |
| L144 | `return crc` | 全部 CRC 计算 | 247 |

#### RzcCom_GetTxDataId（L156-173）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L158 | `switch (pduId)` | 全部 E2E 用例 | 247 |
| L160-161 | 心跳 → 0x04 | `protect_heartbeat_pdu0` 及后续 E2E | 183 |
| L162-163 | 电机状态 → 0x0E | `protect_motor_status_pdu1` | 6 |
| L164-165 | 电机电流 → 0x0F | `protect_motor_current_pdu2` | 6 |
| L166-167 | 电机温度 → 0x00 | `protect_motor_temp_pdu3` | 5 |
| L168-169 | 电池 → 0x13 | `protect_battery_pdu4` | 6 |
| L170-171 | 默认 → 0x00 | RX/未知 PDU 的 E2E 用例（pdu5/7 等） | 41 |

#### RzcCom_GetRxDataId（L175-186）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L177 | `switch (pduId)` | 仅当 `GetTxDataId` 返回 0x00 时进入 | 41 |
| L181-182 | Vehicle_Torque → 0x05 | `check_valid_vehicle_torque_pdu`（pdu7）及 3 次失败用例 | 36 |
| L183-184 | 默认 → 0x00 | `check_unknown_rx_pdu_default_dataid`（pdu5） | 5 |
| L179-180 | E-stop → 0x01 | **结构不可达，见下方说明** | 0 |

#### Swc_RzcCom_Init（L194-209）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L198-202 | 16 槽 TxAlive/RxAlive/RxFailCount 清零 | 每次 Init（每 POST 一次） | 171 |
| L205-207 | TorqueTimeout/HbCycleCount/TxScheduleCycle 清零 | 同上 | 171 |
| L208 | `RzcCom_Initialized = TRUE` | 同上 | 171 |

#### Swc_RzcCom_E2eProtect（L215-252）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L221-224 | `data==NULL || length<2` → E_NOT_OK | `protect_rejects_invalid`（data=null、len=1） | 12 |
| L226-229 | `pduId >= 16` → E_NOT_OK | `protect_rejects_invalid`（pduId=16） | 6 |
| L231-232 | dataId 选择 + 读取 alive | 全部合法 protect | 168 |
| L235 | alive 写入 byte1 低半字节 | 全部合法 protect | 168 |
| L238 | CRC 计算写入 | 同上 | 168 |
| L241 | CRC 写入 byte0 | 同上 | 168 |
| L245-248 | alive 递增回绕（>15→0） | `protect_alive_wraps_at_15`（16 次后回绕） | 6 |
| L249 | TxAlive 更新 | 全部合法 protect | 168 |
| L251 | `return E_OK` | 全部合法 protect | 168 |

#### Swc_RzcCom_E2eCheck（L258-317）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L266-269 | `data==NULL || length<2` → E_NOT_OK | `check_rejects_invalid`（data=null、len=1） | 12 |
| L271-274 | `pduId >= 16` → E_NOT_OK | `check_rejects_invalid`（pduId=16） | 6 |
| L276-280 | TX→RX dataId 回退（`dataId==0` 时查 GetRxDataId） | pdu5/pdu7 用例（GetTxDataId 返回 0） | 79/41 |
| L283 | 读取 rx_crc | 全部 E2E 校验 | 79 |
| L286 | 计算期望 CRC | 同上 | 79 |
| L288-292 | CRC 不匹配 → 失败计数 + E_NOT_OK | `check_crc_corrupt`、3 次失败、E-stop 帧被拒 | 40 |
| L295 | 提取 rx_alive | CRC 通过后的全部帧 | 39 |
| L298-303 | 期望 alive 计算 + 回绕（>15→0） | `check_alive_wrap_at_15`（alive=15 后回绕） | 39/5 |
| L305-310 | alive 不匹配 → 失败计数 + 重同步 + E_NOT_OK | `check_alive_replay`、`check_valid_after_two_protects` 第 1 帧、RX alive 回绕第 1 步 | 11 |
| L313-314 | 校验成功 → RxAlive 更新 + 失败计数清零 | 全部合法 E2E 校验 | 28 |
| L316 | `return E_OK` | 同上 | 28 |

#### Swc_RzcCom_Receive（L323-394）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L330-333 | 未初始化守卫 | `receive_uninitialized_noop`（skipInit=1） | 6 |
| L337 | `Swc_RzcSafety_NotifyCanRx()` | 全部已初始化 receive | 140228 |
| L340-341 | 读取 E-stop 信号 | 同上 | 140228 |
| L343-347 | E-stop 激活 → RTE 写入 1 | `receive_estop_active`（estop=1） | 6 |
| L351-357 | E2E 失败≥3 → 扭矩 0 + DEM CAN_BUS_OFF | `receive_e2e_fail_closed`（3 次失败） | 6 |
| L360-365 | 读取车辆状态与扭矩、`new_torque_received=FALSE` | 其余全部 receive | 140222 |
| L370-374 | 扭矩变更 → new 标记 + 更新 LastTorqueRaw | `receive_estop_*`、`receive_new_torque_resets_timeout` | 42 |
| L377-380 | 新扭矩 → 超时清零 | 同上 | 42 |
| L383-386 | 超时递增（<0xFFFF 时） | 扭矩不变的周期（超时边界/饱和用例） | 140180/131250 |
| L388-392 | 超时≥10 → 扭矩写 0 | `receive_torque_timeout`（11 周期）、饱和用例 | 139988 |
| L394 | 周期结束 | 全部已初始化 receive | 140222 |

#### Swc_RzcCom_TransmitSchedule（L400-542）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L402-405 | 未初始化守卫 | `tx_uninitialized_noop`（skipInit=1） | 6 |
| L408-412 | TxScheduleCycle 递增 + ≥1000 回绕 | `tx_cycle_wrap_at_1000`（1000 周期后回绕） | 5078/5 |
| L434-438 | 心跳：读取车辆状态/故障掩码 + 组合 state_fault + 2 信号 | 全部已初始化 tx | 5078 |
| L442-451 | 电机状态：读取 5 个 RTE 信号 | 同上 | 5078 |
| L453-464 | 电机状态：5 个 Com 信号 | 同上 | 5078 |
| L476-486 | 电机电流：读取电流/过流 + reverse 位 + 扭矩回读 | 同上 | 5078 |
| L488-497 | 电机电流：5 个 Com 信号 | `tx_motor_current_reverse_overcurrent`（reverse/过流） | 5078 |
| L502-519 | 电机温度：cycle%10==3 时读取 + 3 信号 | `tx_motor_temp_at_cycle3`（cycles=3） | 512 |
| L523-538 | 电池状态：cycle%20==7 时读取 + 2 信号 | `tx_battery_at_cycle7`（cycles=7） | 256 |

> 未列出的行号为注释、空行、声明或大括号占位（llvm-cov/lcov 不计入可执行行）。

---

## 未覆盖行说明（2 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L179 | `case RZC_COM_RX_ESTOP:` | **结构性不可达**。pduId 0 既是 TX 心跳（`RZC_COM_TX_HEARTBEAT=0`）又是 RX E-stop（`RZC_COM_RX_ESTOP=0`），两个 PDU 索引**冲突**。`Swc_RzcCom_E2eCheck` 先查 `RzcCom_GetTxDataId(pduId)`，而 `GetTxDataId(0)` 返回心跳 dataId `0x04`（非 0），因此 `if (dataId == 0x00u)` 回退分支永不触发，`GetRxDataId(0)` 的 E-stop 分支永远进不去 |
| L180 | `return RZC_E2E_ESTOP_DATA_ID;` | **结构性不可达**。同上 —— E-stop RX dataId 查找被 TX 心跳分支遮蔽 |

> 以上 2 行暴露了**生产代码缺陷**：RZC 对 pdu0 的 E2E 校验实际使用 TX 心跳
> dataId `0x04`，而 CVC 侧发送 E-stop 广播时按 `CVC_E2E_ESTOP_BROADCAST_DATA_ID=0x01`
> 保护帧。若 RZC 用 `Swc_RzcCom_E2eCheck(0, …)` 校验 CVC 的 E-stop 帧，会因 dataId
> 不匹配而**误判失败**（实测：`data=0001000000000000`（estop dataId 0x01 保护）被
> 拒绝，`data=3401000000000000`（心跳 dataId 0x04 保护）被接受）。
>
> 该缺陷已由 `pdu0 E2E 校验使用 TX 心跳 dataId 0x04（E-stop dataId 查找不可达）`
> 场景**暴露并固化**为回归断言（`results[0].ret: 1`）。修复建议：E2eCheck 对 RX
> PDU 应优先查 `GetRxDataId`，或为 RX E-stop 分配独立 PDU 索引。当前 `E2eCheck`
> 为测试专用直连 API，生产 RX 路径的 E2E 校验在 Com 层（Phase 2），故该缺陷
> 不影响当前已部署的 CAN 数据流，但需在 Com 层 E2E 启用时规避。

---

## 分支覆盖分析（98.7%，75/76）

未命中（not taken）的 1 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `GetRxDataId` 的 `case RZC_COM_RX_ESTOP`（true 侧） | L179 | 结构性不可达：pdu0 被 TX 心跳遮蔽，`GetTxDataId(0)` 返回非 0，`GetRxDataId` 的 E-stop 分支永不被选中 |

> 其余 75 个分支两侧全部覆盖：CRC-8 双路径、TX 5 个 dataId 映射、RX dataId
> 回退、alive 回绕与重同步、E2E 失败安全默认、扭矩超时（含 0xFFFF 饱和）、
> TX 周期回绕、温度/电池定时发射，均已由专门场景覆盖双侧。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 99.3%（284/286） | 2 行 | 均为 E-stop RX dataId 查找（pdu0 与 TX 心跳索引冲突，结构不可达 + 生产缺陷） |
| 分支 | 98.7%（75/76） | 1 个 | E-stop RX dataId 分支（同上，不可达） |
| 函数 | 100%（8/8） | — | — |

**结论**：`Swc_RzcCom` 的全部可执行逻辑（8 个函数、CRC-8、TX/RX dataId 映射、
alive 计数与回绕、E2E 失败安全默认、E-stop 直通、扭矩超时与 0xFFFF 饱和、
TX 心跳/电机状态/电流/温度/电池调度）均由 E2E 测试覆盖。2 行未覆盖代码为
「pdu0（E-stop）RX dataId 查找被 TX 心跳遮蔽」的结构性不可达分支，同时该
行为构成生产缺陷（RZC 侧 pdu0 E2E 校验使用心跳 dataId，与 CVC 侧 estop dataId
不匹配），已通过专用场景暴露固化，供后续修复参考。

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-16 | 初版设计文档（输入/输出因子、用例表、流程图） |
| 2026-08-16 | 新增 `rzc_rzccom.feature`（32 场景全部通过）、`rzc_rzccom_harness.c`、`/api/test/asw/rzc/rzccom` 测试 API；全量套件 286 场景通过；填写实测覆盖率（99.3% 行 / 98.7% 分支 / 100% 函数）、逐行映射、2 行结构不可达代码说明与 E-stop dataId 生产缺陷记录 |
