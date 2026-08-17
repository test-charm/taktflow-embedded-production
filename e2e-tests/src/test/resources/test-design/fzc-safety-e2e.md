# FZC 本地安全监控 (Swc_FzcSafety) E2E 测试设计

## 被测功能

**FZC ASW 本地安全监控 SWC — 看门狗喂狗（TPS3823 WDI 翻转、四条件门控）+
本地故障聚合（转向/制动/激光雷达 → 统一掩码）+ 自检失败处理 + CAN RX 质量
超时检测（宽限期后 CAN_BUS_OFF）+ 电机切断（宽限期抑制 / 结束后置位）+
安全状态发布（OK / DEGRADED / FAULT）**

覆盖链路：

```text
harness 注入（steerFault / brakeFault / lidarFault / vehicleState /
          selfTestResult / selfTestDone / steerCmdQuality / brakeCmdQuality）
  → Swc_FzcSafety_Init()（状态复位：OK + WDI 翻转 0 + 自检标志 FALSE +
     宽限计数 1500）
  → Swc_FzcSafety_MainFunction()（10ms 周期，SWR-FZC-023 / SWR-FZC-025）：
       · 未初始化守卫 → 直接 return
       · 读 RTE：steer/brake/lidar 故障 + 车辆状态 + 自检结果
       · 故障聚合：steer→0x01、brake→0x02、lidar→0x04
       · 宽限期结束后 Com_GetRxPduQuality(STEER/BRAKE_CMD) == TIMED_OUT
           → 置 FZC_FAULT_CAN_BUS_OFF (0x0100)（宽限期内不轮询）
       · 自检完成且失败 → 置 FZC_FAULT_SELF_TEST (0x20)
       · 电机切断：宽限期内关键故障写 0（抑制）；宽限期结束后写 1
       · 安全状态：steer|brake → FAULT(2)；其他掩码 → DEGRADED(1)；无 → OK(0)
       · 看门狗四条件：无关键故障 + 车辆非 SHUTDOWN + 自检未失败 →
            WDI 翻转 + Dio_WriteChannel；否则 Dem_ReportErrorStatus(WATCHDOG_FAIL)
            + 掩码置 FZC_FAULT_WATCHDOG (0x10)
       · 发布 fault_mask / safety_status 到 RTE
  → Swc_FzcSafety_GetStatus()（状态观测）
```

与既有 ASW E2E（CVC `Swc_Watchdog`、FZC `Swc_FzcCanMonitor` 等）一致，通过测试
专用 API 在原生测试框架内执行真实的 `Swc_FzcSafety.c` 生产代码。故障信号、
车辆状态、自检结果经 harness 的 mock RTE 信号表注入，Com RX PDU 质量经
`Com_GetRxPduQuality` mock 注入。

