# FZC 刹车伺服控制 (Swc_Brake) E2E 测试设计

## 被测功能

**FZC ASW 刹车伺服控制 SWC — 命令钳位、E-stop 全刹、命令超时与自动刹车、
命令振荡检测、PWM 偏差反馈验证、故障锁存、电机切断序列、GetPosition 诊断读取**

覆盖链路：

```text
RTE 刹车命令（FZC_SIG_BRAKE_CMD）
  → Swc_Brake_MainFunction（10ms 周期）
  → 命令钳位（0-100）
  → E-stop 标志（FZC_SIG_ESTOP_ACTIVE → 立即 100% 刹车）
  → 命令超时检测（9 周期无新命令 → CMD_TIMEOUT + 自动刹车）
  → 命令振荡检测（|Δcmd| > 30% × 4 周期 → CMD_OSCILLATION）
  → Pwm_SetDutyCycle（TIM2_CH2，0-100% 占空比）
  → IoHwAb ADC 反馈（0-1000 计数 → 0-100%）
  → PWM 偏差反馈验证（|cmd-pos| > 2% × 50 周期 → PWM_DEVIATION）
  → 故障锁存（强制 100% 刹车 + PWM 禁用，50 周期无故障清除）
  → 电机切断序列（MOTOR_CUTOFF 重复 10 次）
  → RTE 输出（位置 / 故障 / 电机切断 / PWM 禁用）
  → Dem DTC 报告（PWM 偏差 / 超时 / 振荡 / 刹车故障 四类）
```

与既有 ASW E2E（CVC `Swc_Pedal`/`Swc_VehicleState`/`Swc_EStop`/`Swc_CvcCom`、
FZC `Swc_Steering`）一致，本测试通过测试专用 API 在原生测试框架内执行真实的
`Swc_Brake.c` 生产代码。刹车命令经 RTE 注入，E-stop 标志经 RTE 注入，
位置反馈经 IoHwAb（ADC）注入，输出经 Pwm/RTE/DEM 观察。

## 被测代码流程图

