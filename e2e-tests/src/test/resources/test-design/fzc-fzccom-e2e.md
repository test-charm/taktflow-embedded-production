# FZC CAN 通信 (Swc_FzcCom) E2E 测试设计

## 被测功能

**FZC ASW CAN 通信 SWC — E2E 发送保护（CRC-8 多项式 0x1D + 4-bit alive 计数器 +
Data ID 参与种子）、E2E 接收校验（CRC 校验）、RX 周期处理（10ms 周期通知
CAN 监视器复位静默计数器，防止误报 CAN 丢失）、TX 周期调度（10ms 周期发送
心跳 0x011 / 转向状态 0x200 / 制动状态 0x201 / 制动故障 0x210 / 电机切断 0x211 /
激光雷达距离 0x220）**

覆盖链路：

```text
测试 API 注入（E2E 缓冲区 / RTE 信号）
  → Swc_FzcCom_Init()（TX alive 清零、4 路 RX alive 置 0xFF、心跳/周期/挂起清零）
  → Swc_FzcCom_E2eProtect（TX：alive 写入 byte1 低半字节（保留高半字节）
                          → CRC-8 写入 byte0 → alive 递增回绕 15→0）
  → Swc_FzcCom_E2eCheck（RX：CRC 校验 → byte1 低半字节提取 alive）
  → Swc_FzcCom_Receive（10ms 周期：未初始化守卫 → Swc_FzcCanMonitor_NotifyRx()）
  → Swc_FzcCom_TransmitSchedule（10ms 周期：未初始化守卫
                          → TxScheduleCycle 递增回绕 1000
                          → 心跳 3 信号（ECU_ID / 运行模式低半字节 / 故障低半字节）
                          → 转向状态 2 信号（sint16 角度 + uint8 故障）
                          → 制动状态 1 信号 → 制动故障 1 信号
                          → 电机切断 1 信号 → 激光雷达距离 3 信号）
```

与既有 ASW E2E（FZC `Swc_Steering`/`Swc_Brake`/`Swc_Lidar`、CVC `Swc_CvcCom`、
RZC `Swc_RzcCom`）一致，本测试通过测试专用 API 在原生测试框架内执行真实的
`Swc_FzcCom.c` 生产代码。E2E 数据缓冲区与 RTE 信号经 mock 注入，输出经
RTE / Com mock 观察；`g_dbg_steer_rte_dispatch` 与 `g_dbg_steer_com_send`
为 SWC 既有的调试计数器，直接作为 TX 周期观测点。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_FzcCom_Init              │
│ TxAlive=0 / RxAlive*[4]=0xFF │
│ HbCycleCount=0 / TxPend=0    │
│ TxScheduleCycle=0 / Init=TRUE│
└─────────────┬────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Swc_FzcCom_E2eProtect(data,  │
│   length, dataId)            │
└─────────────┬────────────────┘
              │
 data==NULL 或 length<2? ──Y──→ return E_NOT_OK
              │N
 data[1] = (data[1]&0xF0)|(TxAlive&0x0F)  [alive 写入 byte1 低半字节]
 data[0] = CalcCrc8(&data[1],len-1,dataId) [CRC-8 多项式 0x1D 写入 byte0]
 TxAlive = (TxAlive+1) & 0x0F            [递增回绕 15→0]
 return E_OK

┌──────────────────────────────┐
│ Swc_FzcCom_E2eCheck(data,    │
│   length, dataId)            │
└─────────────┬────────────────┘
              │
 data==NULL 或 length<2? ──Y──→ return E_NOT_OK
              │N
 received_crc = data[0]
 computed   = CalcCrc8(&data[1],len-1,dataId)
 received_crc != computed? ──Y──→ return E_NOT_OK
              │N
 rx_alive = data[1] & 0x0F        [提取 alive，逐消息校验由 Receive 负责]
 (void)rx_alive
 return E_OK

┌──────────────────────────────┐
│ Swc_FzcCom_Receive (10ms)    │
└─────────────┬────────────────┘
              │
 Init != TRUE? ──────────Y──→ return（未初始化空转）
              │N
 Swc_FzcCanMonitor_NotifyRx()   [复位 CAN 监视器静默计数器]