> **被测代码观测**：`Safety_Initialized`、`Safety_GraceCounter`、
> `Safety_SelfTestDone`、`Safety_WdiToggle` 均为模块静态状态，无法从外部
> 直接读取；`Safety_SelfTestDone` 生产代码仅在 Init 中复位为 FALSE、无任何
> 置位路径。为支持 E2E 断言与驱动自检失败分支，在 `Swc_FzcSafety.c/.h`
> 增加了 **`#ifdef UNIT_TEST` 保护**的观测 getter（`GetInitialized` /
> `GetGraceCounter` / `GetSelfTestDone` / `GetWdiToggle`）与自检标志注入钩子
> （`SetSelfTestDone`）。生产固件构建不定义 `UNIT_TEST`，这些访问器不进入
> 交付固件；仅测试 harness 编译时生效。安全状态输出（faultMask / safetyStatus
> / motorCutoff）经 harness 的 mock RTE 信号表直接观测，WDI 翻转经
> `Dio_WriteChannel` mock 计数观测，DTC 上报经 `Dem_ReportErrorStatus` mock
> 计数观测。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_FzcSafety_Init           │
│ WdiToggle=0, Status=OK       │
│ SelfTestDone=FALSE           │
│ GraceCounter=1500            │
│ Initialized=TRUE             │
└─────────────┬────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Swc_FzcSafety_MainFunction() │
└─────────────┬────────────────┘
              │
  Initialized != TRUE? ──Y──→ return（未初始化空转）
              │N
  Rte_Read 5 信号（steer/brake/lidar 故障、车辆状态、自检结果）
  fault_mask = FZC_FAULT_NONE
  steer_fault != 0? ─────Y──→ fault_mask |= 0x01
  brake_fault != 0? ─────Y──→ fault_mask |= 0x02
  lidar_fault != 0? ─────Y──→ fault_mask |= 0x04
  GraceCounter == 0? ────N──→ 跳过（宽限期抑制）
              │Y
  steer/brake cmd TIMED_OUT? ─→ fault_mask |= 0x0100 (CAN_BUS_OFF)
  SelfTestDone && selftest==FAIL? ──Y──→ fault_mask |= 0x20
  GraceCounter > 0? ──────Y──→ GraceCounter--
  (steer||brake) != 0? ──N──→ 写 motorCutoff=0
              │Y
  GraceCounter == 0? ────Y──→ 写 motorCutoff=1（宽限期后断言）
              └────────N──→ 写 motorCutoff=0（宽限期抑制）
  fault_mask & (STEER|BRAKE)? ─Y──→ Status=FAULT
              │N
  fault_mask != NONE? ─────────Y──→ Status=DEGRADED
              │N
  Status = OK
  wdg_feed_ok = TRUE
  fault_mask & (STEER|BRAKE)? ─Y──→ wdg_feed_ok = FALSE
  vehicle_state == SHUTDOWN? ──Y──→ wdg_feed_ok = FALSE
  SelfTestDone && selftest==FAIL? ─Y──→ wdg_feed_ok = FALSE
  wdg_feed_ok == TRUE? ───────Y──→ WDI 翻转 + Dio_WriteChannel
              │N
  Dem_ReportErrorStatus(WATCHDOG_FAIL) + fault_mask |= 0x10
  Rte_Write(FAULT_MASK, fault_mask)
  Rte_Write(SAFETY_STATUS, Safety_Status)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_FzcSafety_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `reinit` | 相位开始前再次调用 `Swc_FzcSafety_Init()` | `false`、`true`（重复 Init 复位） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`（单次）、`2`（WDI 翻转两次）、`1500`（宽限期结束） | When — 执行控制 |
| `steerFault` | RTE `FZC_SIG_STEER_FAULT` 值 | `0`（无故障）、`1`（关键故障） | When — 故障注入 |
| `brakeFault` | RTE `FZC_SIG_BRAKE_FAULT` 值 | `0`、`1`（关键故障） | When — 故障注入 |
| `lidarFault` | RTE `FZC_SIG_LIDAR_FAULT` 值 | `0`、`1`（非关键故障） | When — 故障注入 |
| `vehicleState` | RTE `FZC_SIG_VEHICLE_STATE` 值 | `RUN=1`（正常）、`SHUTDOWN=5`（抑制喂狗） | When — 状态注入 |
| `selfTestResult` | RTE `FZC_SIG_SELF_TEST_RESULT` 值 | `PASS=1`、`FAIL=0` | When — 状态注入 |
| `selfTestDone` | `Safety_SelfTestDone` 注入（UNIT_TEST 钩子） | `false`（未完成）、`true`（已完成） | When — 状态注入 |
| `steerCmdQuality` | `Com_GetRxPduQuality(STEER_CMD)` 返回值 | `FRESH=0`、`TIMED_OUT=2` | When — 故障注入 |
| `brakeCmdQuality` | `Com_GetRxPduQuality(BRAKE_CMD)` 返回值 | `FRESH=0`、`TIMED_OUT=2` | When — 故障注入 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `status` | `Swc_FzcSafety_GetStatus()` | OK(0)/DEGRADED(1)/FAULT(2) |
| `initialized` | `Safety_Initialized`（getter） | Init 后 1；skipInit 后 0 |
| `graceCounter` | `Safety_GraceCounter`（getter） | 1500 → 随周期递减 → 0 |
| `selfTestDone` | `Safety_SelfTestDone`（getter） | 注入值 |
| `wdiToggle` | `Safety_WdiToggle`（getter） | 每喂狗周期 0/1 交替 |
| `faultMask` | RTE `FZC_SIG_FAULT_MASK`（=204） | 聚合掩码（STEER/BRAKE/LIDAR/WATCHDOG/SELF_TEST/CAN_BUS_OFF） |
| `safetyStatus` | RTE `FZC_SIG_SAFETY_STATUS`（=202） | OK/DEGRADED/FAULT |
| `motorCutoff` | RTE `FZC_SIG_MOTOR_CUTOFF`（=115） | 宽限期后关键故障 1；其余 0 |
| `dtcReported` | `Dem_ReportErrorStatus(FZC_DTC_WATCHDOG_FAIL, FAILED)` 调用次数 | 喂狗抑制时 +1 |
| `comQueries` | `Com_GetRxPduQuality` 调用次数 | 宽限期后每周期 2（steer+brake）；宽限期内 0 |
| `wdiWrites` | `Dio_WriteChannel` 调用次数 | 每喂狗周期 +1 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: 初始化与未初始化守卫**：Init 默认值 / 未初始化 MainFunction 空转，共 2 场景。
> - **规则: 看门狗喂狗**：无故障 WDI 翻转 / 转向故障抑制+DTC / SHUTDOWN 抑制+DTC /
>   自检失败抑制+DTC，共 4 场景。
> - **规则: 故障聚合**：制动掩码 / 激光雷达 DEGRADED / 全部故障组合掩码 /
>   自检未完成忽略失败结果 / 自检完成且通过，共 5 场景。
> - **规则: 电机切断**：宽限期抑制 / 宽限期后置位 / 无关键故障清除 / 故障清除复位，共 4 场景。
> - **规则: CAN RX 质量**：宽限期抑制 / Steer 超时 / Brake 超时 / RX 正常，共 4 场景。
> - **规则: 安全状态发布**：故障清除恢复 OK / 重复 Init 复位，共 2 场景。

每个用例由两个阶段组构成：
- **Given 前置阶段**（经 `存在:` → `/safety/setup` 存储）：设置前置状态（如
  1500 周期宽限期基线）。无前置状态时存空 `phases: []`。
- **When 刺激阶段**（`POST /api/test/asw/fzc/safety` body）：触发被测动作。
  服务端按「前置 + 刺激」顺序执行。
下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`cycles=1`、
`skipInit=false`、`reinit=false`、`steerFault=0`、`brakeFault=0`、
`lidarFault=0`、`vehicleState=RUN(1)`、`selfTestResult=PASS(1)`、
`selfTestDone=false`、`steerCmdQuality=FRESH(0)`、`brakeCmdQuality=FRESH(0)`）。

### 规则: 初始化与未初始化守卫

| 用例 | 阶段序列 | 期望 status | 期望 initialized | 期望 graceCounter |
|---|---|---|---|---|
| `init_defaults_ok` | P0: cycles=1 | OK(0) | 1 | 1499（1500-1） |
| `uninitialized_noop` | P0: cycles=1, skipInit=true | OK(0)（守卫返回） | 0 | 0 |

### 规则: 看门狗喂狗

| 用例 | 阶段序列 | 期望 status | 期望 wdiToggle | 期望 dtcReported |
|---|---|---|---|---|
| `wdg_feed_normal` | P0: cycles=2 | OK(0) | 0（1→0 两次翻转） | 0 |
| `wdg_suppressed_steer_fault` | P0: cycles=1, steerFault=1 | FAULT(2) | — | 1 |
| `wdg_suppressed_shutdown` | P0: cycles=1, vehicleState=5 | OK(0) | — | 1 |
| `wdg_suppressed_selftest_fail` | P0: cycles=1, selfTestDone=true, selfTestResult=0 | DEGRADED(1) | — | 1 |

> `wdg_suppressed_steer_fault`：转向故障置位 STEER(0x01)，关键故障抑制喂狗、
> DTC + WATCHDOG(0x10) → faultMask=0x11。`wdg_suppressed_shutdown`：无故障
> 掩码，但 SHUTDOWN 抑制喂狗 → faultMask=0x10，安全状态仍 OK（状态在喂狗
> 前计算）。`wdg_suppressed_selftest_fail`：自检失败置位 SELF_TEST(0x20) +
> WATCHDOG(0x10) → faultMask=0x30，状态 DEGRADED。

### 规则: 故障聚合

| 用例 | 阶段序列 | 期望 status | 期望 faultMask | 期望 dtcReported |
|---|---|---|---|---|
| `brake_fault_mask` | P0: cycles=1, brakeFault=1 | FAULT(2) | 0x12（BRAKE\|WATCHDOG） | 1 |
| `lidar_fault_mask` | P0: cycles=1, lidarFault=1 | DEGRADED(1) | 0x04（LIDAR） | 0（非关键，喂狗继续） |
| `all_faults_mask` | P0: cycles=1, steer=1, brake=1, lidar=1 | FAULT(2) | 0x17（STEER\|BRAKE\|LIDAR\|WATCHDOG） | 1 |
| `selftest_not_done_ignores_fail` | P0: cycles=1, selfTestDone=false, selfTestResult=0 | OK(0) | 0（未完成不自检位） | 0（喂狗继续） |
| `selftest_done_pass` | P0: cycles=1, selfTestDone=true, selfTestResult=1 | OK(0) | 0（通过不自检位） | 0 |

### 规则: 电机切断

| 用例 | 阶段序列 | 期望 status | 期望 motorCutoff | 期望 faultMask |
|---|---|---|---|---|
| `cutoff_suppressed_during_grace` | P0: cycles=1, steerFault=1 | FAULT(2) | 0（宽限期抑制） | 0x11 |
| `cutoff_asserted_after_grace` | 前置: cycles=1500; P0: cycles=1, steerFault=1 | FAULT(2) | 1（宽限期后断言） | 0x11 |
| `cutoff_cleared_no_critical` | P0: cycles=1, lidarFault=1 | DEGRADED(1) | 0 | 0x04 |
| `cutoff_released_after_fault_clear` | 前置: cycles=1500; P0: cycles=1, steerFault=1; P1: cycles=1, steerFault=0 | OK(0) | 0（故障清除复位） | 0 |

> 电机切断以「读-改-写」保留 Brake SWC 写入的既有值：仅当 (steer||brake)
> 且宽限期结束后才写 1；宽限期内或非关键故障一律写 0。

### 规则: CAN RX 质量（宽限期后 CAN_BUS_OFF）

| 用例 | 阶段序列 | 期望 status | 期望 faultMask | 期望 comQueries |
|---|---|---|---|---|
| `rx_quality_suppressed_during_grace` | P0: cycles=1, steerCmdQuality=2, brakeCmdQuality=2 | OK(0) | 0（宽限期不轮询） | 0 |
| `rx_quality_steer_timedout_after_grace` | 前置: cycles=1500; P0: cycles=1, steerCmdQuality=2 | DEGRADED(1) | 0x0100（CAN_BUS_OFF） | 2 |
| `rx_quality_brake_timedout_after_grace` | 前置: cycles=1500; P0: cycles=1, brakeCmdQuality=2 | DEGRADED(1) | 0x0100（CAN_BUS_OFF） | 2 |
| `rx_quality_fresh_after_grace` | 前置: cycles=1500; P0: cycles=1 | OK(0) | 0（RX 正常） | 2 |

> CAN_BUS_OFF 为**非关键**通信故障（不抑制看门狗，不报 WATCHDOG DTC），但置位
> RTE 掩码 bit8 供 Swc_Heartbeat 抑制 TX。

### 规则: 安全状态发布与重复 Init

| 用例 | 阶段序列 | 期望 status | 期望 safetyStatus | 期望 dtcReported |
|---|---|---|---|---|
| `status_recovers_after_fault_clear` | P0: cycles=1, steerFault=1; P1: cycles=1, steerFault=0 | OK(0) | 0 | 1（P0 累加） |
| `double_init_resets_state` | P0: cycles=1, steerFault=1; P1: cycles=1, reinit=true, steerFault=0 | OK(0) | 0 | 1（P0 累加） |

> `double_init_resets_state`：P0 使状态 FAULT；P1 `reinit=true` 再次 Init 复位
> 状态为 OK、WDI 翻转与宽限计数归位，随后无故障周期喂狗。

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_defaults_ok` | 初始化后默认状态为 OK 且看门狗喂狗 |
> | `uninitialized_noop` | 未初始化时 MainFunction 空转 |
> | `wdg_feed_normal` | 无故障时看门狗每周期翻转 |
> | `wdg_suppressed_steer_fault` | 转向故障抑制看门狗并上报 DTC |
> | `wdg_suppressed_shutdown` | 车辆 SHUTDOWN 抑制看门狗 |
> | `wdg_suppressed_selftest_fail` | 自检完成且失败抑制看门狗并置位自检掩码 |
> | `brake_fault_mask` | 制动故障置位 BRAKE 掩码 |
> | `lidar_fault_mask` | 激光雷达故障置位 LIDAR 掩码且状态为 DEGRADED |
> | `all_faults_mask` | 全部故障同时置位组合掩码 |
> | `selftest_not_done_ignores_fail` | 自检未完成时失败结果不置位自检掩码且看门狗正常 |
> | `selftest_done_pass` | 自检完成且通过时不置位自检掩码且看门狗正常 |
> | `cutoff_suppressed_during_grace` | 宽限期内关键故障抑制电机切断 |
> | `cutoff_asserted_after_grace` | 宽限期结束后关键故障置位电机切断 |
> | `cutoff_cleared_no_critical` | 无关键故障时电机切断保持清除 |
> | `cutoff_released_after_fault_clear` | 故障清除后电机切断复位 |
> | `rx_quality_suppressed_during_grace` | 宽限期内 RX 超时不置 CAN_BUS_OFF |
> | `rx_quality_steer_timedout_after_grace` | 宽限期结束后 Steer 命令超时置 CAN_BUS_OFF |
> | `rx_quality_brake_timedout_after_grace` | 宽限期结束后 Brake 命令超时同样置 CAN_BUS_OFF |
> | `rx_quality_fresh_after_grace` | 宽限期结束后 RX 质量正常不置 CAN_BUS_OFF |
> | `status_recovers_after_fault_clear` | 故障清除后安全状态恢复 OK |
> | `double_init_resets_state` | 重复 Init 复位看门狗翻转与状态 |