```
                    ┌─────────────────────┐
                    │ Swc_Brake_Init       │
                    │ ConfigPtr==NULL?     │
                    │   Y→ 保持未初始化    │
                    │   N→ 状态全清零      │
                    └─────────┬───────────┘
                              │
               ┌──────────────▼──────────────┐
               │ Swc_Brake_MainFunction()     │
               └──────────────┬──────────────┘
                              │
         Step1: Initialized!=TRUE? ──Y──→ return（未初始化空转）
                              │N
         Step2: Rte_Read(BRAKE_CMD)
                · E_OK   → 重置超时计数、置 FirstCmdReceived
                · E_NOT_OK → 超时计数++（<0xFFFF 守卫）
                · 钳位到 0-100
                              │
         Step3: Rte_Read(ESTOP_ACTIVE)
                · 有效 → cmd=100、AutoBrake=TRUE、FaultLatched=TRUE
                              │
         Step4: 命令超时（AutoBrake==FALSE && FirstCmdReceived）
                · 计数≥9 → new_fault=CMD_TIMEOUT、自动刹车
                              │
         Step5: 振荡检测（无故障 && 未锁存）
                · |Δcmd|>30% → 振荡计数++
                · 计数≥4 → new_fault=CMD_OSCILLATION
                · 否则复位；锁存/故障时清零
                              │
         Step6: AutoBrake → cmd=100
         Step7: Pwm_SetDutyCycle(2, cmd)
                              │
         Step8: IoHwAb 读实际位置（0-1000 → /10 = 0-100%）
                · E_NOT_OK → 保留旧位置（失效安全）
                              │
         Step9: 反馈验证（无故障 && FirstCmdReceived）
                · |cmd-pos|>阈值(2) → 消抖计数++
                · 计数≥50 → new_fault=PWM_DEVIATION
                · 否则消抖计数清零
                              │
         Step10: 故障处理
                · 新故障 → 锁存、强制 100%、启动切断序列
                · 已锁存无新故障 → 计数，≥50 清除锁存
                · 锁存中 → 保持故障码、强制 100%
                · 正常 → Fault=NO_FAULT
                              │
         Step11: 电机切断序列（CutoffSending）
                · 计数<10 → Rte_Write(MOTOR_CUTOFF,1)、计数++
                · 计数≥10 → CutoffSending=FALSE
                              │
         Step12: Rte_Write（位置/故障/电机切断/PWM 禁用）
                              │
         Step13: Dem DTC（PWM 偏差/超时/振荡 → FAILED；健康 → PASSED）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Brake_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `initNull` | 是否调用 `Swc_Brake_Init(NULL)` | `false`、`true`（NULL 配置守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`9`、`10`、`50`、`55`（超时/消抖/锁存需要） | When — 执行控制 |
| `cmdBrake` | RTE 刹车命令（%） | `0`（无刹车）、`50`/`60`/`80`（中间）、`100`（全刹边界）、`150`（超限钳位） | When — 数据注入 |
| `rteReadFail` | `Rte_Read` 返回 E_NOT_OK | `false`（新鲜命令）、`true`（超时路径） | When — 故障注入 |
| `estop` | RTE E-stop 标志 | `0`（正常）、`1`（E-stop 激活） | When — 数据注入 |
| `actualPos` | IoHwAb ADC 反馈（0-1000 计数） | `0`（无反馈/偏差）、`500`/`600`（匹配）、`800`（匹配） | When — 数据注入 |
| `actualTrack` | 反馈跟踪命令（健康） | `false`（固定反馈）、`true`（健康反馈） | When — 数据注入 |
| `posReadFail` | `IoHwAb_ReadBrakePosition` 返回 E_NOT_OK | `false`、`true`（读取失败路径） | When — 故障注入 |
| `getPos` | 末尾调用 `Swc_Brake_GetPosition` | `false`、`true` | When — 执行控制 |
| `getPosNull` | 末尾调用 `Swc_Brake_GetPosition(NULL)` | `false`、`true`（NULL 指针守卫） | When — 执行控制 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `brakePosition` | RTE `FZC_SIG_BRAKE_POS`（当前刹车位置 %） | `0..100`、`0`（未初始化） |
| `faultStatus` | RTE `FZC_SIG_BRAKE_FAULT` | `0`=无、`1`=PWM_DEVIATION、`2`=CMD_TIMEOUT、`3`=LATCHED、`4`=CMD_OSCILLATION |
| `pwmDuty` | `Pwm_SetDutyCycle` 最近写入值（%） | `0..100`、`0`（未初始化） |
| `motorCutoff` | RTE `FZC_SIG_MOTOR_CUTOFF` | `0`/`1` |
| `pwmDisable` | RTE `FZC_SIG_BRAKE_PWM_DISABLE` | `0`/`1` |
| `demPwmFail`/`demTimeout`/`demOsc`/`demBrakeFault` | `Dem_ReportErrorStatus` 每 DTC 最近状态 | `0`=PASSED、`1`=FAILED、`-1`=未报告 |
| `getPosStatus` / `getPos` | `Swc_Brake_GetPosition` 返回值/位置 | `0`=E_OK、`1`=E_NOT_OK |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与健康 PWM 输出**：Init 双守卫 / 命令钳位 /
>   健康 PWM 映射 / GetPosition，共 8 场景。
> - **规则: E-stop 立即全刹**：E-stop 强制 100% 刹车 + 锁存保持，共 2 场景。
> - **规则: 命令超时与自动刹车**：超时故障 / 未收过命令不触发 /
>   超时覆盖 PWM 偏差故障，共 3 场景。
> - **规则: 命令振荡检测**：振荡触发 / 未达消抖 / 复位，共 3 场景。
> - **规则: PWM 偏差反馈验证**：消抖触发 / 高反馈触发 / 未达门限 / 恢复清零，
>   共 4 场景。
> - **规则: 故障锁存与电机切断**：锁存保持 / 锁存清除 (Dem PASSED) /
>   反馈读取失败保留旧位置，共 3 场景。
>
> 每个用例经 `POST /api/test/asw/fzc/brake` 一次运行驱动真实
> `Swc_Brake.c`；多阶段脚本中阶段顺序执行、模块状态跨阶段保留
> （如先建立健康基线再注入故障）。

### 规则: 初始化守卫与健康 PWM 输出

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDuty | 期望 brakePosition | 期望 getPosStatus |
|---|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true,getPos=true | 0 | 0（无 PWM） | 0 | 1（E_NOT_OK） |
| init_null_guard | P0: initNull=true | 0 | 0（无 PWM） | 0 | — |
| healthy_no_brake_pwm | P0: cmdBrake=0,actualTrack=true | 0 | 0 | 0 | — |
| healthy_mid_brake_pwm | P0: cmdBrake=60,actualTrack=true | 0 | 60 | 60 | — |
| healthy_full_brake_pwm | P0: cmdBrake=100,actualTrack=true | 0 | 100 | 100 | — |
| clamp_above_max | P0: cmdBrake=150,actualTrack=true | 0 | 100（钳位） | 100 | — |
| get_position_reads_current | P0: cmdBrake=50,actualPos=500,getPos=true | 0 | — | 50 | 0（E_OK） |
| get_position_null_pointer | P0: getPosNull=true | 0 | — | 0 | 1（E_NOT_OK） |

### 规则: E-stop 立即全刹

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDuty | 期望 brakePosition | 期望 motorCutoff | 期望 pwmDisable |
|---|---|---|---|---|---|---|
| estop_forces_full_brake | P0: cmdBrake=20,estop=1,actualTrack=true | 3（LATCHED） | 100 | 100 | 1 | 1 |
| estop_latch_persists | P0: cmdBrake=20,estop=1,actualTrack=true; P1: cycles=10,cmdBrake=20,actualTrack=true | 3（LATCHED） | 100 | 100 | 1 | 1 |

### 规则: 命令超时与自动刹车

| 用例 | 阶段序列 | 期望 faultStatus | 期望 brakePosition | 期望 demTimeout | 期望 demBrakeFault |
|---|---|---|---|---|---|
| cmd_timeout_auto_brake | 前置: cmdBrake=20,actualTrack=true; P0: cycles=9,rteReadFail=true | 2（CMD_TIMEOUT） | 100 | 1（FAILED） | 1（FAILED） |
| timeout_no_first_cmd | P0: cycles=20,rteReadFail=true | 0（未触发） | 0 | — | — |
| timeout_overrides_pwm_dev | P0: cycles=50,cmdBrake=80,actualPos=0; P1: cycles=12,rteReadFail=true | 2（CMD_TIMEOUT 覆盖） | 100 | 1（FAILED） | 1（FAILED） |

> 注意：与 `Swc_Steering` 不同，`Swc_Brake` 的超时门控仅检查
> `AutoBrakeActive==FALSE && FirstCmdReceived==TRUE`，不检查已锁存故障。
> 因此 PWM 偏差故障锁存后，若再发生 9 周期 RTE 读取失败，超时故障会
> **覆盖** PWM 偏差故障（faultStatus 从 1 变为 2）。这是生产代码的真实行为，
> 测试将其固化（见「代码路径覆盖」）。

### 规则: 命令振荡检测

| 用例 | 阶段序列 | 期望 faultStatus | 期望 demOsc | 期望 demBrakeFault |
|---|---|---|---|---|
| oscillation_4_transitions | P0-P4: cmdBrake 依次 0,50,0,50,0,actualTrack=true | 4（CMD_OSCILLATION） | 1（FAILED） | 1（FAILED） |
| oscillation_below_debounce | P0-P2: cmdBrake 依次 0,50,50,actualTrack=true | 0 | — | — |
| oscillation_reset_on_steady | P0-P2: cmdBrake 依次 0,50,50,actualTrack=true | 0 | — | — |

> 振荡检测基于**相邻周期命令变化量**：`|Δcmd|>30%` 计一次，连续 4 次
> 触发 `CMD_OSCILLATION`。0→50→50 因第二次 Δ=0 会复位计数器，与
> `oscillation_below_debounce` 语义一致（稳态命令复位计数器）。

### 规则: PWM 偏差反馈验证

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDuty | 期望 demPwmFail |
|---|---|---|---|---|
| pwm_deviation_debounce_fault | P0: cycles=50,cmdBrake=80,actualPos=0 | 1（PWM_DEVIATION） | 100 | 1（FAILED） |
| pwm_deviation_high_feedback | P0: cycles=50,cmdBrake=50,actualPos=600 | 1（PWM_DEVIATION） | 100 | 1（FAILED） |
| pwm_deviation_below_debounce | P0: cycles=10,cmdBrake=80,actualPos=0 | 0 | 80 | — |
| pwm_deviation_recovery_reset | P0: cycles=5,cmdBrake=80,actualPos=0; P1: cycles=60,cmdBrake=80,actualTrack=true | 0 | 80 | — |

### 规则: 故障锁存与电机切断

| 用例 | 阶段序列 | 期望 faultStatus | 期望 brakePosition | 期望 motorCutoff | 期望 pwmDisable | 期望 demPwmFail |
|---|---|---|---|---|---|---|
| latch_hold_forces_brake | P0: cycles=50,cmdBrake=80,actualPos=0; P1: cycles=10,cmdBrake=80,actualTrack=true | 1（PWM_DEVIATION 保持） | 100 | 1 | 1 | 1（FAILED 保持） |
| latch_clear_dem_passed | P0: cycles=50,cmdBrake=80,actualPos=0; P1: cycles=55,cmdBrake=80,actualTrack=true | 0 | 80 | 0 | 0 | 0（PASSED） |
| pos_read_fail_keeps_stale | P0: cmdBrake=60,actualPos=600; P1: cmdBrake=60,posReadFail=true | 0 | 60（旧位置保留） | 0 | 0 | — |

> 阶段序列中未列出的因子取默认值（`cycles=1`、`cmdBrake=0`、
> `actualPos=0`、`actualTrack=false`、各故障注入=0）。
> 「前置」列表示该阶段在 `FzcBrakeSetup`（`假如存在`）中给出，P0..Pn
> 为刺激阶段；服务端按「前置 + 刺激」顺序执行。

## 代码路径覆盖

- `Swc_Brake_Init` 全部可执行行 ✅
  - NULL 配置守卫（`ConfigPtr==NULL` → 保持未初始化）✅
  - 正常初始化（全部状态清零 + `Initialized=TRUE`）✅
- `Swc_Brake_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅
  - RTE 命令读取（E_OK / E_NOT_OK 双路径 + 超时计数 `<0xFFFF` 守卫）✅
  - 命令钳位（`>100` → 100）✅
  - E-stop 读取 + 立即全刹 ✅
  - 命令超时检测（AutoBrake 门控 / FirstCmdReceived 两侧 / 覆盖已锁存故障）✅
  - 振荡检测（Δ 计算双向 / 计数++ / 复位 / 锁存时清零）✅
  - 自动刹车覆盖 ✅
  - PWM 驱动 ✅
  - ADC 位置读取（E_OK / E_NOT_OK 保留旧值）✅
  - 反馈验证（偏差双向 / 消抖累计 / ≥50 触发 / 恢复清零）✅
  - 故障处理（新故障锁存 / 锁存计数清除 / 锁存中强制 100% / 正常）✅
  - 电机切断序列（发送中计数 / 完成停止）✅
  - RTE 输出（位置/故障/电机切断三态/PWM 禁用）✅
  - Dem DTC（PWM 偏差/超时/振荡 FAILED；健康 PASSED；锁存保持）✅
