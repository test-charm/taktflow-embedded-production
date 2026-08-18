# RZC 本地安全监控 (Swc_RzcSafety) E2E 测试设计

## 被测功能

**RZC ASW 安全监控 SWC — TPS3823 看门狗 WDI 四条件门控喂狗、故障聚合
位掩码（过流/过温/方向/堵转/电池/自检）、CAN 丢失检测（bus-off / 200ms
静默 / 500ms 错误警告 / 锁存）、CAN 丢失时电机禁用（R_EN/L_EN 拉低）与
安全状态发布（OK/DEGRADED/FAULT）、WATCHDOG_FAIL 边沿 DTC 上报。**
（安全需求 SWR-RZC-023、SWR-RZC-024）

覆盖链路：

```text
Can_GetControllerErrorState(0)（测试 API 注入 error state）
  → Swc_RzcSafety_Init
    → WDI 翻转/故障锁存/状态/CAN 锁存/静默/错误警告计数清零
    → Initialized = TRUE
  → Swc_RzcSafety_MainFunction（10ms 周期）
    → 未初始化守卫
    → Rte_Read 8 路故障/状态信号
    → 故障聚合：过流/过温/方向/堵转/电池/自检 → fault_mask
    → CAN 丢失检测：
        bus-off           → can_fault, 锁存
        静默计数 ≥20      → can_fault, 锁存
        错误警告计数 ≥50  → can_fault, 锁存；否则清零
        锁存后恒置 can_fault
    → can_fault → fault_mask |= CAN, Dio_WriteChannel(R_EN/L_EN, LOW)
    → 安全状态：关键故障/e-stop → FAULT；非关键 → DEGRADED；无 → OK
    → 看门狗四条件门控：
        关键故障 / SHUTDOWN / 自检失败 / bus-off 任一成立 → 不喂狗
          首次边沿上报 Dem_ReportErrorStatus(WATCHDOG_FAIL, FAILED) + 锁存
          fault_mask |= WATCHDOG
        全部通过 → WDI 翻转 + Dio_WriteChannel(WDI, toggle)
    → Rte_Write(RZC_SIG_FAULT_MASK, fault_mask)
    → Rte_Write(RZC_SIG_SAFETY_STATUS, status)
  → Swc_RzcSafety_NotifyCanRx
    → 静默计数器清零
```

与既有 `rzc_motor.feature` / `rzc_currentmonitor.feature` 一致，本测试通过
`/api/test/asw/rzc/safety` 调用原生 harness，执行真实 `Swc_RzcSafety.c`
生产代码。所有内部状态均经公开 API 与 mocked BSW 接口观测（WDI 翻转经
`Dio_WriteChannel` mock 计数、DTC 经 `Dem_ReportErrorStatus` mock 计数、
输出经 harness 的 mock RTE 信号表），**无需修改生产代码**。

## 被测代码流程图