## 代码路径覆盖

- `Swc_FzcSafety_Init` 全部可执行行 ✅（21 次 harness 运行均先 Init；`double_init`
  场景 P1 额外再 Init 1 次）
- `Swc_FzcSafety_MainFunction` 全部可执行行 ✅
  - 未初始化守卫（`Safety_Initialized != TRUE` → return）✅（两侧）
  - Rte_Read 5 输入信号 ✅
  - 故障聚合三分支（steer / brake / lidar）✅（各两侧）
  - 宽限期 CAN RX 质量检查（`GraceCounter == 0` 两侧：宽限期抑制 / 结束轮询；
    STEER / BRAKE 各自 TIMED_OUT 两侧）✅
  - 自检失败置位（`SelfTestDone && FAIL` 两侧 + 组合条件）✅
  - 宽限计数递减 ✅（两侧：>0 递减 / ==0 保持）
  - 电机切断三分支（宽限期后置 1 / 宽限期抑制置 0 / 非关键置 0）✅
  - 安全状态三分类（FAULT / DEGRADED / OK）✅
  - 看门狗四条件门控：关键故障 / SHUTDOWN / 自检失败 各自抑制 ✅；喂狗 WDI 翻转 ✅
  - DTC 上报 + WATCHDOG 掩码置位 ✅
  - 发布 fault_mask / safety_status ✅
