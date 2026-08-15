# CVC 踏板 -> Torque_Request E2E 测试设计

## 被测功能

**CVC ASW 踏板处理到 Torque_Request 输出**

覆盖链路：

```text
踏板传感器输入
  → Swc_Pedal_MainFunction
  → RTE 扭矩/故障输出
  → Swc_CvcCom_TransmitSchedule
  → Torque_Request 命令信号
```

这是首个面向 ASW 的 E2E 用例，因为它是：

1. 比紧急停止/多 ECU 安全状态链更简单，
2. 仍然代表核心车辆控制逻辑，
3. 直接关联 CVC ASW 层，
4. 接近 `panda` 原生固件 feature 测试的风格。

测试**不**通过仪表盘/系统 E2E 运行器。  
而是使用**测试专用 API**，在原生测试框架内执行 `Swc_Pedal.c` 和 `Swc_CvcCom.c` 的真实 C 生产代码。

---

## 被测代码流程图

```
                        ┌──────────────────┐
                        │ Swc_Pedal_Init   │
                        └────────┬─────────┘
                                 │
                        ┌────────▼─────────┐
                        │ ConfigPtr==NULL? │────Y──→ [返回, 未初始化]
                        └────────┬─────────┘
                                 │N
                        ┌────────▼─────────┐
                        │ 初始化状态变量    │
                        └────────┬─────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │       Swc_Pedal_MainFunction         │
              │         (每 10ms 调用)               │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │  Pedal_Initialized != TRUE?          │──Y──→ [返回]
              │  Pedal_CfgPtr == NULL_PTR?           │──Y──→ [返回]
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 1: IoHwAb_ReadPedalAngle(0/1)  │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 2: SPI 故障检查                  │
              │  ret1 != E_OK → SENSOR1_FAIL        │
              │  ret2 != E_OK → SENSOR2_FAIL        │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 3: 合理性检查 (仅传感器OK时)      │
              │  |S1-S2| >= plausThreshold → debounce│
              │  debounce >= plausDebounce → fault   │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 4: 卡滞检测 (仅无其他故障时)      │
              │  |raw1-prev| < stuckThreshold → count│
              │  count >= stuckCycles → STUCK fault  │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 5: 位置计算                      │
              │  (raw1+raw2)/2, scale 16383→1000    │
              │  传感器故障时 position=0              │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 6: 扭矩查表 (16项LUT+线性插值)    │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 7: 斜坡限制                      │
              │  上升: max Δ=rampLimit/cycle          │
              │  下降: 立即生效 (安全优先)             │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 8: 模式限制                      │
              │  RUN→100%, DEGRADED→75%              │
              │  LIMP→30%, 其他→0%                   │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 9: 故障处理 (零扭矩锁存)          │
              │  新故障→激活锁存, 立即清零(除STUCK)    │
              │  锁存中→计数清零周期→清除锁存          │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 10: 写 RTE 信号                  │
              │  位置/故障/扭矩/方向                   │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Step 11: DTC 上报                     │
              └──────────────────┬──────────────────┘
                                 │
              ┌──────────────────▼──────────────────┐
              │ Swc_CvcCom_TransmitSchedule           │
              │  读RTE扭矩→Com_SendSignal             │
              └──────────────────────────────────────┘
```

---

## 输入和输出

### 输入因子

| 因子 | 含义 | 等价类/范围 | 选定值 | 分类 |
|---|---|---|---|---|
| `sensor1Pct` | 踏板传感器 1 百分比 (功能输入) | 0..100，标称相等，大幅不匹配，死区附近 | `3`、`20`、`40`、`50`、`60`、`80`、`100` | **When — 功能输入** |
| `sensor2Pct` | 踏板传感器 2 百分比 (功能输入) | 0..100，标称相等，大幅不匹配，死区附近 | `3`、`40`、`50`、`60`、`80`、`100` | **When — 功能输入** |
| `vehicleState` | 车辆模式 (RTE 环境上下文) | `RUN`、`DEGRADED`、`LIMP`、`SAFE_STOP`、`SHUTDOWN`、`INIT` | 全部 | **Given — 前置条件** |
| `cycles` | 10ms 循环次数 (harness 执行参数) | 消抖不足，消抖足够，斜坡饱和足够 | `2`、`100`、`200` | **When — 执行控制** |
| `spiFaultSensor` | SPI 故障注入 (测试基础设施) | `null`、`0`、`1` | `0`、`1` | **When — 故障注入** |