```text
              ┌──────────────────────────┐
              │ Swc_RzcSafety_Init        │
              └─────────────┬────────────┘
                            │
  WDI/锁存/状态/计数全部清零 ──┤
  Initialized = TRUE ────────┤
                            │
              ┌─────────────▼──────────────┐
              │ MainFunction (10ms)        │
              └─────────────┬──────────────┘
                            │
    Initialized != TRUE? ────┬─ Y → return
                             └─ N
                             │
    Rte_Read 8 路故障/状态信号
                             │
    ┌─ 故障聚合（每位独立判断）─┐
    │  overcurrent → 0x01      │
    │  overtemp    → 0x02      │
    │  direction   → 0x04      │
    │  stall       → 0x80      │
    │  battery     → 0x40      │
    │  selftest=FAIL → 0x20    │
    └───────────┬─────────────┘
                │
    ┌─ CAN 丢失检测 ──────────────┐
    │  error_state == BUSOFF? ─┬─ Y → can_fault=TRUE, latch=TRUE
    │  silence++ ≥20? ─────────┬─ Y → can_fault=TRUE, latch=TRUE
    │  error_state == WARNING? ─┬─ Y → warn++ ≥50 ? ─┬─ Y → can_fault,latch
    │                           └─ N → warn=0         └─ N → 继续
    │  latch == TRUE? ──────────┬─ Y → can_fault=TRUE
    │  can_fault? ──────────────┬─ Y → mask|=CAN(0x08)
    │                                  Dio(R_EN, LOW), Dio(L_EN, LOW)
    └───────────┬─────────────┘
                │
    ┌─ 安全状态判断 ──────────────┐
    │  关键位(0x01|0x02|0x04|0x08)? ─┬─ Y → status=FAULT
    │  estop_active? ────────────────┬─ Y → status=FAULT
    │  mask != NONE? ────────────────┬─ Y → status=DEGRADED
    │                                └─ N → status=OK
    └───────────┬─────────────┘
                │
    ┌─ 看门狗四条件门控 ───────────┐
    │  关键位? ────┬─ Y → feed_ok=FALSE
    │  SHUTDOWN? ──┬─ Y → feed_ok=FALSE
    │  自检失败? ──┬─ Y → feed_ok=FALSE
    │  BUSOFF? ────┬─ Y → feed_ok=FALSE
    │  feed_ok? ───┬─ Y → WDI ^= 1, Dio(WDI, toggle), 边沿锁存复位
    │              └─ N → 边沿未锁存 ? Dem(WATCHDOG_FAIL, FAILED)+锁存
    │                     mask |= WATCHDOG(0x10)
    └───────────┬─────────────┘
                │
    Rte_Write(FAULT_MASK, mask), Rte_Write(SAFETY_STATUS, status)
                │
              ┌─▼─────────────────────────────┐
              │ NotifyCanRx: 静默计数器 = 0    │
              └────────────────────────────────┘
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_RzcSafety_Init()` | `false`、`true` | When — 执行控制 |
| `reinit` | 本 phase 起始是否重复 Init | `false`、`true` | When — 执行控制 |
| `cycles` | 当前 phase 的 MainFunction 调用次数 | `1`、`5`、`19`、`20`、`49`、`50` | When — 执行控制 |
| `overcurrent` | RTE `RZC_SIG_OVERCURRENT` | `0`、`1` | When — 数据注入 |
| `overtemp` | RTE `RZC_SIG_TEMP_FAULT` | `0`、`1` | When — 数据注入 |
| `directionFault` | RTE `RZC_SIG_ENCODER_DIR` | `0`、`1` | When — 数据注入 |
| `stallFault` | RTE `RZC_SIG_ENCODER_STALL` | `0`、`1` | When — 数据注入 |
| `batteryFault` | RTE `RZC_SIG_BATTERY_STATUS` | `0`、`1` | When — 数据注入 |
| `selfTestResult` | RTE `RZC_SIG_SELF_TEST_RESULT` | `1`（PASS）、`0`（FAIL） | When — 数据注入 |
| `estopActive` | RTE `RZC_SIG_ESTOP_ACTIVE` | `0`、`1` | When — 数据注入 |
| `vehicleState` | RTE `RZC_SIG_VEHICLE_STATE` | `1`（RUN，等价类代表）、`4`（SAFE_STOP，SHUTDOWN 下界）、`5`（SHUTDOWN） | When — 数据注入 |
| `canErrorState` | `Can_GetControllerErrorState(0)` | `0`（ACTIVE）、`1`（WARNING）、`2`（BUSOFF） | When — 数据注入 |
| `notifyCanRx` | 每周期调用 `Swc_RzcSafety_NotifyCanRx` | `false`、`true` | When — 执行控制 |

说明：

1. `cycles` 的取值用于覆盖 CAN 静默（≥20）与错误警告（≥50）两个计数阈值
   的边界：`19`/`20`（静默下界/触发）、`49`/`50`（警告下界/触发）。
2. `vehicleState` 只对 SHUTDOWN 是否成立影响喂狗门控，故等价类为
   非 SHUTDOWN（0-4）与 SHUTDOWN（5）；取 RUN 作为代表值，取 SAFE_STOP
   作为边界紧邻值（不触发门控），SHUTDOWN 触发门控。