- `Swc_FzcSafety_GetStatus` ✅（每场景 harness 输出读取）
- UNIT_TEST 观测 getter（`GetInitialized` / `GetGraceCounter` / `GetSelfTestDone`
  / `GetWdiToggle`）与自检注入钩子（`SetSelfTestDone`）✅ 由 harness 输出读取，
  全部命中

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_FzcSafety.c.gcov.html` 实测（2026-08-16 完整套件运行后，含本 feature
21 场景单次运行累积）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（120 / 120 行） |
| **分支覆盖** | **100%**（40 / 40 分支） |
| **函数覆盖** | **100%**（8 / 8 函数） |

覆盖到的函数：`Swc_FzcSafety_Init`、`Swc_FzcSafety_MainFunction`、
`Swc_FzcSafety_GetStatus`（生产 API），以及 5 个 `#ifdef UNIT_TEST` 观测
getter / 注入钩子（`GetInitialized`、`GetGraceCounter`、`GetSelfTestDone`、
`GetWdiToggle`、`SetSelfTestDone`）。

> 下表「实测命中」为**仅运行本 feature（21 场景）单次**后的累积值：21 场景
> 共触发 **21 次 harness 调用**（其中 1 次 `skipInit` 跳过 Init，`double_init`
> 场景 P1 额外 `reinit` 1 次，故 `Swc_FzcSafety_Init` 命中 21 次），
> `MainFunction` 合计进入 **7525 次**（与各场景 cycles 之和 21+7524 一致），
> `SetSelfTestDone` 命中 **29 次**（21 次 harness 运行中前置+刺激相位总数
> 29）。每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。
> 生产固件编译不定义 `UNIT_TEST`，getter 相关行不计入交付固件的有效代码。

