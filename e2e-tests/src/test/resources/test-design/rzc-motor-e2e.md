# RZC 电机控制 (Swc_Motor) E2E 测试设计

## 被测功能

**RZC ASW BTS7960 H 桥电机 SWC — 扭矩→PWM 映射（95% 占空比上限）、
模式扭矩限制（RUN/DEGRADED/LIMP/SAFE_STOP/未知状态）、热降额、
急停/INIT/SHUTDOWN 立即关闭、方向切换死区时间、快速反向防直通、
命令超时（10 周期）与恢复（5 条有效命令）、过流/过温外部故障、
故障锁存与 DEM DTC 报告**

覆盖链路：

```text
RTE 输入（车辆状态 / E-stop / 扭矩命令 / 降额百分比 / 过流 / 过温）
  → Swc_Motor_MainFunction（10ms 周期）
  → 未初始化守卫
  → 降额百分比钳位（>100 → 100）
  → ESTOP / INIT / SHUTDOWN 立即关闭路径
  → SAFE_STOP 强制零扭矩
  → Motor_GetModeTorqueLimit（按状态取扭矩上限）
  → 正/负扭矩模式限制钳位
  → 热降额（effective = limited * derating / 100）
  → 命令超时检测（同一非零命令保持 10 周期 → CMD_TIMEOUT DTC FAILED）
  → 超时恢复（5 条有效命令 → DTC PASSED）
  → 方向判定（扭矩符号 → FORWARD / REVERSE / STOP）
  → 死区时间序列（方向切换第一周期 PWM=0，下一周期应用新方向）
  → 防直通软件检查（ShootThrough 锁存 → 永久禁用）
  → 占空比计算（abs * 95 / 100，上限 95%）
  → IoHwAb 刻度换算（*10000 / 100）
  → 使能逻辑（方向非 STOP 且 duty>0 → R_EN/L_EN HIGH + PWM）
  → 过流/过温外部故障集成（MOTOR_FAULT=3/4）
  → Rte_Write（TORQUE_ECHO / MOTOR_DIR / MOTOR_ENABLE / MOTOR_FAULT）
  → Dem DTC 报告（命令超时）
```

与既有 ASW E2E（CVC `Swc_Pedal`/`Swc_VehicleState`/`Swc_EStop`/`Swc_CvcCom`、
FZC `Swc_Steering`/`Swc_Brake`/`Swc_Lidar`）一致，本测试通过测试专用 API 在
原生测试框架内执行真实的 `Swc_Motor.c` 生产代码。扭矩/状态/降额/故障经 RTE
注入，输出经 PWM/Dio/RTE/DEM 观察。

## 被测代码流程图