3. 组合场景（如 `all_faults`）用于验证故障聚合位互不影响地合并。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `status` | `Swc_RzcSafety_GetStatus()` 返回值 | `0`=OK、`1`=DEGRADED、`2`=FAULT |
| `faultMask` | RTE `RZC_SIG_FAULT_MASK` 聚合位掩码 | `0x00`/`0x01`/`0x02`/`0x04`/`0x08`/`0x10`/`0x20`/`0x40`/`0x80` 组合 |
| `safetyStatus` | RTE `RZC_SIG_SAFETY_STATUS` | `0`=OK、`1`=DEGRADED、`2`=FAULT |
| `wdiWrites` | `Dio_WriteChannel(WDI)` 调用次数 | 喂狗周期数 |
| `wdiLevel` | WDI 通道当前电平 | `0`/`1`（翻转） |
| `dioCh5`/`dioCh6` | R_EN / L_EN 当前电平 | `0`（CAN 丢失禁用） |
| `dioWrites` | `Dio_WriteChannel` 总调用次数 | `0` 或 喂狗+电机禁用 |
| `demWatchdog` | `RZC_DTC_WATCHDOG_FAIL` 最近状态 | `1`=FAILED、`-1`=未报告 |
| `demWatchdogCount` | `RZC_DTC_WATCHDOG_FAIL` 报告次数 | `0`/`1`（边沿上报） |

## 测试用例

### 规则: 初始化守卫与健康喂狗

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `uninitialized_guard` | `[skipInit=true, cycles=1]` | `status=0`, `faultMask=0`, `wdiWrites=0`, `demWatchdog=-1` |
| `healthy_wdi_toggles` | `[cycles=3, notifyCanRx=true]` | `wdiWrites=3`, `wdiLevel=1`, `status=0`, `demWatchdog=-1` |
| `reinit_idempotent` | `[overcurrent=1] → [reinit=true, cycles=1]` | `faultMask=0`, `wdiWrites=1` |

### 规则: 故障聚合位掩码

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `overcurrent_sets_bit` | `[overcurrent=1, notifyCanRx=true]` | `faultMask=17`（0x01\|0x10）, `status=2`, `wdiWrites=0`, `demWatchdogCount=1` |
| `overtemp_sets_bit` | `[overtemp=1, notifyCanRx=true]` | `faultMask=18`（0x02\|0x10）, `status=2`, `wdiWrites=0` |
| `direction_sets_bit` | `[directionFault=1, notifyCanRx=true]` | `faultMask=20`（0x04\|0x10）, `status=2`, `wdiWrites=0` |
| `stall_degraded` | `[stallFault=1, notifyCanRx=true]` | `faultMask=128`, `status=1`, `wdiWrites=1`, `demWatchdog=-1` |
| `battery_degraded` | `[batteryFault=1, notifyCanRx=true]` | `faultMask=64`, `status=1`, `wdiWrites=1`, `demWatchdog=-1` |
| `stall_battery_aggregate` | `[stallFault=1, batteryFault=1, notifyCanRx=true]` | `faultMask=192`, `status=1`, `wdiWrites=1` |
| `selftest_fail_sets_bit` | `[selfTestResult=0, notifyCanRx=true]` | `faultMask=48`（0x20\|0x10）, `status=1`, `wdiWrites=0` |
| `all_faults_aggregate` | `[overcurrent=1, overtemp=1, directionFault=1, stallFault=1, batteryFault=1, selfTestResult=0, notifyCanRx=true]` | `faultMask=247`（0x01\|0x02\|0x04\|0x80\|0x40\|0x20\|0x10）, `status=2`, `wdiWrites=0` |