---

## 行覆盖分析（100%，120/120）

行覆盖反映**每一行是否被执行**。120 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_FzcSafety_Init（L76-85）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L76-77 | 函数签名 + 入口 `{` | 全部 21 场景（每 harness 运行先 Init；`double_init` P1 再 Init 1 次） | 21 |
| L78 | `Safety_WdiToggle = 0u` | `double_init_resets_state`（P1 复位后 wdiToggle=0）及全部场景 | 21 |
| L79 | `Safety_Status = SAFETY_STATUS_OK` | `double_init_resets_state`（P1 复位后 status=0 断言）及全部场景 | 21 |
| L80 | `Safety_SelfTestDone = FALSE` | `double_init_resets_state`（P1 复位后 selfTestDone=0）及全部场景 | 21 |
| L82 | `Safety_GraceCounter = FZC_POST_INIT_GRACE_CYCLES` | `init_defaults_ok`（graceCounter=1499 起点）、`double_init_resets_state`（P1 复位后 graceCounter=1499） | 21 |
| L84 | `Safety_Initialized = TRUE` | `init_defaults_ok`（initialized=1 断言）及全部已初始化场景 | 21 |
| L85 | 函数结束 `}` | 全部场景 | 21 |

#### Swc_FzcSafety_MainFunction（L91-248）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L91-99 | 函数签名 + 局部变量声明 | 全部场景（每 MainFunction 调用进入） | 7525 |
| L101 | `if (Safety_Initialized != TRUE)` 守卫 | true 侧：`uninitialized_noop`（1 次）；false 侧：全部已初始化周期 | 7525 |
| L102-103 | `return; }`（未初始化空转） | `uninitialized_noop`（skipInit=true） | 1 |
| L108-112 | 局部默认值初始化 | 全部已初始化周期 | 7524 |
| L114-118 | `Rte_Read` 读 5 输入信号 | 全部已初始化周期（每周期读 steer/brake/lidar 故障、车辆状态、自检结果） | 7524 |
| L123 | `fault_mask = FZC_FAULT_NONE` | 全部已初始化周期 | 7524 |
| L125 | `if (steer_fault != 0u)` | true 侧：`wdg_suppressed_steer_fault`、`all_faults_mask`、`cutoff_suppressed_during_grace`、`cutoff_asserted_after_grace`、`cutoff_released_after_fault_clear` P0、`status_recovers` P0、`double_init` P0（7 次）；false 侧：其余周期 | 7524 |
| L126-127 | `fault_mask |= FZC_FAULT_STEER` | 转向故障场景（faultMask=0x01/0x11 断言） | 7 |
| L129 | `if (brake_fault != 0u)` | true 侧：`brake_fault_mask`、`all_faults_mask`（2 次）；false 侧：其余周期 | 7524 |
| L130-131 | `fault_mask |= FZC_FAULT_BRAKE` | 制动故障场景（faultMask=0x12 断言） | 2 |
| L133 | `if (lidar_fault != 0u)` | true 侧：`lidar_fault_mask`、`all_faults_mask`、`cutoff_cleared_no_critical`（3 次）；false 侧：其余周期 | 7524 |
| L134-135 | `fault_mask |= FZC_FAULT_LIDAR` | 激光雷达故障场景（faultMask=0x04 断言） | 3 |
| L144 | `if (Safety_GraceCounter == 0u)` | true 侧：4 个「前置 cycles=1500」场景的刺激周期（6 次）；false 侧：宽限期周期 | 7524 |
| L145 | `if (Com_GetRxPduQuality(STEER_CMD) == TIMED_OUT)` | true 侧：`rx_quality_steer_timedout_after_grace`（1 次）；false 侧：`rx_quality_fresh_after_grace` 等宽限期后周期 | 6 |
| L146-147 | `fault_mask |= FZC_FAULT_CAN_BUS_OFF`（steer） | `rx_quality_steer_timedout_after_grace`（faultMask=256 断言） | 1 |
| L148 | `if (Com_GetRxPduQuality(BRAKE_CMD) == TIMED_OUT)` | true 侧：`rx_quality_brake_timedout_after_grace`（1 次）；false 侧：其余 | 6 |
| L149-150 | `fault_mask |= FZC_FAULT_CAN_BUS_OFF`（brake） | `rx_quality_brake_timedout_after_grace`（faultMask=256 断言） | 1 |
| L151 | `}`（宽限期检查结束） | 宽限期后周期 | 6 |
| L154-155 | `if ((SelfTestDone == TRUE) && (result == FAIL))` | true 侧：`wdg_suppressed_selftest_fail`（1 次）；false 侧：`selftest_done_pass`（result=PASS 短路径）、`selftest_not_done_ignores_fail`（SelfTestDone=FALSE 短路径）等 | 7524 |
| L156-157 | `fault_mask |= FZC_FAULT_SELF_TEST` | `wdg_suppressed_selftest_fail`（faultMask=0x30 含 0x20 断言） | 1 |
| L162 | `if (Safety_GraceCounter > 0u)` | true 侧：宽限期周期（7518 次）；false 侧：宽限期结束后 | 7524 |
| L163-164 | `Safety_GraceCounter--` | 宽限期周期（graceCounter 递减断言） | 7518 |
| L182 | `if ((steer_fault != 0u) \|\| (brake_fault != 0u))` | true 侧：steer（5 次）+ brake（`brake_fault_mask`、`all_faults_mask` 2 次）= 8 次；false 侧：其余周期 | 7524 |
| L183 | `if (Safety_GraceCounter == 0u)`（电机切断） | true 侧：`cutoff_asserted_after_grace`、`cutoff_released_after_fault_clear` P0（2 次）；false 侧：宽限期关键故障（6 次） | 8 |
| L184-187 | `Rte_Write(MOTOR_CUTOFF, 1u)`（断言） | `cutoff_asserted_after_grace`（motorCutoff=1 断言）、`cutoff_released_after_fault_clear` P0 | 2 |
| L188-194 | `Rte_Write(MOTOR_CUTOFF, 0u)`（宽限期抑制） | `wdg_suppressed_steer_fault`、`all_faults_mask`、`cutoff_suppressed_during_grace`、`cutoff_released` P0（宽限期）+ `double_init` P0（6 次） | 6 |
| L195-197 | `else { Rte_Write(MOTOR_CUTOFF, 0u); }`（非关键） | 无转向/制动故障的周期（motorCutoff=0 断言） | 7516 |
| L202 | `if ((fault_mask & (STEER\|BRAKE)) != 0u)`（安全状态） | true 侧：steer/brake 故障场景（8 次）；false 侧：其余周期 | 7524 |
| L203 | `Safety_Status = SAFETY_STATUS_FAULT` | 关键故障场景（status=2 断言） | 8 |
| L204 | `else if (fault_mask != FZC_FAULT_NONE)` | true 侧：`lidar_fault_mask`、`wdg_suppressed_selftest_fail`、RX 超时场景（5 次）；false 侧：无故障周期 | 7516 |
| L205 | `Safety_Status = SAFETY_STATUS_DEGRADED` | 非关键故障场景（status=1 断言） | 5 |
| L206-207 | `else { Safety_Status = SAFETY_STATUS_OK; }` | 无故障周期（status=0 断言） | 7511 |
| L213 | `wdg_feed_ok = TRUE` | 全部周期 | 7524 |
| L216 | `if ((fault_mask & (STEER\|BRAKE)) != 0u)`（喂狗条件 1） | true 侧：关键故障场景（8 次）；false 侧：其余周期 | 7524 |
| L217-218 | `wdg_feed_ok = FALSE` | 关键故障场景（wdiWrites=0 断言） | 8 |
| L221 | `if (vehicle_state == FZC_STATE_SHUTDOWN)`（条件 2） | true 侧：`wdg_suppressed_shutdown`（1 次）；false 侧：其余周期 | 7524 |
| L222-223 | `wdg_feed_ok = FALSE` | `wdg_suppressed_shutdown`（wdiWrites=0、dtcReported=1 断言） | 1 |
| L226-227 | `if ((SelfTestDone == TRUE) && (result == FAIL))`（条件 3） | true 侧：`wdg_suppressed_selftest_fail`（1 次）；false 侧：其余周期 | 7524 |
| L228-229 | `wdg_feed_ok = FALSE` | `wdg_suppressed_selftest_fail`（wdiWrites=0、dtcReported=1 断言） | 1 |
| L233 | `if (wdg_feed_ok == TRUE)` | true 侧：喂狗周期（7514 次）；false 侧：抑制周期（10 次） | 7524 |
| L235-236 | `WdiToggle ^= 1; Dio_WriteChannel(...)` | 喂狗周期（wdiWrites 递增、wdiToggle 交替断言） | 7514 |
| L237 | `} else {`（喂狗抑制分支） | 抑制周期 | 7514 |
| L239-240 | `Dem_ReportErrorStatus(WATCHDOG_FAIL, FAILED); fault_mask \|= WATCHDOG` | 抑制周期（dtcReported 递增、faultMask 含 0x10 断言） | 10 |
| L246 | `Rte_Write(FAULT_MASK, fault_mask)` | 全部周期（faultMask 断言） | 7524 |
| L247 | `Rte_Write(SAFETY_STATUS, Safety_Status)` | 全部周期（safetyStatus 断言） | 7524 |
| L248 | 函数结束 `}` | 全部周期 | 7524 |