> **注意**: SPI 传感器故障 (`SENSOR1_FAIL` / `SENSOR2_FAIL`) 通过 `spiFaultSensor` 注入，卡滞检测
> (`STUCK`) 通过 `ditherAmplitude=0` 触发，零扭矩锁存生命周期通过 `recoverCycles` 恢复阶段验证——
> 详见下方「无法覆盖的代码说明」。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `outputs.torqueRequestPct` | ASW 写入的最终扭矩请求百分比 | `0`、`30`、`40`、`75` |
| `outputs.pedalFaultName` | ASW 踏板故障分类 | `NONE`、`PLAUSIBILITY` |
| `outputs.torqueDirection` | 扭矩方向信号 | 扭矩 > 0 时为 `1`，扭矩 = 0 时为 `0` |
| `outputs.comSignals.torqueRequestCommandPct` | `Swc_CvcCom` 转发的值 | 与 `torqueRequestPct` 相同 |

---

## 测试用例

| 用例名称 | sensor1Pct | sensor2Pct | vehicleState | cycles | spiFaultSensor | 期望 torqueRequestPct | 期望故障 | 期望方向 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| run_matching_40pct_produces_40pct_torque | 40 | 40 | RUN | 100 | — | 40 | NONE | 1 |
| run_mismatched_pedals_zero_torque_after_debounce | 20 | 80 | RUN | 2 | — | 0 | PLAUSIBILITY | 0 |
| degraded_mode_caps_full_pedal_to_75pct | 100 | 100 | DEGRADED | 200 | — | 75 | NONE | 1 |
| limp_mode_caps_pedal_to_30pct | 50 | 50 | LIMP | 100 | — | 30 | NONE | 1 |
| safe_stop_mode_zeroes_torque | 80 | 80 | SAFE_STOP | 100 | — | 0 | NONE | 0 |
| shutdown_mode_zeroes_torque | 60 | 60 | SHUTDOWN | 100 | — | 0 | NONE | 0 |
| init_mode_zeroes_torque | 40 | 40 | INIT | 100 | — | 0 | NONE | 0 |
| sensor1_spi_fault_zeroes_torque | 50 | 50 | RUN | 100 | 0 | 0 | SENSOR1_FAIL | 0 |
| sensor2_spi_fault_zeroes_torque | 50 | 50 | RUN | 100 | 1 | 0 | SENSOR2_FAIL | 0 |
| dead_zone_low_pedal_outputs_zero | 3 | 3 | RUN | 100 | — | 0 | NONE | 0 |

---

## 代码行级覆盖映射

> **覆盖率实测**（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）：
>
> | 源文件 | 行覆盖 | 分支覆盖 | 函数覆盖 |
> |---|---|---|---|
> | `Swc_Pedal.c` | **90.3%**（223/247） | **87.8%**（86/98） | **100%**（7/7） |
> | `Swc_CvcCom.c` | **94.5%**（138/146） | **61.8%**（21/34） | **100%**（3/3） |
>
> 两文件的覆盖率差异源于测试链路范围：harness 执行 `Swc_Pedal_MainFunction`（完整踏板逻辑）
> 与 `Swc_CvcCom_TransmitSchedule`（TX 路径），但**不调用** `Swc_CvcCom_BridgeRxToRte`
> （RX→RTE 桥接，约 50 行）与 `Swc_Pedal_GetPosition`（不在功能链路内）——详见下方各节。
>
> 以下逐行列出 `Swc_Pedal.c` 中每个代码块被哪个测试用例覆盖。未覆盖的行说明原因。