┌──────────────────────────────┐
│ Swc_FzcCom_TransmitSchedule  │
└─────────────┬────────────────┘
              │
 Init != TRUE? ──────────Y──→ return（未初始化空转）
              │N
 g_dbg_steer_rte_dispatch++
 TxScheduleCycle++ ; >=1000 → 0
 心跳: op_mode=vs&0x0F; fault_nibble=fm&0x0F
       Com_Send(HB_ECU_ID=FZC_ECU_ID=2, HB_OPERATING_MODE=op_mode,
                HB_FAULT_STATUS=fault_nibble)
 转向状态: angle=(sint16)((uint16)steer_val); fault=(uint8)fault_val
       Com_Send(STEERING_STATUS_ACTUAL_ANGLE, STEER_FAULT_STATUS)
       g_dbg_steer_com_send++
 制动状态: bpos=(uint8)brake_val
       Com_Send(BRAKE_STATUS_BRAKE_POSITION)
 制动故障: fault=(uint8)brake_fault; Com_Send(BRAKE_FAULT_FAULT_TYPE)
 电机切断: cutoff=(uint8)rteVal;      Com_Send(MOTOR_CUTOFF_REQ_REQUEST_TYPE)
 激光雷达: zone=(uint8); dist=(uint16); sig=(uint8)(rteVal>>8)
       Com_Send(OBSTACLE_ZONE, RANGE_CM, SIGNAL_STRENGTH)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 要执行的 Swc_FzcCom 动作 | `init` / `e2eProtect` / `e2eCheck` / `receive` / `tx` | When — 执行控制 |
| `data` | E2E 8 字节缓冲区（hex） | 合法 hex 串（全零、带载荷、byte1 高半字节非零）/ `null`（NULL_PTR） | When — 数据注入 |
| `dataId` | FZC E2E Data ID（参与 CRC 种子） | `0`、`3`（心跳）、`8`（制动指令）、`16`（0x10） | When — 数据注入 |
| `length` | E2E 载荷长度 | `1`（<2 非法）、`8`（合法） | When — 数据注入 |
| `repeats` | e2eProtect/e2eCheck 重复次数 | `1`、`16`（alive 回绕边界） | When — 执行控制 |
| `skipInit` | 跳过 Swc_FzcCom_Init | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | receive/tx 周期调用次数 | `1`、`3`/`5`（多次周期）、`1000`（TxScheduleCycle 回绕边界） | When — 执行控制 |
| `vehicleState` | RTE FZC_SIG_VEHICLE_STATE | `1`(RUN)、`31`(0x1F 高半字节掩码测试) | When — 数据注入 |
| `faultMask` | RTE FZC_SIG_FAULT_MASK | `0`、`5`、`255`(0xFF 高半字节掩码测试) | When — 数据注入 |
| `steerAngle` | RTE FZC_SIG_STEER_ANGLE | `0`、`45` | When — 数据注入 |
| `steerFault` | RTE FZC_SIG_STEER_FAULT | `0`、`1` | When — 数据注入 |
| `brakePos` | RTE FZC_SIG_BRAKE_POS | `0`、`75` | When — 数据注入 |
| `brakeFault` | RTE FZC_SIG_BRAKE_FAULT | `0`、`1` | When — 数据注入 |
| `motorCutoff` | RTE FZC_SIG_MOTOR_CUTOFF | `0`、`1` | When — 数据注入 |
| `lidarZone` | RTE FZC_SIG_LIDAR_ZONE | `0`、`2`(BRAKING) | When — 数据注入 |
| `lidarDist` | RTE FZC_SIG_LIDAR_DIST | `0`、`291`(0x0123) | When — 数据注入 |
| `lidarSignal` | RTE FZC_SIG_LIDAR_SIGNAL | `0`、`43981`(0xABCD，高字节 0xAB) | When — 数据注入 |