#### Swc_FzcSafety_GetStatus（L254-257）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L254-257 | 返回 `Safety_Status` | 全部 21 场景（harness 输出 JSON 的 status 字段逐次调用） | 21 |

#### UNIT_TEST 观测 getter / 注入钩子（L265-289，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L265-269 | `GetInitialized` 返回初始化标志 | 全部场景（harness 输出 initialized 逐次调用） | 21 |
| L270-274 | `GetGraceCounter` 返回宽限计数 | 全部场景（graceCounter 断言） | 21 |
| L275-279 | `GetSelfTestDone` 返回自检标志 | 全部场景（selfTestDone 断言） | 21 |
| L280-284 | `GetWdiToggle` 返回 WDI 翻转值 | 全部场景（wdiToggle 断言） | 21 |
| L285-289 | `SetSelfTestDone` 注入自检标志 | 每个相位（run_phase 顶部调用；含前置+刺激相位共 29 次） | 29 |

> 常量/静态声明（L51-70 静态变量、L44 宏）为非执行行或宏展开计数，不计入
> 可执行行统计（120 行内）。genhtml 的行统计另含 12 个「带分支计数的条件行」
> （L101、L125、L129、L133、L144、L145、L148、L154、L162、L182、L183、L202、
> L204、L216、L221、L226、L233），全部由上述场景命中两侧，故 120/120 行全部
> 覆盖。