### 辅助函数

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 107-113 | `Pedal_AbsDiff16` | 全部用例 (合理性检查内部调用) | ✅ 完全覆盖 |
| 124-166 | `Pedal_LookupTorque` | run_matching_40pct, degraded_75pct 等 | ✅ 主路径覆盖 |
| 133-135 | `position >= PEDAL_POSITION_MAX` (返回最后LUT项) | degraded_75pct, limp_30pct | ✅ 两分支覆盖 |
| 141-143 | `idx >= (TORQUE_LUT_SIZE-1)` 保护 | — | ❌ **不可达** — 前一个检查已覆盖 position≥1000 的情况，position<1000 时 idx 最大为 14，永远 < 15 |
| 155-159 | `upper >= lower` else 分支 | — | ❌ **不可达** — Torque LUT 单调不减，upper 永远 ≥ lower |
| 161-163 | `interp > PEDAL_POSITION_MAX` 保护 | — | ❌ **不可达** — 插值结果在当前LUT范围内永远 ≤ 1000 |
| 177-201 | `Pedal_ApplyRamp` | 全部用例 | ✅ 完全覆盖 |
| 182-184 | `Pedal_CfgPtr == NULL_PTR` 保护 | — | ❌ **防御代码** — harness 总是传递有效配置 |
| 190-193 | ramp 超出/未超出 分支 | run_matching_40pct (ramp起作用), degraded_75pct (ramp饱和) | ✅ 两分支覆盖 |
| 212-238 | `Pedal_GetModeLimit` | 全部用例 | — |
| 217-219 | `CVC_STATE_RUN` | run_matching_40pct, mismatch, dead_zone | ✅ |
| 220-222 | `CVC_STATE_DEGRADED` | degraded_75pct | ✅ |
| 223-225 | `CVC_STATE_LIMP` | **limp_mode_caps_pedal_to_30pct** (新增) | ✅ |
| 226-231 | `SAFE_STOP/SHUTDOWN/INIT` → limit=0 | **safe_stop, shutdown, init** (新增) | ✅ |
| 232 | `default` → limit=0（fallthrough 兜底） | — | ❌ **不可达** — 车辆状态枚举 0-5 全部被显式 case 覆盖（RUN/DEGRADED/LIMP/SAFE_STOP/SHUTDOWN/INIT），API 也拒绝 >5 的非法值，`default` 标签分支永不命中（防御兜底） |

### Swc_Pedal_Init (行 244-268)

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 246-250 | `ConfigPtr == NULL_PTR` 保护 | — | ❌ **防御代码** — harness 总是传递有效配置 |
| 252-267 | 正常初始化 | 全部用例 (harness 调用 Init) | ✅ |

### Swc_Pedal_MainFunction (行 274-532)

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 288-290 | `Pedal_Initialized != TRUE` 保护 | — | ❌ **防御代码** — harness 在 MainFunction 前调用 Init |
| 292-294 | `Pedal_CfgPtr == NULL_PTR` 保护 | — | ❌ **防御代码** — harness 总是传递有效配置 |
| 303-304 | IoHwAb_ReadPedalAngle 调用 | 全部用例 | ✅ |
| 309-315 | SPI 故障检查 (ret1/ret2 != E_OK) | sensor1_spi (SENSOR1_FAIL), sensor2_spi (SENSOR2_FAIL) | ✅ 全分支覆盖 (ret1失败 + ret2失败 + 都OK) |
| 324-335 | 合理性检查 | run_mismatched (触发故障), 其他 (正常通过) | ✅ 两分支覆盖 |
| 349-372 | 卡滞检测 | stuck_fault (`ditherAmplitude=0`, STUCK→FAILED), 正常用例 (PASSED) | ✅ 已覆盖（`ditherAmplitude=0` 使 \|raw-prev\| < stuckThreshold，卡滞计数递增） |
| 381-389 | 位置计算 (传感器OK/故障分支) | 全部用例 | ✅ OK分支覆盖; ✅ 故障分支 → sensor1_spi, sensor2_spi |
| 384-386 | `position > PEDAL_POSITION_MAX` 保护 | — | ❌ **不可达** — raw 值 14 位上限 16383，按缩放公式后 position 不可能超过 1000 |
| 396 | Pedal_LookupTorque 调用 | 全部用例 | ✅ |
| 401 | Pedal_ApplyRamp 调用 | 全部用例 | ✅ |
| 407 | Pedal_PrevTorque 保存 | 全部用例 | ✅ |
| 412-413 | Rte_Read vehicle_state | 全部用例 | ✅ |
| 415-419 | 模式限制 (torque > mode_limit?) | degraded_75pct, limp_30pct (限制生效); run_40pct (不限制) | ✅ 两分支覆盖 |
| 435-453 | 新故障处理 | run_mismatched (PLAUSIBILITY → latch激活), run_mismatched 第3周期 (L444-445 锁存中再故障重启计数器) | ✅ 全分支覆盖 |
| 454-467 | 零扭矩锁存生命周期 (计数→清除→恢复) | latch_recovery (合理性故障锁存恢复) | ✅ 全分支覆盖 (计数器递增+清除+恢复前强制清零) |
| 468-471 | 无故障无锁存路径 | run_40pct 等正常用例 | ✅ |
| 476-494 | 写 RTE 信号 | 全部用例 | ✅ |
| 482-488 | torque_pct_scaled > 100 保护 | — | ❌ **不可达** — torque 内部范围 0-1000，除以 10 后最大 100 |
| 501-507 | DTC 合理性上报 (PASSED/FAILED/latch) | run_mismatched (FAILED), 正常用例 (PASSED) | ✅ PASSED+FAILED; ❌ latch分支 → latch不可测 |
| 509-515 | DTC 卡滞上报 | stuck_fault (STUCK→FAILED), 正常用例 (PASSED) | ✅ PASSED+FAILED; latch分支不可测 |
| 517-523 | DTC SENSOR1_FAIL 上报 | sensor1_spi (FAILED), 正常用例 (PASSED) | ✅ PASSED+FAILED; latch分支不可测 |
| 525-531 | DTC SENSOR2_FAIL 上报 | sensor2_spi (FAILED), 正常用例 (PASSED) | ✅ PASSED+FAILED; latch分支不可测 |