### 规则: CAN 丢失检测与电机禁用

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `silence_19_no_fault` | `[cycles=19]` | `faultMask=0`, `wdiWrites=19` |
| `silence_20_fault` | `[cycles=20]` | `faultMask=24`（0x08\|0x10）, `status=2`, `dioCh5=0`, `dioCh6=0` |
| `notify_resets_silence` | `[cycles=20, notifyCanRx=true]` | `faultMask=0`, `wdiWrites=20` |
| `silence_interrupted_then_restarts` | `[cycles=19] → [cycles=19, notifyCanRx=true]` | `faultMask=0`, `wdiWrites=38` |
| `errwarn_49_no_fault` | `[cycles=49, canErrorState=1, notifyCanRx=true]` | `faultMask=0`, `wdiWrites=49` |
| `errwarn_50_fault` | `[cycles=50, canErrorState=1, notifyCanRx=true]` | `faultMask=24`, `status=2`, `dioCh5=0`, `dioCh6=0` |
| `errwarn_reset_on_exit` | `[49, WARNING] → [1, ACTIVE] → [49, WARNING]`（全部 notify） | `faultMask=0`, `wdiWrites=99` |
| `busoff_immediate_fault` | `[cycles=1, canErrorState=2, notifyCanRx=true]` | `faultMask=24`, `status=2`, `dioCh5=0`, `dioCh6=0` |

### 规则: CAN 丢失锁存

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `latch_persists_after_recovery` | `[cycles=20] → [cycles=5, notifyCanRx=true]` | `faultMask=24`, `status=2`, `demWatchdogCount=1` |
| `reinit_clears_latch` | `[cycles=20] → [reinit=true, cycles=1, notifyCanRx=true]` | `faultMask=0`, `status=0`, `wdiWrites=20` |

### 规则: 看门狗喂狗门控与 DTC 上报

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `shutdown_blocks_wdg` | `[vehicleState=5, notifyCanRx=true]` | `faultMask=16`, `status=0`, `wdiWrites=0`, `demWatchdog=1` |
| `selftest_fail_blocks_wdg` | `[selfTestResult=0, notifyCanRx=true]` | `faultMask=48`, `status=1`, `wdiWrites=0`, `demWatchdog=1` |
| `wdg_dtc_edge_once` | `[cycles=5, overcurrent=1, notifyCanRx=true]` | `faultMask=17`, `wdiWrites=0`, `demWatchdogCount=1` |
| `wdg_recovers_after_clear` | `[overcurrent=1] → [notifyCanRx=true]` | `faultMask=0`, `wdiWrites=1`, `demWatchdogCount=1`（保持边沿一次） |

### 规则: 安全状态发布

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `ok_status` | `[notifyCanRx=true]` | `status=0`, `safetyStatus=0` |
| `degraded_status` | `[batteryFault=1, notifyCanRx=true]` | `status=1`, `safetyStatus=1` |
| `estop_fault_status` | `[estopActive=1, notifyCanRx=true]` | `status=2`, `faultMask=0`, `safetyStatus=2`, `wdiWrites=1`, `demWatchdog=-1` |
| `critical_fault_status` | `[overcurrent=1, notifyCanRx=true]` | `status=2`, `safetyStatus=2` |

## 覆盖目标与充分性判断

1. **所有输入取值均至少出现一次**：CAN 静默阈值 19/20、错误警告阈值
   49/50、SHUTDOWN 边界（SAFE_STOP 不触发 / SHUTDOWN 触发）、自检
   PASS/FAIL、全部 6 类故障位、bus-off/警告/正常三种 CAN 状态、
   NotifyCanRx 有/无、重复 Init 有/无。
2. **所有条件分支的判断点均有双侧用例**：
   - `Safety_Initialized != TRUE`（`uninitialized_guard`）
   - 6 个故障位各自 `!=0` / `==0`（`*_sets_bit` 与健康/其他场景）
   - `self_test_result == FAIL`（`selftest_fail_sets_bit`）
   - `can_error_state == BUSOFF`（`busoff_immediate_fault`）
   - `Safety_CanSilenceCounter >= 20`（`silence_19_no_fault` / `silence_20_fault`）
   - `can_error_state == WARNING` 且计数 `>= 50`（`errwarn_49/50_fault`）
   - WARNING 的 else 分支清零（`errwarn_reset_on_exit`）
   - `Safety_CanLossLatched == TRUE`（`latch_persists_after_recovery`）
   - `can_fault == TRUE`（`silence_20_fault` 等）
   - 关键故障位掩码判断（`overcurrent_sets_bit` / `ok_status`）
   - `estop_active != 0`（`estop_fault_status`）
   - `fault_mask != NONE`（`degraded_status` / `ok_status`）
   - 喂狗条件 1 关键故障（`overcurrent_sets_bit`）
   - 喂狗条件 2 SHUTDOWN（`shutdown_blocks_wdg`）
   - 喂狗条件 3 自检失败（`selftest_fail_blocks_wdg`）
   - 喂狗条件 4 bus-off（`busoff_immediate_fault`）
   - `Safety_WdgFailLatched == FALSE`（`wdg_dtc_edge_once` 首周期 vs 后续）