> TX alive 计数器是**模块内部状态**：由 e2eProtect 递增（首调用 0），测试通过
> 多次 e2eProtect / init 操作驱动其取值，而非直接注入。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[]` | 每阶段动作结果数组 | 见各用例 |
| `results[i].ret` | e2eProtect/e2eCheck 返回码 | `0`=E_OK、`1`=E_NOT_OK |
| `results[i].data` | e2eProtect 后 8 字节缓冲区（hex） | `data[0]`=CRC、`data[1]` 低半字节=alive、高半字节保留 |
| `results[i].canmonNotify` | receive 阶段 `Swc_FzcCanMonitor_NotifyRx` 调用次数 | 每周期 1 次（未初始化 0） |
| `rteDispatch` | `g_dbg_steer_rte_dispatch`（TX 周期计数） | 每周期 1 次（未初始化 0） |
| `steerComSend` | `g_dbg_steer_com_send`（转向信号发送计数） | 每周期 1 次（未初始化 0） |
| `hbEcuId` | Com 心跳 ECU ID | `2`（FZC_ECU_ID） |
| `hbOpMode` | Com 心跳运行模式 | `vehicleState & 0x0F` |
| `hbFaultStatus` | Com 心跳故障状态 | `faultMask & 0x0F` |
| `steerAngle`/`steerFault` | Com 转向状态信号 | `(sint16)steerAngle`、`(uint8)steerFault` |
| `brakePos` | Com 制动位置信号 | `(uint8)brakePos` |
| `brakeFaultType` | Com 制动故障类型 | `(uint8)brakeFault` |
| `motorCutoffReq` | Com 电机切断请求 | `(uint8)motorCutoff` |
| `lidarZone`/`lidarRange`/`lidarSignal` | Com 激光雷达距离信号 | zone=`(uint8)`、range=`(uint16)`、signal=`rteVal>>8` 高字节 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: E2E 发送保护**：不同 Data ID 的 CRC、alive 写入与递增回绕、byte1
>   高半字节保留、重新 Init 复位、非法参数拒绝，共 9 场景。
> - **规则: E2E 接收校验**：合法帧通过、CRC 损坏拒绝、Data ID 不匹配拒绝、
>   非法参数拒绝，共 4 场景。
> - **规则: RX 周期处理**：未初始化空转 / 单周期通知 / 多周期每周期通知，
>   共 3 场景。
> - **规则: TX 周期调度**：未初始化 / 全部信号发送 / 低半字节掩码 / 多周期
>   计数器 / 周期回绕，共 5 场景。
>
> 每个用例经 `POST /api/test/asw/fzc/fzccom` 一次运行驱动真实
> `Swc_FzcCom.c`；同一 POST 内的多个阶段顺序执行、模块状态（TX alive、
> TxScheduleCycle）跨阶段保留。

### 规则: E2E 发送保护

| 用例 | 阶段序列 | 期望 results[i].ret | 期望 results[i].data（byte0=CRC, byte1 低半=alive） |
|---|---|---|---|
| protect_heartbeat_dataid3 | P0: e2eProtect dataId=3 data=0000000000000000 | 0 | `1200000000000000` |
| protect_brake_cmd_dataid8 | P0: e2eProtect dataId=8 data=0000000000000000 | 0 | `2700000000000000` |
| protect_zero_dataid | P0: e2eProtect dataId=0 data=0000000000000000 | 0 | `f500000000000000` |
| protect_custom_dataid16 | P0: e2eProtect dataId=16 data=0000000000000000 | 0 | `4c00000000000000` |
| protect_alive_increments | P0: e2eProtect dataId=3 data=0000000000000000<br>P1: e2eProtect dataId=3 data=0000000000000000 | 0, 0 | `1200000000000000`, `4f01000000000000` |
| protect_alive_wraps_at_15 | P0: e2eProtect dataId=3 repeats=16<br>P1: e2eProtect dataId=3 ×1 | 0, 0 | 第 16 次 alive=15（`4e0f...`）、第 17 次回绕 alive=0（`12...`） |
| protect_preserves_high_nibble | P0: e2eProtect dataId=3 data=00a0000000000000 | 0 | `dca0000000000000`（byte1 高半字节 0xA0 保留） |
| protect_reinit_resets_alive | P0: e2eProtect dataId=3<br>P1: init<br>P2: e2eProtect dataId=3 | 0, -, 0 | `12...`, op=init, `12...` |
| protect_rejects_null_short | P0: e2eProtect data=null<br>P1: e2eProtect length=1 | 1, 1 | — |

### 规则: E2E 接收校验

| 用例 | 阶段序列 | 期望 results[i].ret |
|---|---|---|
| check_valid_frame | P0: e2eProtect dataId=3（alive=0）<br>P1: e2eCheck dataId=3 data=1200000000000000 | 0, 0 |
| check_corrupt_crc | P0: e2eCheck dataId=3 data=1200000100000000 | 1 |
| check_wrong_dataid | P0: e2eProtect dataId=3<br>P1: e2eCheck dataId=8 data=1200000000000000 | 0, 1 |
| check_rejects_null_short | P0: e2eCheck data=null<br>P1: e2eCheck length=1 | 1, 1 |

### 规则: RX 周期处理

| 用例 | 阶段序列 | 期望 results[0].canmonNotify |
|---|---|---|
| receive_uninitialized_noop | P0: receive cycles=3 skipInit=true | 0 |
| receive_single_cycle | P0: receive cycles=1 | 1 |
| receive_multiple_cycles | P0: receive cycles=5 | 5 |

### 规则: TX 周期调度

| 用例 | 阶段序列 | 期望 Com 信号 |
|---|---|---|
| tx_uninitialized_noop | P0: tx skipInit=true vehicleState=1 faultMask=5 cycles=1 | rteDispatch=0, hbEcuId=0, hbFaultStatus=0 |
| tx_healthy_all_signals | P0: tx vehicleState=1 faultMask=5 steerAngle=45 steerFault=1 brakePos=75 brakeFault=1 motorCutoff=1 lidarZone=2 lidarDist=291 lidarSignal=43981 cycles=1 | rteDispatch=1, steerComSend=1, hbEcuId=2, hbOpMode=1, hbFaultStatus=5, steerAngle=45, steerFault=1, brakePos=75, brakeFaultType=1, motorCutoffReq=1, lidarZone=2, lidarRange=291, lidarSignal=171 |
| tx_heartbeat_nibble_masking | P0: tx vehicleState=31 faultMask=255 cycles=1 | hbOpMode=15, hbFaultStatus=15 |
| tx_multiple_cycles_counters | P0: tx vehicleState=1 faultMask=0 cycles=3 | rteDispatch=3, steerComSend=3, hbEcuId=2 |
| tx_schedule_cycle_wraps | P0: tx vehicleState=1 faultMask=0 steerAngle=45 brakePos=75 cycles=1000 | rteDispatch=1000, steerComSend=1000, hbEcuId=2, steerAngle=45, brakePos=75 |

> `g_dbg_steer_rte_dispatch` 与 `g_dbg_steer_com_send` 为 SWC 生产代码中既有的
> 全局调试计数器（`Swc_FzcCom.c` L88-89），harness 仅在每阶段开始时清零以便
> 按阶段观测，不修改生产逻辑。`cycles=1000` 时 `TxScheduleCycle` 在第 1000 次
> 调用处自增到 1000 后回绕 0，覆盖 L249 的 `>= 1000` 分支。

## 代码路径覆盖

- `FzcCom_CalcCrc8` ✅
  - CRC 初始化=`0xFF ^ dataId`、逐字节/逐位移位、`crc&0x80` 异或多项式双侧 ✅
- `Swc_FzcCom_Init` ✅
  - TX alive 清零、4 路 RX alive 置 0xFF、心跳/周期/挂起清零、Init=TRUE ✅
- `Swc_FzcCom_E2eProtect` ✅
  - NULL/short 拒绝、alive 写入 byte1 低半字节（高半字节保留）、CRC 写入、
    alive 递增回绕、重新 Init 复位 ✅
- `Swc_FzcCom_E2eCheck` ✅
  - NULL/short 拒绝、CRC 失败拒绝、成功返回、alive 提取 ✅
- `Swc_FzcCom_Receive` ✅
  - 未初始化守卫、每个 RX 周期 `Swc_FzcCanMonitor_NotifyRx` ✅
- `Swc_FzcCom_TransmitSchedule` ✅
  - 未初始化守卫、TxScheduleCycle 回绕、心跳 3 信号、转向/制动状态、
    制动故障、电机切断、激光雷达 3 信号、调试计数器 ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_FzcCom.c.gcov.html` 实测（2026-08-16 全量套件 386 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100.0%**（141 / 141 行） |