---

## 分支覆盖分析（100%，40/40）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `Safety_Initialized != TRUE` | L101 | ✅ 两侧 | `uninitialized_noop`（true，1 次）/ 全部已初始化周期（false，7524 次） |
| `steer_fault != 0u` | L125 | ✅ 两侧 | 转向故障场景（true，7 次）/ 其余周期（false，7517 次） |
| `brake_fault != 0u` | L129 | ✅ 两侧 | 制动故障场景（true，2 次）/ 其余周期（false，7522 次） |
| `lidar_fault != 0u` | L133 | ✅ 两侧 | 激光雷达故障场景（true，3 次）/ 其余周期（false，7521 次） |
| `Safety_GraceCounter == 0u` | L144 | ✅ 两侧 | 宽限期后周期（true，6 次）/ 宽限期周期（false，7518 次） |
| `steer cmd TIMED_OUT` | L145 | ✅ 两侧 | `rx_quality_steer_timedout_after_grace`（true，1 次）/ RX 正常（false，5 次） |
| `brake cmd TIMED_OUT` | L148 | ✅ 两侧 | `rx_quality_brake_timedout_after_grace`（true，1 次）/ RX 正常（false，5 次） |
| `SelfTestDone && FAIL`（自检掩码） | L154-155 | ✅ 两侧 | `wdg_suppressed_selftest_fail`（true，1 次）/ `selftest_done_pass`、`selftest_not_done_ignores_fail`（false，7523 次） |
| `Safety_GraceCounter > 0u` | L162 | ✅ 两侧 | 宽限期周期（true，7518 次）/ 宽限期后（false，6 次） |
| `steer_fault != 0u`（\|\| 左操作数） | L182 | ✅ 两侧 | 转向故障（true，5 次）/ 其余（false） |
| `brake_fault != 0u`（\|\| 右操作数） | L182 | ✅ 两侧 | `brake_fault_mask`、`all_faults_mask`（true，2 次）/ steer=0 场景（false） |
| `GraceCounter == 0u`（电机切断） | L183 | ✅ 两侧 | `cutoff_asserted_after_grace`、`cutoff_released` P0（true，2 次）/ 宽限期关键故障（false，6 次） |
| `fault_mask & (STEER\|BRAKE)`（安全状态） | L202 | ✅ 两侧 | 关键故障场景（true，8 次）/ 其余周期（false，7516 次） |
| `fault_mask != NONE` | L204 | ✅ 两侧 | 非关键故障场景（true，5 次）/ 无故障周期（false，7511 次） |
| `fault_mask & (STEER\|BRAKE)`（喂狗条件 1） | L216 | ✅ 两侧 | 关键故障场景（true，8 次）/ 其余周期（false，7516 次） |
| `vehicle_state == SHUTDOWN` | L221 | ✅ 两侧 | `wdg_suppressed_shutdown`（true，1 次）/ 其余周期（false，7523 次） |
| `SelfTestDone && FAIL`（喂狗条件 3） | L226-227 | ✅ 两侧 | `wdg_suppressed_selftest_fail`（true，1 次）/ 其余周期（false，7523 次） |
| `wdg_feed_ok == TRUE` | L233 | ✅ 两侧 | 喂狗周期（true，7514 次）/ 抑制周期（false，10 次） |