3. **流程图中所有公开 API 路径均被至少一个用例命中**：Init、MainFunction、
   GetStatus、NotifyCanRx。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_RzcSafety.c.gcov.html` 实测（2026-08-18，全量套件 **540 场景 / 3267 步**
全部通过，含本 feature 29 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（145 / 145 行） |
| **分支覆盖** | **100%**（46 / 46 分支） |
| **函数覆盖** | **100%**（4 / 4 函数） |

覆盖到的函数（实测命中次数）：
`Swc_RzcSafety_Init`（92）、`Swc_RzcSafety_MainFunction`（1130）、
`Swc_RzcSafety_GetStatus`（89）、`Swc_RzcSafety_NotifyCanRx`（810）。

> 命中次数来自整套 `./gradlew cucumber` 执行后的覆盖 HTML；数值会随套件规模变化，
> 但“哪些行由哪些场景覆盖”这一映射关系保持不变。

### 行覆盖分析（100%，145/145）

无未覆盖行。本模块**不存在**其他 ASW SWC 常见的“防御性守卫不可达”或
`#ifdef PLATFORM_HIL` 平台排除问题：

1. 唯一的初始化守卫（L127）两侧均被覆盖：`uninitialized_guard` 走 true 侧，
   其余全部场景走 false 侧。
2. 唯一的编译期分支 `#ifdef SIL_DIAG`（CAN_ERR_WARN_CYCLES 500 vs 50）中，
   harness 以生产配置编译（不定义 `SIL_DIAG`），`500u` 分支被预处理器排除，
   不计入行统计；实际编译的 `50u` 定义由 `errwarn_49_no_fault` /
   `errwarn_50_fault` 边界用例覆盖。
3. 不需要 UNIT_TEST 观测 getter：WDI 翻转经 `Dio_WriteChannel` mock 计数，
   DTC 上报经 `Dem_ReportErrorStatus` mock 计数，输出经 harness 的 mock RTE
   信号表观测，**生产代码零改动**。

#### Swc_RzcSafety_Init（L97-106）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L98 | 函数体入口 | 除 `uninitialized_guard` 外的全部场景（首 phase Init） | 92 |
| L99-104 | 6 项状态清零（WDI 翻转/边沿锁存/状态/CAN 锁存/静默计数/错误警告计数） | 同上；`reinit_idempotent`、`reinit_clears_latch` 覆盖重复 Init 复位 | 92 |
| L105 | `Safety_Initialized = TRUE` | 全部已初始化场景 | 92 |