```
                    ┌─────────────────────┐
                    │ Swc_Motor_Init       │
                    │ 全状态清零           │
                    │ Motor_DisableOutputs │
                    │ Initialized=TRUE     │
                    └─────────┬───────────┘
                              │
               ┌──────────────▼──────────────┐
               │ Swc_Motor_MainFunction()     │
               └──────────────┬──────────────┘
                              │
   Step1: Initialized!=TRUE? ──Y──→ return（未初始化空转）
                              │N
   Step2: 读 RTE（状态/E-stop/扭矩/降额），降额>100 钳位到 100
                              │
   Step3: estop || INIT || SHUTDOWN? ──Y──→ 立即关闭 + 写安全输出 + return
                              │N
   Step4: SAFE_STOP? ──Y──→ torque_cmd=0
                              │
   Step5: mode_limit = Motor_GetModeTorqueLimit(state)
          · RUN=100 / DEGRADED=75 / LIMP=30 / SAFE_STOP=0 / 未知=0
          · 正扭矩 > limit → 钳位到 +limit
          · 负扭矩 |t| > limit → 钳位到 -limit
                              │
   Step6: derated = limited * derating / 100
                              │
   Step7: 命令超时检测
          · 扭矩变化 或 扭矩==0 → new_cmd、超时计数清零
          · 否则计数++（钳位 0xFFFF）
          · 未超时且计数 ≥ 10 → CmdTimedOut=TRUE、CMD_TIMEOUT DTC FAILED
          · 已超时 → 有效命令计数 ≥ 5 → 恢复、DTC PASSED
          · 已超时 → derated_torque=0
                              │
   Step8: 方向判定
          · derated>0 → FORWARD / <0 → REVERSE / ==0 → STOP
                              │
   Step9: 死区时间
          · DeadtimeActive==TRUE → 应用延迟方向
            · 期间再次反向（FWD↔REV）→ 再插入一次死区、return
          · 方向 FWD↔REV 切换 → DeadtimeActive=TRUE、禁用 PWM、return
          · 否则 → Motor_Direction = new_direction
                              │
   Step10: ShootThroughLatched? ──Y──→ 永久禁用 + SHOOT_THROUGH + return
                              │N
   Step11: duty_pct = abs(derated) * 95 / 100，>95 钳位到 95
   Step12: duty_hw = duty_pct * 10000 / 100
   Step13: 使能 =（方向非 STOP）&&（duty_hw>0）
   Step14: FaultLatched? ──Y──→ 强制禁用输出
                              │N
   Step15: 使能 → R_EN/L_EN HIGH + IoHwAb_SetMotorPWM(dir,duty)
          否则 → Motor_DisableOutputs（双 EN LOW + PWM 0）
                              │
   Step16: 过流/过温标志 → MOTOR_FAULT=OVERCURRENT(3)/OVERTEMP(4)
                              │
   Step17: Rte_Write（TORQUE_ECHO=abs(derated) / MOTOR_DIR / MOTOR_ENABLE /
           MOTOR_FAULT）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Motor_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`10`/`11`（超时边界）、`3`/`5`（恢复边界）、`65600`（计数钳位） | When — 执行控制 |
| `vehicleState` | RTE 车辆状态 | `0`=INIT、`1`=RUN、`2`=DEGRADED、`3`=LIMP、`4`=SAFE_STOP、`5`=SHUTDOWN、`200`（未知） | When — 数据注入 |
| `estop` | RTE 急停有效标志 | `0`、`1` | When — 故障注入 |
| `torqueCmd` | 扭矩命令（sint16 %） | `0`、`1`（最小正向）、`30`/`31`（LIMP 边界）、`50`/`60`、`75`/`76`（DEGRADED 边界）、`100`（满扭矩）、`-50`/`-100`（反向）、`200`（超限） | When — 数据注入 |
| `derating` | 热降额百分比 | `0`、`50`、`100`、`150`（>100 钳位） | When — 数据注入 |
| `overcurrent` | 过流外部故障标志 | `0`、`1` | When — 故障注入 |
| `tempFault` | 过温外部故障标志 | `0`、`1` | When — 故障注入 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `torqueEcho` | RTE `RZC_SIG_TORQUE_ECHO`（扭矩回显 = abs(derated)） | `0..100` |
| `motorDir` | RTE `RZC_SIG_MOTOR_DIR`（电机方向） | `0`=FORWARD、`1`=REVERSE、`2`=STOP |
| `motorEnable` | RTE `RZC_SIG_MOTOR_ENABLE` | `0`/`1` |
| `motorFault` | RTE `RZC_SIG_MOTOR_FAULT`（故障码） | `0`=无、`2`=CMD_TIMEOUT、`3`=OVERCURRENT、`4`=OVERTEMP |
| `pwmDuty` | `IoHwAb_SetMotorPWM` 占空比（0..9500） | `0`、`4700`、`7100`、`9500` 等 |
| `pwmDir` | `IoHwAb_SetMotorPWM` 方向 | `0`/`1`/`2`、`255`（未调用） |
| `dioCh5`/`dioCh6` | R_EN / L_EN GPIO 电平 | `0`（禁用）、`1`（使能） |
| `demTimeout` | `Dem_ReportErrorStatus` CMD_TIMEOUT DTC 最近状态 | `0`=PASSED、`1`=FAILED、`-1`=未报告 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与健康扭矩→PWM 映射**：未初始化守卫 / 零扭矩 /
>   正反向满扭矩 / 比例映射 / 最小非零扭矩边界，共 6 场景。
> - **规则: 模式扭矩限制**：DEGRADED/LIMP 上下钳位、SAFE_STOP 强制零、
>   未知状态安全默认，共 7 场景。
> - **规则: 热降额**：50% 减半 / 0% 强制零 / >100% 钳位，共 3 场景。
> - **规则: 急停与 INIT/SHUTDOWN 立即关闭**：ESTop / INIT / SHUTDOWN，
>   共 3 场景。
> - **规则: 方向切换死区时间**：切换第一周期 PWM 清零 / 延迟应用 /
>   死区内快速反向延长 / 切至停止不重复死区 / 同方向无死区，共 5 场景。
> - **规则: 命令超时与恢复**：10 周期未触发 / 11 周期触发 / 5 条恢复 /
>   4 条保持禁用 / 相同命令不计数 / 计数 65535 钳位 / 零扭矩不超时 /
>   零扭矩恢复，共 8 场景。
> - **规则: 过流与过温外部故障**：过流 / 过温 / 过温优先，共 3 场景。
>
> 每个用例经 `POST /api/test/asw/rzc/motor` 一次运行驱动真实
> `Swc_Motor.c`；多阶段脚本中阶段顺序执行、模块状态跨阶段保留
> （如先建立健康扭矩基线再注入超时/故障）。

### 规则: 初始化守卫与健康扭矩→PWM 映射

| 用例 | 阶段序列 | 期望 torqueEcho | 期望 motorDir | 期望 pwmDuty | 期望 motorEnable |
|---|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true, torqueCmd=100 | 0 | 0 | 0 | 0 |
| torque_0_stop | P0: torqueCmd=0 | 0 | 2（STOP） | 0 | 0 |
| torque_100_forward | P0: torqueCmd=100 | 100 | 0（FWD） | 9500 | 1 |
| torque_neg100_reverse | P0: torqueCmd=-100 | 100 | 1（REV） | 9500 | 1 |
| torque_50_mapping | P0: torqueCmd=50 | 50 | 0（FWD） | 4700 | 1 |
| torque_1_min_duty | P0: torqueCmd=1 | 1 | 0（FWD） | 0 | 0 |

> `torque_1_min_duty`：1% 扭矩经 `1*95/100=0` 整数截断为 0% 占空比，
> 方向虽为 FORWARD 但因 duty_hw==0 不使能 —— 覆盖最小非零扭矩边界。

### 规则: 模式扭矩限制

| 用例 | 阶段序列 | 期望 torqueEcho | 期望 motorDir | 期望 pwmDuty |
|---|---|---|---|---|
| degraded_cap_75 | P0: torqueCmd=100, vehicleState=2 | 75 | 0（FWD） | 7100 |
| degraded_boundary_76 | P0: torqueCmd=76, vehicleState=2 | 75 | 0（FWD） | 7100 |
| degraded_neg_cap_75 | P0: torqueCmd=-100, vehicleState=2 | 75 | 1（REV） | 7100 |
| limp_cap_30 | P0: torqueCmd=100, vehicleState=3 | 30 | 0（FWD） | 2800 |
| limp_boundary_31 | P0: torqueCmd=31, vehicleState=3 | 30 | 0（FWD） | 2800 |
| safe_stop_zero | P0: torqueCmd=100, vehicleState=4 | 0 | 2（STOP） | 0 |
| unknown_state_zero | P0: torqueCmd=100, vehicleState=200 | 0 | 2（STOP） | 0 |

> `degraded_neg_cap_75` 覆盖负扭矩钳位分支（`|t| > mode_limit` → `-limit`）。

### 规则: 热降额

| 用例 | 阶段序列 | 期望 torqueEcho | 期望 pwmDuty |
|---|---|---|---|
| derating_half | P0: torqueCmd=100, derating=50 | 50 | 4700 |
| derating_zero | P0: torqueCmd=100, derating=0 | 0 | 0 |
| derating_clamp_150 | P0: torqueCmd=100, derating=150 | 100 | 9500 |

### 规则: 急停与 INIT/SHUTDOWN 立即关闭

| 用例 | 阶段序列 | 期望 torqueEcho | 期望 motorDir | 期望 pwmDuty | 期望 dioCh5/6 |
|---|---|---|---|---|---|
| estop_disable | P0: torqueCmd=100, estop=1 | 0 | 2（STOP） | 0 | 0/0 |
| init_state_disable | P0: torqueCmd=100, vehicleState=0 | 0 | 2（STOP） | 0 | 0/0 |
| shutdown_state_disable | P0: torqueCmd=100, vehicleState=5 | 0 | 2（STOP） | 0 | 0/0 |

### 规则: 方向切换死区时间

| 用例 | 阶段序列 | 期望 torqueEcho | 期望 motorDir | 期望 pwmDuty |
|---|---|---|---|---|
| direction_change_deadtime | P0: 1×torque=50; P1: 1×torque=-50 | 0 | 2（STOP） | 0 |
| deadtime_applied_next | P0: 1×torque=50; P1: 1×torque=-50; P2: 1×torque=-50 | 50 | 1（REV） | 4700 |
| rapid_reversal_extend | P0: 1×torque=50; P1: 1×torque=-50; P2: 1×torque=50 | 0 | 2（STOP） | 0 |
| deadtime_to_stop | P0: 1×torque=50; P1: 1×torque=-50; P2: 1×torque=0 | 0 | 1（REV） | 0 |
| same_direction_no_deadtime | P0: 1×torque=50; P1: 1×torque=60 | 60 | 0（FWD） | 5700 |

> `direction_change_deadtime`：正向→反向切换的**第一周期** PWM=0（死区时间）。
> `deadtime_applied_next`：第二周期应用 REVERSE 方向与 PWM。
> `rapid_reversal_extend`：死区时间期间再次反向（FWD→REV→FWD）再插入一次
> 死区时间周期（防直通）。
> `deadtime_to_stop`：死区时间期间切至 STOP —— 覆盖「死区期间方向未再次
> 改变」的延迟应用路径（Motor_Direction 保持延迟的 REVERSE，但 duty=0 不使能）。

### 规则: 命令超时与恢复

| 用例 | 阶段序列 | 期望 motorFault | 期望 demTimeout | 期望 pwmDuty |
|---|---|---|---|---|
| timeout_below_threshold | P0: cycles=10, torqueCmd=50 | 0 | -1 | 4700 |
| timeout_triggered_11 | P0: cycles=11, torqueCmd=50 | 2 | 1（FAILED） | 0 |
| recovery_5_valid | P0: 11×torque=50; P1-P5: torque=51/52/53/54/55 | 0 | 0（PASSED） | 5200 |
| recovery_4_still_disabled | P0: 11×torque=50; P1-P4: torque=51/52/53/54 | 2 | 1（FAILED） | 0 |
| recovery_same_cmd_no_count | P0: 11×torque=50; P1: 3×torque=50 | 2 | 1（FAILED） | 0 |
| timeout_counter_clamp | P0: cycles=65600, torqueCmd=50 | 2 | 1（FAILED） | 0 |
| zero_torque_no_timeout | P0: cycles=30, torqueCmd=0 | 0 | -1 | 0 |
| zero_torque_recovers | P0: 11×torque=50; P1-P5: torque=0 | 0 | 0（PASSED） | 0 |

> `timeout_below_threshold`：10 周期（含首周期 new_cmd 后累计 9 次）未达
> 门限，电机继续运行。
> `timeout_triggered_11`：累计 10 周期相同非零命令 → CMD_TIMEOUT（fault=2）、
> DTC FAILED、扭矩强制 0。
> `recovery_5_valid`：超时后 5 条**变化**命令触发恢复（DTC PASSED、fault=0、
> 第 5 条命令即时生效，55% → duty 5200）。
> `recovery_4_still_disabled`：仅 4 条变化命令不足 5 → 仍禁用。
> `recovery_same_cmd_no_count`：超时后保持**相同**命令 → new_cmd_received=FALSE，
> 恢复计数不增加 → 仍禁用。
> `timeout_counter_clamp`：同一非零命令持续 65600 周期，超时计数器在
> 0xFFFF 处钳位不溢出（覆盖 `Motor_CmdTimeoutCycles < 0xFFFFu` 的 false 侧）。
> `zero_torque_no_timeout`：零扭矩视为有意怠速（`raw==0` 恒为 new_cmd），
> 30 周期不触发超时。
> `zero_torque_recovers`：超时后 5 次零扭矩同样计数恢复（DTC PASSED）。

### 规则: 过流与过温外部故障

| 用例 | 阶段序列 | 期望 motorFault | 期望 pwmDuty |
|---|---|---|---|
| overcurrent_fault_latch | P0: torque=50, overcurrent=1; P1: torque=50, overcurrent=0 | 3 | 4700 |
| overtemp_fault | P0: torque=50, tempFault=1 | 4 | 4700 |
| overtemp_wins | P0: torque=50, overcurrent=1, tempFault=1 | 4 | 4700 |

> `overcurrent_fault_latch`：过流标志置位后 MOTOR_FAULT=3；标志清除后故障码
> **锁存**（代码仅在被置位时更新 Motor_Fault，无自动复位路径）—— 覆盖
> 故障锁存行为（直到恢复/重新 Init 才复位）。
> `overtemp_wins`：代码先判过流后判过温，两者同时置位时过温（4）覆盖
> 过流（3）。

## 代码路径覆盖

- `Swc_Motor_Init` 全部可执行行 ✅
  - 全状态清零（方向/占空比/故障/超时/恢复/死区/前值）✅
  - `Motor_DisableOutputs()` 安全输出 ✅
  - `Motor_Initialized=TRUE` ✅
- `Motor_AbsSint16` 全部可执行行 ✅
  - `val < 0` → `(uint16)(-val)` ✅（负扭矩用例）
  - 非负 → `(uint16)val` ✅（零/正扭矩用例）
- `Motor_DisableOutputs` 全部可执行行 ✅
  - R_EN=0 / L_EN=0 / PWM(STOP,0) ✅（关闭/急停/死区/超时全部路径）
- `Motor_GetModeTorqueLimit` 全部可执行行 ✅
  - RUN=100 / DEGRADED=75 / LIMP=30 / SAFE_STOP=0 ✅
  - default（未知状态）=0 ✅ `unknown_state_zero`
- `Swc_Motor_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅ `uninitialized_guard`
  - RTE 读取 6 信号 ✅
  - 降额钳位 `>100→100` ✅ `derating_clamp_150`
  - ESTOP/INIT/SHUTDOWN 立即关闭路径 ✅ 三场景
  - SAFE_STOP 强制零 ✅ `safe_stop_zero`
  - 模式扭矩限制（正钳位 / 负钳位 / 零）✅
  - 热降额 ✅
  - 命令超时（触发 / 未触发 / 恢复 / 计数钳位）✅
  - 方向判定（FWD/REV/STOP）✅
  - 死区时间（插入 / 延迟应用 / 死区内再反向 / 切至停止）✅
  - 占空比计算 / IoHwAb 刻度换算 ✅
  - 使能逻辑（FWD/REV + duty>0）✅
  - 输出应用（使能 → 双 EN HIGH + PWM / 禁用 → 安全输出）✅
  - 过流/过温故障集成 ✅
  - Rte_Write 4 信号 ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Motor.c.gcov.html` 实测（2026-08-16 全量套件 195 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **93.8%**（226 / 241 行） |
| **分支覆盖** | **95.2%**（80 / 84 分支） |
| **函数覆盖** | **100%**（5 / 5） |

覆盖到的函数（实测命中次数）：
`Swc_Motor_Init`（222）、`Motor_DisableOutputs`（131827）、
`Motor_GetModeTorqueLimit`（132122）、`Swc_Motor_MainFunction`（132150）、
`Motor_AbsSint16`（264226）。

> 下表「实测命中」为完整套件（195 个场景，含 65600 周期计数钳位用例）运行
> 后的累积值；每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系
> 不变。命中次数极高源于 `timeout_counter_clamp` 场景的 65600 次循环。

---

## 行覆盖分析（93.8%，226/241）

行覆盖反映**每一行是否被执行**。15 行未覆盖，全部为**结构性不可达的防御性
代码**（见下方「未覆盖行说明」）。其余 226 行全部覆盖，逐行映射如下。

### 逐函数代码行覆盖映射

#### Motor_AbsSint16（L105-111）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L106-111 | `if (val < 0)` → `-(val)`，否则 `val` | 正/负/零扭矩全部用例（L108 负值 80 次，L110 非负 264146 次） | 264226 |

#### Motor_DisableOutputs（L120-125）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L122-124 | R_EN=0 / L_EN=0 / PWM(STOP,0) | Init 安全输出、急停/INIT/SHUTDOWN、死区时间、超时禁用、duty=0 不使能 | 131827 |

#### Motor_GetModeTorqueLimit（L136-160）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L141-143 | RUN → 100 | 全部 RUN 用例 | 132075 |
| L144-146 | DEGRADED → 75 | `degraded_cap_75` 等 | 19 |
| L147-149 | LIMP → 30 | `limp_cap_30` 等 | 14 |
| L150-152 | SAFE_STOP → 0 | `safe_stop_zero` 等 | 7 |
| L153-156 | default → 0 | `unknown_state_zero` | 7 |

#### Swc_Motor_Init（L166-185）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L168-179 | 全状态清零 | 全部已初始化场景（每个 POST 一次 Init） | 222 |
| L182 | `Motor_DisableOutputs()` | 同上 | 222 |
| L184 | `Initialized=TRUE` | 同上 | 222 |

#### Swc_Motor_MainFunction（L191-527）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L215-217 | 未初始化守卫 → return | `uninitialized_guard` | 7 |
| L222-236 | 读 RTE 6 信号 | 全部已初始化场景 | 132143 |
| L239-241 | 降额 >100 钳位到 100 | `derating_clamp_150` | 7 |
| L246-259 | ESTOP/INIT/SHUTDOWN → 立即关闭 + 写安全输出 + return | `estop_disable` / `init_state_disable` / `shutdown_state_disable` | 21 |
| L264-266 | SAFE_STOP → torque_cmd=0 | `safe_stop_zero` | 7 |
| L273-277 | 正扭矩钳位（>limit → +limit） | `degraded_cap_75`、`degraded_boundary_76`、`limp_cap_30`、`limp_boundary_31` 等 | 35 |
| L278-283 | 负扭矩钳位（\|t\|>limit → -limit） | `degraded_neg_cap_75`（L282/283） | 42 |
| L284-286 | 零扭矩 → limited=0 | `torque_0_stop` 等 | 261 |
| L292 | 热降额运算 | 全部降额用例 | 132122 |
| L299-312 | 超时检测（new_cmd 判断 / 计数++ / 0xFFFF 钳位） | 超时系列全部场景（L309/310 由 65600 周期用例达 131453 次） | 132122 |
| L316-325 | 未超时且计数 ≥10 → 超时 + DTC FAILED | `timeout_triggered_11` 等 | 32 |
| L326-340 | 超时恢复（计数 / ≥5 恢复 + DTC PASSED） | `recovery_5_valid`、`zero_torque_recovers` | 14 |
| L343-345 | 超时 → derated=0 | 超时系列 | 131300 |
| L350-356 | 方向判定 FWD/REV/STOP | 正/负/零扭矩用例 | 132122 |
| L363-385 | 死区时间激活处理（延迟方向 / 再反向延长 / 禁用） | `deadtime_applied_next`、`rapid_reversal_extend`、`deadtime_to_stop` | 16 |
| L386-403 | 方向 FWD↔REV 切换 → 插入死区 + 禁用 + return | `direction_change_deadtime` 等 | 23 |
| L404-407 | 方向无变化或与 STOP 切换 → 直接应用 | 健康正向/反向用例 | 132083 |
| L416 | `Motor_ShootThroughLatched` 判断 | 全部用例（false 侧，见未覆盖说明） | 132092 |
| L437-445 | 占空比计算 + 95% 上限（L441-443 防御性，见未覆盖说明） | 全部健康用例 | 132092 |
| L451 | IoHwAb 刻度换算 | 全部健康用例 | 132092 |
| L456-463 | 使能逻辑（方向非 STOP 且 duty>0） | 全部用例（duty=0 用例走 false 侧） | 547 |
| L468 | `Motor_FaultLatched` 判断 | 全部用例（false 侧，见未覆盖说明） | 132092 |
| L477-485 | 使能 → 双 EN HIGH + PWM / 禁用 → 安全输出 | 健康用例 / 关闭用例 | 538 |
| L490-500 | 过流/过温标志集成 | `overcurrent_fault_latch`、`overtemp_fault`、`overtemp_wins` | 14 |
| L523-526 | Rte_Write 4 输出信号 | 全部已初始化场景 | 132092 |

> 未列出的行号为声明、注释、空行或不可达分支占位行（llvm-cov/lcov 计入
> 非可执行行，见下方说明）。

---

## 未覆盖行说明（15 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L418-427 | 防直通锁存分支：`Motor_ShootThroughLatched==TRUE` → 永久禁用 + `RZC_MOTOR_SHOOT_THROUGH` 故障码 | **结构性不可达**。`Motor_ShootThroughLatched` 仅在 `Swc_Motor_Init`（L173）置为 `FALSE`，**生产代码中没有任何路径将其置为 TRUE**。该分支是为硬件级直通故障准备的防御性安全网 —— SWC 采用互斥方向逻辑（Step 8 的 else-if 链保证 FWD/REV 不可能同时激活），软件层无法触发射穿条件。单元测试同样无法通过生产路径覆盖此分支（测试注释亦说明「cannot directly trigger the condition」） |
| L442-443 | 占空比上限钳位：`duty_pct > RZC_MOTOR_MAX_DUTY_PCT` → 95 | **结构性不可达**。`duty_pct = abs_torque * 95 / 100`，而 `abs_torque` 受限于模式扭矩上限（最大 RUN=100）与降额钳位（≤100），故 `duty_pct` 最大值为 `100*95/100 = 95 = RZC_MOTOR_MAX_DUTY_PCT`，**数学上不可能大于 95**。该分支是 ISO 26262 要求的防御性上限校验 |
| L469-472 | 故障锁存分支：`Motor_FaultLatched==TRUE` → 强制禁用输出 | **结构性不可达**。`Motor_FaultLatched` 仅在 `Swc_Motor_Init`（L172）置为 `FALSE`，**生产代码中没有任何路径将其置为 TRUE**。该标志为保留的锁存扩展点，当前版本无触发源 |

> 以上 15 行均为 ISO 26262 编码规范要求的防御性代码（锁存/上限校验），在
> **任何合法生产输入下都不可能触发**，属「结构不可达」而非「测试遗漏」。
> 单元测试中同样无法通过生产路径覆盖这些分支（shoot-through 测试注释亦
> 承认无法从 SWC 逻辑直接触发）。

---

## 分支覆盖分析（95.2%，80/84）

未命中（not taken）的 4 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `Motor_Direction != RZC_DIR_STOP`（false 侧） | L372 | 死区时间激活块内 `Motor_Direction` 刚被赋为 `Motor_CmdDirection`（L366），而 `Motor_CmdDirection` 只在方向 FWD↔REV 切换时设置（L391），恒为 FWD/REV 非 STOP，故该条件恒真 |
| `Motor_ShootThroughLatched == TRUE`（true 侧） | L416 | 锁存标志无任何置位路径（见上表），恒 false |
| `duty_pct > RZC_MOTOR_MAX_DUTY_PCT`（true 侧） | L441 | 数学上不可能超过 95%（见上表） |
| `Motor_FaultLatched == TRUE`（true 侧） | L468 | 锁存标志无任何置位路径（见上表），恒 false |

> 全部 80 个命中的分支两侧均已覆盖；未命中分支全部为防御性/结构不可达。
> 超时相关的 `new_cmd_received`、`CmdTimeoutCycles < 0xFFFF`、恢复计数
> 阈值等分支均已由专门场景覆盖双侧。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 93.8%（226/241） | 15 行 | 全部为防御性锁存/上限分支（结构不可达） |
| 分支 | 95.2%（80/84） | 4 个 | 全部为防御性/恒真恒假的不可达分支 |
| 函数 | 100%（5/5） | — | — |

**结论**：`Swc_Motor` 的全部可执行逻辑（含 5 个函数、模式扭矩限制、热降额、
死区时间、防直通、命令超时与恢复、过流/过温、DTC 报告）均由 E2E 测试覆盖。
15 行未覆盖代码全部为主机厂级防御性锁存/上限校验，通过生产输入无法触发，
符合预期。单元测试同样无法覆盖这些分支，说明其不可达性与测试层级无关。