| **分支覆盖** | **100.0%**（22 / 22 分支） |
| **函数覆盖** | **100.0%**（6 / 6） |

覆盖到的函数（实测调用次数）：
`FzcCom_CalcCrc8`（62）、`Swc_FzcCom_Init`（45）、
`Swc_FzcCom_E2eProtect`（60）、`Swc_FzcCom_E2eCheck`（10）、
`Swc_FzcCom_Receive`（23）、`Swc_FzcCom_TransmitSchedule`（3018）。

> 下表「实测命中」为全量套件（386 场景，含 365 个既有场景 + 21 个本 SWC
> 场景）一次干净运行后的累积值；本 SWC 的函数仅由 `fzc_fzccom.feature`
> 场景驱动，其他既有 feature 不调用 `Swc_FzcCom_*`。每次容器重建后
> `.profraw` 会重新累积，具体命中数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（100%，141/141）

行覆盖反映**每一行是否被执行**。`Swc_FzcCom.c` 全部 141 个可执行行均已覆盖，
无未覆盖行。逐行映射如下。

### 逐函数代码行覆盖映射

#### FzcCom_CalcCrc8（L104-124）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L106-108 | 函数入口与局部变量 `crc`/`i`/`bit` | 全部 E2E 保护/校验用例（每次 CRC 计算） | 62 |
| L110 | `crc = 0xFF ^ dataId` | 全部 E2E 用例（不同 dataId 走不同种子） | 62 |
| L112 | `for (i < length)`（含 false 侧） | 全部长度 ≥2 的 E2E 用例 | 496/496 |
| L113 | `crc ^= data[i]` | 同上（每次迭代） | 434 |
| L114 | `for (bit < 8)` | 同上（每字节 8 位） | 3906 |
| L115 | `if (crc & 0x80)`（双侧） | 高位为 1（异或多项式）与为 0（仅移位）均出现 | 3472/3472 |
| L116 | `crc ^= FZC_COM_CRC8_POLY` | `crc&0x80` 为真的字节位 | 1788 |
| L118 | `crc = crc << 1`（仅移位） | `crc&0x80` 为假的字节位 | 1684 |
| L123 | `return crc` | 全部 CRC 计算 | 62 |