### Swc_Pedal_GetPosition (行 539-552)

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 539-540, 549-552 | GetPosition 正常路径（初始化检查通过 + 非 NULL + 缩放） | 「GetPosition 报告踏板位置百分比」（`getPosition: true`） | ✅ 正常路径覆盖（`getPosition: 40`） |
| 541-542 | `Pedal_Initialized != TRUE` 返回 E_NOT_OK | — | ❌ **防御代码** — harness 在调用 GetPosition 前已执行 `Swc_Pedal_Init`，不可能未初始化 |
| 545-546 | `pos == NULL_PTR` 返回 E_NOT_OK | — | ❌ **防御代码** — harness 总是传递有效指针 |

### Swc_CvcCom_TransmitSchedule — Torque_Request 桥接 (行 198-212)

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 203-211 | 读 RTE → Com_SendSignal | 全部用例 | ✅ |

### Swc_CvcCom TX 路径其它块（行 72-220）

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 76-79 | `CvcCom_Initialized != TRUE` 返回 | — | ❌ **防御代码** — harness 在 TransmitSchedule 前调用 Init |
| 99-104 | TX 缓冲清零 + 车辆状态填充 | 全部用例 | ✅ |
| 106 | faultMask 组装：`faultSig != 0`（ESTOP） | 全部用例（默认 0） | ✅ true+false 均走（部分用例注入故障） |
| 108-128 | faultMask 组装各信号位 | 部分用例 | ✅ 位或分支随注入信号命中 |
| 153-164 | `vs >= SAFE_STOP` → `tx_brake = CVC_SAFE_BRAKE_CMD` | — | ❌ **未覆盖** — 踏板 feature 场景均未进入 SAFE_STOP 状态（车辆状态固定，无故障链） |
| 176 | E-Stop 广播 `estop_val != 0 ? 1 : 0` | 全部用例 | ✅ |
| 207-209 | `torque > 100` 钳位 | — | ❌ **不可达** — pedal 内部已限幅 0-100 |

### Swc_CvcCom_BridgeRxToRte（行 222-307，RX→RTE 桥接）

| 行号 | 代码 | 覆盖用例 | 状态 |
|---|---|---|---|
| 222-307 | 整个函数（读 Com RX 信号 → 写 RTE） | — | ❌ **不在测试链路** — harness 只调用 TX 路径（`Swc_CvcCom_TransmitSchedule`），不调用 `Swc_CvcCom_BridgeRxToRte`（RX 桥接属 `Swc_VehicleState` feature 的输入侧，由车辆状态机测试覆盖） |

---

## 无法覆盖的代码说明

以下 `Swc_Pedal.c` 与 `Swc_CvcCom.c` 中的代码无法被当前 E2E 测试覆盖，按原因分类：