#### Swc_RzcSafety_MainFunction（L112-295）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L127-129 | `Initialized != TRUE` 守卫 → return | `uninitialized_guard`（true 侧）；其余场景 false 侧 | 3 / 1127 |
| L134-141 | 局部变量默认值 | 全部已初始化场景 | 1127 |
| L143-150 | 8 路 `Rte_Read` 故障/状态信号 | 全部已初始化场景 | 1127 |
| L155 | `fault_mask = NONE` | 全部已初始化场景 | 1127 |
| L157-159 | `overcurrent != 0` → 置位 0x01 | `overcurrent_sets_bit`、`all_faults_aggregate`、`wdg_dtc_edge_once`、`wdg_recovers_after_clear` | 30 / 1097 |
| L161-163 | `overtemp != 0` → 置位 0x02 | `overtemp_sets_bit`、`all_faults_aggregate` | 6 / 1121 |
| L165-167 | `direction != 0` → 置位 0x04 | `direction_sets_bit`、`all_faults_aggregate` | 6 / 1121 |
| L169-171 | `stall != 0` → 置位 0x80 | `stall_degraded`、`stall_battery_aggregate`、`all_faults_aggregate` | 9 / 1118 |
| L173-175 | `battery != 0` → 置位 0x40 | `battery_degraded`、`stall_battery_aggregate`、`all_faults_aggregate` | 12 / 1115 |
| L177-179 | `self_test == FAIL` → 置位 0x20 | `selftest_fail_sets_bit`、`selftest_fail_blocks_wdg`、`all_faults_aggregate` | 9 / 1118 |
| L184-186 | `can_fault=FALSE`、`can_error_state=ACTIVE`、`Can_GetControllerErrorState` | 全部已初始化场景 | 1127 |
| L189-192 | `error_state == BUSOFF` → can_fault + 锁存 | `busoff_immediate_fault` | 3 / 1124 |
| L195-199 | 静默计数 ≥ 20 → can_fault + 锁存 | `silence_20_fault`、`latch_persists_after_recovery`、`reinit_clears_latch`；`silence_19_no_fault` 覆盖 19 边界 | 10 / 1117 |
| L202-207 | WARNING 计数 ≥ 50 → can_fault + 锁存 | `errwarn_50_fault`（50 触发）；`errwarn_49_no_fault` 覆盖 49 边界 | 3 / 588 |
| L208-210 | 非 WARNING 态计数清零 | `errwarn_reset_on_exit`、全部健康/静默场景 | 536 |
| L213-215 | `CanLossLatched == TRUE` → can_fault | `latch_persists_after_recovery`（锁存保持）；`reinit_clears_latch` 覆盖清除后 false 侧 | 31 / 1096 |
| L217-219 | `can_fault` → `mask |= 0x08` | `silence_20_fault`、`errwarn_50_fault`、`busoff_immediate_fault`、`latch_persists_after_recovery` | 31 / 1096 |
| L222-225 | `can_fault` → `Dio(R_EN/L_EN, LOW)` | 同上 | 31 |
| L233-235 | 关键位（0x01\|0x02\|0x04\|0x08）→ FAULT | `overcurrent_sets_bit`、`overtemp_sets_bit`、`direction_sets_bit`、全部 CAN 丢失场景 | 67 / 1060 |
| L236-238 | `estop_active != 0` → FAULT | `estop_fault_status` | 3 / 1124 |
| L239-240 | 非关键故障（mask≠NONE）→ DEGRADED | `stall_degraded`、`battery_degraded`、`stall_battery_aggregate`、`selftest_fail_sets_bit` | 18 / 1109 |
| L241-242 | 无故障 → OK | `ok_status`、`healthy_wdi_toggles`、`uninitialized_guard` 外全部健康场景 | 1039 |
| L251-254 | 喂狗条件 1：关键故障 → 不喂 | 全部关键故障/CAN 丢失场景 | 67 / 1060 |
| L257-259 | 喂狗条件 2：SHUTDOWN → 不喂 | `shutdown_blocks_wdg`；`vehicleState=4`（SAFE_STOP）等非 SHUTDOWN 场景覆盖 false 侧 | 3 / 1124 |
| L262-264 | 喂狗条件 3：自检失败 → 不喂 | `selftest_fail_blocks_wdg`、`selftest_fail_sets_bit` | 9 / 1118 |
| L267-269 | 喂狗条件 4：bus-off → 不喂 | `busoff_immediate_fault` | 3 / 1124 |
| L271-275 | 四条件全过 → WDI 翻转 + `Dio(WDI)` + 边沿锁存复位 | `healthy_wdi_toggles`、`ok_status`、`stall_degraded`、`battery_degraded`、`errwarn_49_no_fault`、`reinit_idempotent` 等 | 1051 |
| L283-286 | 首次边沿 `Dem(WATCHDOG_FAIL, FAILED)` + 锁存 | `wdg_dtc_edge_once`（5 周期只报 1 次）、`wdg_recovers_after_clear`（恢复后保持 1 次） | 49 / 102 |
| L287-288 | 不喂狗时 `mask |= 0x10` | 全部不喂狗场景 | 76 |
| L293-294 | `Rte_Write(FAULT_MASK)` + `Rte_Write(SAFETY_STATUS)` | 全部已初始化场景 | 1127 |