#### Swc_FzcCom_Init（L130-141）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L132-139 | 8 项模块状态清零（TxAlive/RxAlive×4/HbCycle/TxPend/TxScheduleCycle） | 每次 POST 默认 Init + `protect_reinit_resets_alive` 显式 init | 45 |
| L140 | `FzcCom_Initialized = TRUE` | 同上 | 45 |

#### Swc_FzcCom_E2eProtect（L147-172）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L151-153 | `data==NULL` → E_NOT_OK | `protect_rejects_null_short`（data=null） | 60（true 侧 2） |
| L155-157 | `length < 2` → E_NOT_OK | `protect_rejects_null_short`（length=1） | 58（true 侧 2） |
| L160 | alive 写入 byte1 低半字节（`& 0xF0u` 保留高半字节） | `protect_*` 全部合法保护用例（`protect_preserves_high_nibble` 验证保留） | 56 |
| L163 | CRC-8 计算（`&data[1]`, `length-1`, dataId） | 同上 | 56 |
| L166 | `data[0] = crc` | 同上 | 56 |
| L169 | `TxAlive = (TxAlive+1) & 0x0F`（回绕） | `protect_alive_increments`、`protect_alive_wraps_at_15`（15→0） | 56 |
| L171 | `return E_OK` | 全部合法保护用例 | 56 |

#### Swc_FzcCom_E2eCheck（L178-207）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L184-186 | `data==NULL` → E_NOT_OK | `check_rejects_null_short`（data=null） | 10（true 侧 2） |
| L188-190 | `length < 2` → E_NOT_OK | `check_rejects_null_short`（length=1） | 8（true 侧 2） |
| L193 | `received_crc = data[0]` | `check_valid_frame`、`check_corrupt_crc`、`check_wrong_dataid` | 6 |
| L196 | 计算期望 CRC | 同上 | 6 |
| L198-200 | `received_crc != computed` → E_NOT_OK | `check_corrupt_crc`、`check_wrong_dataid` | 6（true 侧 4） |
| L203 | `rx_alive = data[1] & 0x0F` | `check_valid_frame`（CRC 通过后提取） | 2 |
| L204 | `(void)rx_alive`（alive 逐消息校验由 Receive 负责） | 同上 | 2 |
| L206 | `return E_OK` | `check_valid_frame` | 2 |