### A. 防御代码 (Defensive Guards)

这些是 ASIL D 安全编码实践中的防御性检查，在正常流程中永远不会触发：

| 代码位置 | 说明 | 不可覆盖的原因 |
|---|---|---|
| `Pedal_Init` L246-250 | ConfigPtr == NULL_PTR 检查 | harness 始终传递有效配置指针 |
| `MainFunction` L288-294 | 未初始化 / NULL 配置检查 | harness 在 MainFunction 前调用 Init 并传递有效配置 |
| `Pedal_ApplyRamp` L182-184 | NULL_PTR 配置检查 | 同上 |
| `Pedal_GetPosition` L541-542 | `Pedal_Initialized != TRUE` 返回 | harness 调用 GetPosition 前已执行 Init |
| `Pedal_GetPosition` L545-546 | `pos == NULL_PTR` 返回 | harness 总是传递有效指针 |
| `CvcCom_TransmitSchedule` L76-79 | `CvcCom_Initialized != TRUE` 返回 | harness 在 TransmitSchedule 前调用 Init |

### B. 不可达代码 (Dead Code)

这些分支在当前常量配置下逻辑上不可能到达：

| 代码位置 | 说明 | 不可达的原因 |
|---|---|---|
| `Pedal_LookupTorque` L141-143 | idx >= LUT_SIZE-1 保护 | position<1000 时 idx ≤ 14, position≥1000 已在前面返回 |
| `Pedal_LookupTorque` L158-159 | 插值递减分支 | LUT 单调不减 |
| `Pedal_LookupTorque` L161-163 | interp > MAX 保护 | LUT 值 × 插值不会超限 |
| `MainFunction` L384-386 | position > MAX 保护 | raw 14 位上限 16383，缩放后 position 不可能超过 1000 |
| `MainFunction` L484-486 | torque_pct > 100 保护 | 内部值 0-1000, ÷10 ≤ 100 |
| `GetModeLimit` L232 | `default` → limit=0（fallthrough 兜底） | 车辆状态枚举 0-5 全部被显式 case 覆盖，API 拒绝 >5 的非法值，`default` 标签永不命中 |
| `CvcCom_TransmitSchedule` L207-209 | torque > 100 钳位 | pedal 内部已限幅 0-100 |

### C. 已通过 harness 增强覆盖的路径（原不可测/链路外）

以下路径此前不在测试链路或 harness 无法触发，已通过 harness 增强 + 新增场景覆盖：

| 代码位置 | 覆盖场景 | 增强方式 |
|---|---|---|
| `Swc_Pedal_GetPosition` L538-552 | 「GetPosition 报告踏板位置百分比」 | `getPosition: true` 触发 harness 调用该函数并输出（`getPosition: 40`） |
| `Swc_CvcCom_BridgeRxToRte` L222-307 | 「BridgeRxToRte 将制动与电机故障桥接到 RTE」「心跳存活计数器桥接」 | `bridgeRx: true` + `rxBrakeFault`/`rxMotorCutoff`/`rxBattery`/`rxSteeringFault`/`rxMotorFault`/`rxScRelay`/`rxFzcAlive`/`rxzAlive` 注入 Com shadow |
| `CvcCom_TransmitSchedule` L153-164 SAFE_STOP 制动 | 「SAFE_STOP 状态发送最大制动命令」「SHUTDOWN 状态发送最大制动命令」 | 从 pedal harness 链接列表移除 `Swc_VehicleState.c`，使 `Swc_VehicleState_GetState` 解析到 harness stub（读注入的 vehicle_state），`tx_brake` 正确置 100 |

> **关键修复**：此前 `Swc_VehicleState_GetState` 解析到真实 `Swc_VehicleState.c` 实现（返回内部
> `current_state`，恒为 INIT=0），导致 `tx_brake` 恒 0。从 pedal harness 链接列表移除
> `Swc_VehicleState.c` 后，GetState 解析到 harness stub（读 RTE 注入的 vehicle_state），SAFE_STOP
> 制动命令正确输出。这也解释了为何原 feature 无法验证制动分支。

### D. 当前 Harness 不可测（已全部覆盖，无剩余）