#### Swc_RzcSafety_GetStatus（L301-304）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L302-303 | 返回 `Safety_Status` | 全部场景（harness 每个运行输出一次） | 89 |

#### Swc_RzcSafety_NotifyCanRx（L310-313）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L311-312 | `Safety_CanSilenceCounter = 0` | `notify_resets_silence`、`silence_interrupted_then_restarts`、`errwarn_*`、`healthy_wdi_toggles` 等所有 `notifyCanRx=true` 场景 | 810 |

### 分支覆盖分析（100%，46/46）

23 个条件判断点的两侧全部命中，无未覆盖分支：

| 分支 | 位置 | 覆盖用例（true 侧 / false 侧） |
|---|---|---|
| `Safety_Initialized != TRUE` | L127 | `uninitialized_guard` / 全部已初始化场景 |
| `overcurrent != 0u` | L157 | `overcurrent_sets_bit` / 健康场景 |
| `overtemp != 0u` | L161 | `overtemp_sets_bit` / 健康场景 |
| `direction_fault != 0u` | L165 | `direction_sets_bit` / 健康场景 |
| `stall_fault != 0u` | L169 | `stall_degraded` / 健康场景 |
| `battery_fault != 0u` | L173 | `battery_degraded` / 健康场景 |
| `self_test == FAIL` | L177 | `selftest_fail_sets_bit` / 健康场景 |
| `error_state == BUSOFF` | L189 | `busoff_immediate_fault` / 全部非 bus-off 场景 |
| `silence >= 20` | L196 | `silence_20_fault` / `silence_19_no_fault` |
| `error_state == WARNING` | L202 | `errwarn_*` / 健康场景 |
| `warn >= 50` | L204 | `errwarn_50_fault` / `errwarn_49_no_fault` |
| `CanLossLatched == TRUE` | L213 | `latch_persists_after_recovery` / 未锁存场景 |
| `can_fault == TRUE` | L217/L222 | `silence_20_fault` 等 / 健康场景 |
| 关键位 != 0（状态） | L233 | `overcurrent_sets_bit` 等 / `degraded_status`、`ok_status` |
| `estop_active != 0` | L236 | `estop_fault_status` / 非 e-stop 场景 |
| `fault_mask != NONE` | L239 | `degraded_status` / `ok_status` |
| 关键位 != 0（喂狗条件 1） | L251 | 全部关键故障场景 / 健康场景 |
| `vehicle_state == SHUTDOWN` | L257 | `shutdown_blocks_wdg` / `vehicleState=4` 等 |
| `self_test == FAIL`（喂狗条件 3） | L262 | `selftest_fail_blocks_wdg` / 健康场景 |
| `error_state == BUSOFF`（喂狗条件 4） | L267 | `busoff_immediate_fault` / 非 bus-off 场景 |
| `wdg_feed_ok == TRUE` | L271 | 健康场景 / 全部不喂狗场景 |
| `WdgFailLatched == FALSE` | L283 | `wdg_dtc_edge_once` 首周期 / 后续周期（不再上报） |

### 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（145/145） | 0 行 | — |
| 分支 | 100%（46/46） | 0 个 | — |
| 函数 | 100%（4/4） | — | — |

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-18 | 初版设计文档（输入/输出因子、流程图、29 个 E2E 用例） |
| 2026-08-18 | 新增 `rzc_safety.feature`（29 场景全部通过）、`rzc_safety_harness.c`、`/api/test/asw/rzc/safety` 测试 API；全量 `./gradlew cucumber` 实测 **540 场景 / 3267 步全部通过**，并补充覆盖率（行/分支/函数 100%）与逐行映射 |