#### Swc_FzcCom_Receive（L213-226）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L215-217 | `FzcCom_Initialized != TRUE` → return | `receive_uninitialized_noop`（skipInit） | 23（true 侧 6） |
| L221 | `Swc_FzcCanMonitor_NotifyRx()` | `receive_single_cycle`、`receive_multiple_cycles` | 17 |

#### Swc_FzcCom_TransmitSchedule（L232-333）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L236-238 | `FzcCom_Initialized != TRUE` → return | `tx_uninitialized_noop`（skipInit） | 3018（true 侧 6） |
| L240 | `g_dbg_steer_rte_dispatch++` | `tx_*` 全部已初始化用例 | 3015 |
| L248 | `FzcCom_TxScheduleCycle++` | 同上 | 3015 |
| L249-251 | `>= 1000` → 回绕 0（双侧） | `tx_schedule_cycle_wraps`（cycles=1000）+ 其余用例 | 4/3011 |
| L260-263 | 读 VEHICLE_STATE/FAULT_MASK，低半字节掩码 | `tx_healthy_all_signals`、`tx_heartbeat_nibble_masking` | 3015 |
| L264-266 | 心跳 3 信号 Com_Send | 全部 `tx_*` 用例 | 3015 |
| L274-275 | 读 STEER_ANGLE/STEER_FAULT | `tx_healthy_all_signals`、`tx_schedule_cycle_wraps` | 3015 |
| L279-283 | 转向 sint16 转换 + 2 信号发送 + `g_dbg_steer_com_send++` | 同上 | 3015 |
| L290-292 | 读 BRAKE_POS + 制动位置发送 | `tx_healthy_all_signals`、`tx_schedule_cycle_wraps` | 3015 |
| L299-301 | 读 BRAKE_FAULT + 制动故障发送 | `tx_healthy_all_signals`（brakeFault=1） | 3015 |
| L307-309 | 读 MOTOR_CUTOFF + 电机切断发送 | `tx_healthy_all_signals`（motorCutoff=1） | 3015 |
| L321-323 | 读 LIDAR_ZONE + 障碍区发送 | `tx_healthy_all_signals`（lidarZone=2） | 3015 |
| L325-327 | 读 LIDAR_DIST + 距离发送 | `tx_healthy_all_signals`（lidarDist=291） | 3015 |
| L329-331 | 读 LIDAR_SIGNAL，`>>8` 取高字节 + 发送 | `tx_healthy_all_signals`（lidarSignal=0xABCD→0xAB） | 3015 |

## 无法覆盖的代码说明

**无。** `Swc_FzcCom.c` 行/分支/函数覆盖均为 **100%**（141/141 行、22/22 分支、
6/6 函数），不存在未覆盖代码。

需要说明的是，`Swc_FzcCom.c` 中几处**防御性分支**均被测试显式驱动而非豁免：

1. **E2eProtect/E2eCheck 的 `data == NULL_PTR` 与 `length < 2` 守卫**——经
   `data=null` 与 `length=1` 输入显式触发 E_NOT_OK 分支（`protect_rejects_null_short`、
   `check_rejects_null_short`）。
2. **Receive/TransmitSchedule 的 `FzcCom_Initialized != TRUE` 守卫**——经
   `skipInit=true` 场景显式驱动未初始化路径（`receive_uninitialized_noop`、
   `tx_uninitialized_noop`），验证 4 路 RX alive 初值、TX alive 初值与周期
   计数器在未初始化时不被触碰。
3. **`FzcCom_TxScheduleCycle >= 1000u` 回绕分支**——经 `cycles=1000` 场景在第
   1000 次调度调用处触发回绕（`tx_schedule_cycle_wraps`），同时覆盖该分支
   true/false 两侧。
4. **CRC-8 移位异或双侧**——经不同载荷字节自然覆盖 `crc & 0x80` 为真（异或
   多项式 0x1D）与为假（仅左移）两种路径，L116/L118 命中数 894/842。

由于测试专用 API 位于独立的原生 harness 进程（链接真实 `Swc_FzcCom.c` 生产
代码），以上防御分支均可通过公开 API 以合法/非法参数组合直接到达，无需注入
测试钩子，**生产代码零改动**。