> 全部 18 个分支点两侧均已覆盖（40 条分支全部命中），无无法覆盖的分支。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（120/120） | 0 行 | — |
| 分支 | 100%（40/40） | 0 个 | — |
| 函数 | 100%（8/8） | — | — |

> **无法覆盖的代码说明**：`Swc_FzcSafety.c` 中不存在编译期排除或经公开 API
> 不可达的代码路径，**无豁免项**：
> 1. 本模块无配置指针参数，不存在 `CfgPtr == NULL_PTR` 类防御守卫（与
>    `Swc_Watchdog`/`Swc_Scheduler` 的豁免项不同）。
> 2. `#ifdef SIL_DIAG` 分支（L40-45，`FSAFE_DIAG` 打印）为 SIL 诊断日志开关，
>    原生 harness 以生产配置编译（不定义 `SIL_DIAG`），对应分支被预处理器
>    排除，不计入 120 行总数。该日志路径仅影响诊断输出、不影响安全行为。
> 3. `Safety_SelfTestDone` 生产代码无置位路径（仅 Init 复位），为测试完整
>    驱动自检失败分支，通过 `#ifdef UNIT_TEST` 注入钩子 `SetSelfTestDone`
>    覆盖；交付固件不含该钩子，也不含相关 getter。
>
> 全部生产代码路径（含所有分支两侧）均经公开 API（`Init` + `MainFunction` +
> `GetStatus`）驱动，与 `Swc_Heartbeat`/`Swc_FzcCanMonitor` 等已覆盖 SWC
> 一致。观测 getter（`#ifdef UNIT_TEST`）不计入交付固件，仅为测试编译产物。