- `Swc_Brake_GetPosition` 全部可执行行 ✅
  - 未初始化 → E_NOT_OK ✅
  - NULL 指针 → E_NOT_OK ✅
  - 正常读取 → E_OK ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Brake.c.gcov.html` 实测（2026-08-16 全量套件 131 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **99.0%**（202 / 204 行） |
| **分支覆盖** | **96.2%**（75 / 78 分支） |
| **函数覆盖** | **100%**（3 / 3） |

覆盖到的函数（实测命中次数）：
`Swc_Brake_Init`（100）、`Swc_Brake_MainFunction`（2146）、
`Swc_Brake_GetPosition`（10）。

> 下表「实测命中」为完整套件（131 个场景）运行后的累积值；每次运行因
> 容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（99.0%，202/204）

行覆盖反映**每一行是否被执行**。2 行未覆盖为**防御性空指针守卫**
（见下方「未覆盖行说明」）。其余 202 行全部覆盖，逐行映射如下。

### 逐函数代码行覆盖映射

#### Swc_Brake_Init（L116-144）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L118-122 | `if (ConfigPtr == NULL)` → 保持未初始化 | `init_null_guard`（initNull=true） | 5 |
| L124-143 | 正常初始化（状态清零 + `Initialized=TRUE`） | 全部已初始化场景（healthy/fault 全链路） | 95 |

#### Swc_Brake_MainFunction（L150-452）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L161-163 | 未初始化守卫 | `uninitialized_guard`（skipInit=true）、`init_null_guard` | 10 |
| L165-167 | `if (CfgPtr == NULL)` 返回 | **未覆盖** — 不可达：`Init` 保证 `Initialized=TRUE ⇔ CfgPtr≠NULL` | 0 |
| L169-171 | `new_fault/cmd_raw/estop` 清零 | 全部已初始化场景 | 2136 |
| L178-191 | RTE 命令读取（E_OK→重置计数 / E_NOT_OK→计数++） | 健康/超时全链路 | 2136 |
| L185-186 | 新鲜命令 → 重置超时计数、置 FirstCmdReceived | 全部有命令场景 | 1929 |
| L188-190 | 无数据 → 计数++（`<0xFFFF` 守卫 true 侧） | `cmd_timeout_auto_brake` 等超时场景 | 207 |
| L195-199 | 命令钳位（>100→100 / 直通） | `clamp_above_max`（L196 钳位侧）；其余场景（L198 直通侧） | 2136 |
| L204-212 | E-stop 读取 + 立即全刹锁存 | `estop_forces_full_brake`、`estop_latch_persists` | 9 |
| L219-226 | 命令超时检测（AutoBrake 门控 / 计数≥9） | `cmd_timeout_auto_brake`、`timeout_overrides_pwm_dev`（L221-225）；`timeout_no_first_cmd`（L219 门控 false） | 2136 |
| L236-258 | 振荡检测（Δ 计算双向 L239-243 / Δ>30 计数 L245-248 / 复位 L249-251 / ≥4 触发 L253-255 / 锁存清零 L256-258） | `oscillation_4_transitions`（L253-255）；`oscillation_steady`（L249-251）；`estop_latch_persists`（L256-258） | 2136 |
| L260-262 | 更新 OscPrevCmd / PrevBrakeCmd | 全部 | 2136 |
| L268-270 | AutoBrake 覆盖 → cmd=100 | `estop_*`、`cmd_timeout_*`、锁存场景 | 98 |
| L277 | Pwm_SetDutyCycle（TIM2_CH2, cmd） | 全部 | 2136 |
| L286-294 | ADC 位置读取（E_OK→/10 / E_NOT_OK→保留旧值） | 全部；`pos_read_fail_keeps_stale`（L289 false 侧） | 2136 |
| L302-322 | 反馈验证（偏差双向 L303-307 / >阈值消抖 L309-318 / 恢复清零 L319-321） | `pwm_deviation_debounce_fault`（L304、L316-318）；`pwm_deviation_high_feedback`（L306 高反馈侧）；`pwm_deviation_below_debounce`（L320 清零侧）；`pwm_deviation_recovery_reset` | 2040 |
| L340-353 | 新故障处理（锁存 / 强制 100% / 启动切断 L350-353） | 全部故障场景（超时/振荡/PWM 偏差/E-stop 后续） | 38 |
| L354-377 | 已锁存处理（计数 L356-357 / 清除 L364-369 / 保持 L370-377） | `latch_clear_dem_passed`（L364-369）；`latch_hold_forces_brake`、`estop_latch_persists`（L370-377） | 2098 |
| L372-374 | 锁存中无具体故障码 → `Fault=LATCHED` | `estop_forces_full_brake`（L372-374） | 9 |
| L378-381 | 正常操作 → `Fault=NO_FAULT` | healthy 全部场景 | 1621 |
| L388-395 | 电机切断序列（发送中计数 L389-391 / 完成停止 L392-394） | 全部故障场景（L389-391）；`latch_clear_dem_passed`（L392-394 切断完成） | 204 |
| L408-409 | Rte_Write（位置/故障） | 全部 | 2136 |
| L416-422 | 电机切断三态输出（发送中 / 故障活动 / 正常） | 全部 | 2136 |
| L425-429 | PWM 禁用标志（锁存→1 / 正常→0） | 全部 | 2136 |
| L434-451 | Dem DTC 报告 | `pwm_deviation_*`（L435-436）；`cmd_timeout_*`（L438-439）；`oscillation_4`（L441-442）；healthy/latch_clear（L445-448 PASSED）；`latch_hold`/`estop_latch`（L449-451 无报告） | 2136 |

#### Swc_Brake_GetPosition（L458-471）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L460-462 | 未初始化 → E_NOT_OK | `uninitialized_guard`（skipInit+getPos） | 6 |
| L464-466 | NULL 指针 → E_NOT_OK | `get_position_null_pointer` | 5 |
| L468-470 | 正常读取当前位置 → E_OK | `get_position_reads_current` | 5 |

> 未列出的行号为声明、注释、空行或不可达分支占位行（llvm-cov/lcov 计入
> 非可执行行，见下方说明）。

---

## 未覆盖行说明（2 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L165-167 | `MainFunction` 中 `if (CfgPtr==NULL) return` | **不可达**。`Swc_Brake_Init` 仅在传入非 NULL 配置时置 `Initialized=TRUE`；NULL 配置时 `Initialized=FALSE`，已在 L161-163 返回。`Initialized=TRUE ⇔ CfgPtr≠NULL` 恒成立 |

> 以上 2 行均为 ISO 26262 编码规范要求的防御性空指针守卫，在**任何合法
> 生产输入下都不可能触发**，属「结构不可达」而非「测试遗漏」。与
> `Swc_Steering` 的同类守卫分析一致。

---

## 分支覆盖分析（96.2%，75/78）

未命中（not taken）的 3 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `CfgPtr == NULL`（true 侧） | L165 | 防御性空指针守卫，Init 保证不可达 |
| `CmdTimeoutCounter < 0xFFFF`（false 侧） | L188 | 需连续 65535 周期无命令才溢出；E2E 不可能，计数器饱和守卫 |
| `OscillationCounter < 0xFF`（false 侧） | L246 | 需连续 255 次大跳变才溢出；E2E 不可能，计数器饱和守卫 |

> 全部 75 个命中的分支两侧均已覆盖；未命中分支全部为防御性/结构不可达。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 99.0%（202/204） | 2 行 | 全部为防御性空指针守卫 |
| 分支 | 96.2%（75/78） | 3 个 | 全部为防御性/计数器饱和守卫 |
| 函数 | 100%（3/3） | — | — |

**结论**：`Swc_Brake` 的全部可执行逻辑（含 3 个函数、4 类 DTC、E-stop 锁存、
超时/振荡/PWM 偏差故障路径、电机切断序列、命令钳位、ADC 反馈保留旧值、
GetPosition 诊断）均由 E2E 测试覆盖。2 行未覆盖代码均为主机厂级防御性
编程（空指针守卫），通过生产输入无法触发，符合预期。

### 关键行为固化说明

1. **超时覆盖已锁存故障**：`timeout_overrides_pwm_dev` 场景证明
   `Swc_Brake` 的超时门控（`AutoBrakeActive==FALSE && FirstCmdReceived`）
   **不检查**已锁存故障，因此 PWM 偏差故障锁存后再丢失命令 9 周期，
   超时故障会覆盖 PWM 偏差故障（faultStatus 1→2）。这与 `Swc_Steering`
   的「超时不覆盖硬件故障」语义不同，是 `Swc_Brake.c` 的真实行为，测试
   予以固化；若后续产品决策要求「首个故障优先级更高」，应修改生产代码
   并在超时门控中增加 `FaultLatched` 检查。

2. **E-stop 故障码为 LATCHED**：E-stop 激活走「已锁存」分支，`Fault`
   置为 `FZC_BRAKE_LATCHED`（3），与超时/振荡/PWM 偏差的独立故障码
   不同，Dem 不额外上报（L449-451 保持无报告）。