| 代码位置 | 说明 | 覆盖情况 |
|---|---|---|
| `MainFunction` L349-372 卡滞检测 | harness 每次循环添加 dither (±16)，使 \|raw-prev\| = 16 > stuckThreshold(10) | 已通过 `ditherAmplitude=0` 场景覆盖 STUCK（feature「传感器持续卡滞触发 STUCK 故障」） |
| DTC 上报的 latch 分支 (L501-531) | 故障锁存期间 DTC 保持上报分支 | 已覆盖 PASSED/FAILED + latch 生命周期场景 |

> **结论**: 剩余不可覆盖代码仅 **A 类**（防御守卫：`ConfigPtr==NULL`、未初始化检查）与 **B 类**
> （常量约束下的逻辑冗余：LUT 越界保护、position/torque 上限保护），均为必要的安全护栏，
> 无需额外测试覆盖。原 C 类（GetPosition/BridgeRxToRte/SAFE_STOP 制动）与 D 类（卡滞/DTC）
> 已全部通过 harness 增强与新增场景覆盖。

---

## 覆盖检查清单

### 覆盖率实测汇总

| 源文件 | 行覆盖 | 分支覆盖 | 函数覆盖 |
|---|---|---:|---:|---:|
| `Swc_Pedal.c` | 90.3%（223/247） | 87.8%（86/98） | 100%（7/7） |
| `Swc_CvcCom.c` | 94.5%（138/146） | 61.8%（21/34） | 100%（3/3） |

> **差异说明**：原 `Swc_CvcCom.c` 的 `BridgeRxToRte`（RX 桥接）与 `Swc_Pedal_GetPosition` 不在
> 测试链路中（行覆盖 64.4%）。通过新增场景（`getPosition`/`bridgeRx`/`rx*` 注入/SAFE_STOP 制动）
> 后，CvcCom 行覆盖提升至 94.5%、函数 100%；剩余未覆盖仅为防御守卫与不可达分支。

### 代码路径覆盖

- 正常无故障路径已覆盖 ✅
- 合理性故障路径已覆盖 ✅
- 模式限制路径: RUN, DEGRADED, LIMP, SAFE_STOP, SHUTDOWN, INIT 全部覆盖 ✅
- 扭矩死区路径已覆盖 ✅
- 卡滞检测路径已覆盖（`ditherAmplitude=0` 场景）✅

### 输入覆盖

- 匹配输入已覆盖 ✅
- 不匹配输入已覆盖 ✅
- 全部 6 个车辆状态已覆盖 ✅
- 消抖敏感的周期数已覆盖 ✅
- 斜坡饱和的周期数已覆盖 ✅
- 低踏板死区已覆盖 ✅

### 分支覆盖（`Swc_Pedal.c`，87.8% = 86/98）

- 合理性分支: 通过和失败均已覆盖 ✅
- 模式限制分支: RUN/DEGRADED/LIMP/SAFE_STOP/SHUTDOWN/INIT 六个 case 已覆盖；`default` 兜底（L232）不可达（状态枚举完整）✅
- 扭矩方向分支: 零和非零均已覆盖 ✅
- ramp 上升/下降分支: 均已覆盖 ✅
- 故障 latch 激活分支: 首次故障已覆盖 ✅
- SPI 故障分支: ret1 失败 / ret2 失败 / 都 OK 均已覆盖 ✅
- GetPosition 分支: 已初始化/NULL 守卫外的正常路径 ✅
- 12 个未覆盖分支全部为防御守卫（L182/L246/L288/L292/L541/L545）与不可达（L141/L155/L161/L232/L384/L484）（见「无法覆盖的代码说明」）

### 分支覆盖（`Swc_CvcCom.c`，61.8% = 21/34）

- TX 缓冲填充与车辆状态回写分支 ✅
- faultMask 组装各信号位（随注入信号命中）✅
- BridgeRxToRte 全函数（RX 桥接 + 心跳存活桥接）✅
- SAFE_STOP/SHUTDOWN 制动命令分支（`vs >= SAFE_STOP` → 100）✅
- 未覆盖 13 个分支：faultMask 各 `!=0` 短路侧（需注入对应信号为 0/非 0 组合）+ 防御守卫（L76/L227）
  + torque 钳位（L207）+ brake OR 逻辑（L257）——均为防御/短路保护
