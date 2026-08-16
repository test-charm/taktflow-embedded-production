# FZC 转向伺服控制 (Swc_Steering) E2E 测试设计

## 被测功能

**FZC ASW 转向伺服控制 SWC — 角度→PWM 映射、范围检查、速率限制、合理性检查、命令超时与回中（RTC）、SPI 故障、故障锁存与 3 级 PWM 禁用、GetAngle 诊断读取**

覆盖链路：

```text
RTE 命令角（FZC_SIG_STEER_CMD）
  → Swc_Steering_MainFunction（10ms 周期）
  → 范围检查（-45..+45 度）
  → 合理性检查（|输出-反馈| ≥ 5 度 × 50 周期）
  → 命令超时（10 周期无新命令 → CMD_TIMEOUT + RTC）
  → 速率限制（增加 ≤ 0.3 度/10ms，回中不限速）
  → 故障锁存（非超时故障 → 中性 PWM + 3 级禁用）
  → 角度→PWM 线性映射（-45..+45 → 1000..2000us）
  → Pwm_SetDutyCycle / Dio_WriteChannel（3 级禁用）
  → RTE 输出（当前角 / 故障 / PWM 禁用级别）
  → Dem DTC 报告（合理性/范围/超时/SPI 四类）

IoHwAb SPI 反馈（14-bit 原始角度，8191=0 度）
  → Swc_Steering_MainFunction 合理性检查与故障注入
```

与既有 ASW E2E（CVC `Swc_Pedal`/`Swc_VehicleState`/`Swc_EStop`/`Swc_CvcCom`）
一致，本测试通过测试专用 API 在原生测试框架内执行真实的
`Swc_Steering.c` 生产代码。转向命令经 RTE 注入，角度反馈经 IoHwAb
（SPI）注入，输出经 Pwm/Dio/RTE/DEM 观察。

## 被测代码流程图

```
                    ┌─────────────────────┐
                    │ Swc_Steering_Init    │
                    │ ConfigPtr==NULL?     │
                    │   Y→ 保持未初始化    │
                    │   N→ 状态全清零      │
                    └─────────┬───────────┘
                              │
               ┌──────────────▼──────────────┐
               │ Swc_Steering_MainFunction()  │
               └──────────────┬──────────────┘
                              │
         Step1: Initialized!=TRUE? ──Y──→ return（未初始化空转）
                              │N
         Step2: Rte_Read(STEER_CMD)
                · E_OK   → cmd=值，重置超时计数，置 FirstCmdReceived
                · E_NOT_OK → cmd=旧值，超时计数++（<0xFFFF 守卫）
                              │
         Step3: IoHwAb 读实际角度（14-bit→度）
                · E_NOT_OK → new_fault=SPI_FAIL
                              │
         Step4: 范围检查（仅无故障时）
                · cmd>+45 或 cmd<-45 → new_fault=OUT_OF_RANGE
                              │
         Step5: 合理性检查（无故障 && PlausArmed）
                · |prev输出 - 反馈| ≥ 阈值(5) → debounce++
                · debounce ≥ 50 → new_fault=PLAUSIBILITY
                · 否则 debounce 清零
                              │
         Step6: 命令超时（FirstCmdReceived && 计数≥10）
                · CmdTimedOut=TRUE；无其他故障 → new_fault=CMD_TIMEOUT
                              │
         Step7: 目标角
                · CmdTimedOut → RTC：向 0 移动（30°/s=3 十分位/周期）
                · 否则 → cmd×10
                              │
         Step8: 速率限制（增加受限 3 十分位/周期，回中不限速）
                              │
         Step9: 故障处理
                · 非超时故障 → 锁存、episode++、输出=0
                · 已锁存无新故障 → 计数，≥50 清除锁存
                · 锁存中 → 输出=0（强制中性）
                              │
         Step10: 更新 CurrentAngle/PrevAngle，PlausArmed=TRUE
         Step11: 角度→PWM（1500 + angle10×500/450，钳位 1000..2000）
                              │
         Step12: 3 级 PWM 禁用
                · episode≥3 → level 3（双 Dio 拉低）
                · episode≥2 → level 2（Dio ch10 拉低）
                · episode=1 → level 1（中性 PWM）
                · 否则 → 正常 PWM
                              │
         Step13: Rte_Write（当前角/故障/PWM 禁用级别）
                              │
         Step14: Dem DTC（合理性/范围/超时/SPI → FAILED 或 PASSED）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Steering_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `initNull` | 是否调用 `Swc_Steering_Init(NULL)` | `false`、`true`（NULL 配置守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`10`、`55`、`150`（速率/消抖/锁存需要） | When — 执行控制 |
| `cmdAngle` | RTE 命令角（度） | `0`（居中）、`30`/`-30`、`45`/`-45`（边界）、`46`/`-46`（越界） | When — 数据注入 |
| `rteReadFail` | `Rte_Read` 返回 E_NOT_OK | `false`（新鲜命令）、`true`（超时路径） | When — 故障注入 |
| `actualAngle` | IoHwAb 反馈角（度，转 14-bit） | `0`（匹配）、`30`（偏差 > 阈值） | When — 数据注入 |
| `actualTrack` | 反馈跟踪上一周期 RTE 输出 | `false`（固定反馈）、`true`（健康反馈） | When — 数据注入 |
| `spiFail` | `IoHwAb_ReadSteeringAngle` 返回 E_NOT_OK | `false`、`true`（SPI 故障） | When — 故障注入 |
| `getAngle` | 末尾调用 `Swc_Steering_GetAngle` | `false`、`true` | When — 执行控制 |
| `getAngleNull` | 末尾调用 `Swc_Steering_GetAngle(NULL)` | `false`、`true`（NULL 指针守卫） | When — 执行控制 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `currentAngle` | RTE `FZC_SIG_STEER_ANGLE`（整度） | `-45..45`、`0` |
| `faultStatus` | RTE `FZC_SIG_STEER_FAULT` | `0`=无、`1`=PLAUS、`2`=RANGE、`4`=TIMEOUT、`5`=SPI |
| `pwmDisableLevel` | RTE `FZC_SIG_STEER_PWM_DISABLE` | `0..3` |
| `pwmDuty` | `Pwm_SetDutyCycle` 最近写入值（us） | `1000..2000`、`0`（未初始化） |
| `dioCh10` / `dioCh11` | `Dio_WriteChannel` ch10/ch11 电平 | `0`/`1` |
| `demPlaus`/`demRange`/`demTimeout`/`demSpi` | `Dem_ReportErrorStatus` 每 DTC 最近状态 | `0`=PASSED、`1`=FAILED、`-1`=未报告 |
| `getAngleStatus` / `getAngle` | `Swc_Steering_GetAngle` 返回值/角度 | `0`=E_OK、`1`=E_NOT_OK |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与健康 PWM 输出**：Init 双守卫 / 角度→PWM 映射 /
>   GetAngle，共 7 场景。
> - **规则: 范围检查与速率限制**：越界故障 / 限速 / 回中不限速，共 5 场景。
> - **规则: 合理性检查 (Plausibility)**：消抖触发 / 未达门限 / 恢复清零，
>   共 3 场景。
> - **规则: 命令超时与回中 (Command Timeout + RTC)**：超时故障 / RTC 正负向 /
>   吸附到 0 / 未收过命令 / 超时不覆盖硬件故障，共 6 场景。
> - **规则: SPI 故障**：SPI 读取失败，共 1 场景。
> - **规则: 故障锁存与 3 级 PWM 禁用**：锁存强制中性 / 锁存清除 (Dem PASSED) /
>   2 级 Dio / 3 级双 Dio，共 4 场景。
>
> 每个用例经 `POST /api/test/asw/fzc/steering` 一次运行驱动真实
> `Swc_Steering.c`；多阶段脚本中阶段顺序执行、模块状态跨阶段保留
> （如先建立健康基线再注入故障）。

### 规则: 初始化守卫与健康 PWM 输出

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDuty | 期望 currentAngle | 期望 getAngleStatus |
|---|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true,getAngle=true | 0 | 0（无 PWM） | 0 | 1（E_NOT_OK） |
| init_null_guard | P0: initNull=true | 0 | 0（无 PWM） | 0 | — |
| healthy_center_pwm | P0: cmdAngle=0,actualAngle=0,getAngle=true | 0 | 1500 | 0 | 0（E_OK） |
| full_right_pwm_2000 | P0: cycles=150,cmdAngle=45,actualTrack=true | 0 | 2000 | 45 | — |
| full_left_pwm_1000 | P0: cycles=150,cmdAngle=-45,actualTrack=true | 0 | 1000 | -45 | — |
| get_angle_reads_current | P0: cycles=150,cmdAngle=45,actualTrack=true,getAngle=true | 0 | — | 45 | 0（E_OK） |
| get_angle_null_pointer | P0: cmdAngle=0,actualAngle=0,getAngleNull=true | 0 | — | 0 | 1（E_NOT_OK） |

### 规则: 范围检查与速率限制

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDisableLevel | 期望 pwmDuty | 期望 currentAngle |
|---|---|---|---|---|---|
| range_fault_above_max | P0: cmdAngle=46,actualTrack=true | 2（RANGE） | 1 | 1500 | 0 |
| range_fault_below_min | P0: cmdAngle=-46,actualTrack=true | 2（RANGE） | 1 | 1500 | 0 |
| rate_limit_cap_increase | P0: cycles=3,cmdAngle=45,actualTrack=true | 0 | 0 | 1510（3 周期=9 十分位） | 0 |
| rate_decrease_toward_center | 前置: cycles=150,cmdAngle=45; P0: cmdAngle=30 | 0 | 0 | 1833 | 30（立即） |
| rate_decrease_negative_center | 前置: cycles=150,cmdAngle=-45; P0: cmdAngle=-30 | 0 | 0 | 1167 | -30（立即） |

### 规则: 合理性检查 (Plausibility)

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDisableLevel | 期望 demPlaus |
|---|---|---|---|---|
| plausibility_fault_debounce | P0: cycles=55,cmdAngle=0,actualAngle=30 | 1（PLAUS） | 1 | 1（FAILED） |
| plausibility_below_debounce | P0: cycles=10,cmdAngle=0,actualAngle=30 | 0 | 0 | — |
| plausibility_debounce_reset | P0: cycles=5,cmdAngle=0,actualAngle=30; P1: cycles=60,cmdAngle=0,actualAngle=0 | 0 | 0 | — |

### 规则: 命令超时与回中 (Command Timeout + RTC)

| 用例 | 阶段序列 | 期望 faultStatus | 期望 currentAngle | 期望 pwmDuty | 期望 demTimeout |
|---|---|---|---|---|---|
| cmd_timeout_fault | P0: cmdAngle=20,actualTrack=true; P1: cycles=10,rteReadFail=true | 4（TIMEOUT） | 2 | 1530 | 1（FAILED） |
| rtc_positive_return | 前置: cycles=150,cmdAngle=45; P0: cycles=10,rteReadFail=true | 4（TIMEOUT） | 44 | 1996 | — |
| rtc_negative_return | 前置: cycles=150,cmdAngle=-45; P0: cycles=10,rteReadFail=true | 4（TIMEOUT） | -44 | 1004 | — |
| rtc_snap_to_center | 前置: cmdAngle=0,actualTrack=true; P0: cycles=10,rteReadFail=true | 4（TIMEOUT） | 0 | 1500 | — |
| timeout_no_first_cmd | P0: cycles=20,rteReadFail=true | 0（未触发） | 0 | 1500 | — |
| timeout_not_override_range | P0: cmdAngle=46,actualTrack=true; P1: cycles=12,rteReadFail=true | 2（RANGE 保留） | 0 | 1500 | — |

### 规则: SPI 故障

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDisableLevel | 期望 demSpi |
|---|---|---|---|---|
| spi_fail_immediate | P0: cmdAngle=0,actualAngle=0,spiFail=true | 5（SPI） | 1 | 1（FAILED） |

### 规则: 故障锁存与 3 级 PWM 禁用

| 用例 | 阶段序列 | 期望 faultStatus | 期望 pwmDisableLevel | 期望 dioCh10 | 期望 dioCh11 | 期望 demRange |
|---|---|---|---|---|---|---|
| latch_hold_neutral | P0: cmdAngle=46; P1: cycles=10,cmdAngle=0 | 0（RTE 故障清） | 1（锁存保持） | — | — | 1（FAILED 保持） |
| latch_clear_dem_passed | P0: cmdAngle=46; P1: cycles=55,cmdAngle=0 | 0 | 1（禁用级保持） | — | — | 0（PASSED） |
| fault_escalation_level2 | P0: cmd=46; P1: 55×cmd=0; P2: cmd=46 | 2 | 2 | 0 | — | — |
| fault_escalation_level3 | P0: cmd=46; P1: 55×cmd=0; P2: cmd=46; P3: 55×cmd=0; P4: cmd=46 | 2 | 3 | 0 | 0 | — |

> 阶段序列中未列出的因子取默认值（`cycles=1`、`cmdAngle=0`、
> `actualAngle=0`、`actualTrack=false`、各故障注入=0）。
> 「前置」列表示该阶段在 `FzcSteeringSetup`（`假如存在`）中给出，P0..Pn
> 为刺激阶段；服务端按「前置 + 刺激」顺序执行。

## 代码路径覆盖

- `Swc_Steering_Init` 全部可执行行 ✅
  - NULL 配置守卫（`ConfigPtr==NULL` → 保持未初始化）✅
  - 正常初始化（全部状态清零 + `Initialized=TRUE`）✅
- `Swc_Steering_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅
  - RTE 命令读取（E_OK / E_NOT_OK 双路径 + 超时计数 `<0xFFFF` 守卫）✅
  - SPI 反馈读取 + 故障注入 ✅
  - 范围检查（>+45 与 <-45 双侧）✅
  - 合理性检查（PlausArmed 门控 / 消抖累计 / 恢复清零）✅
  - 命令超时检测（FirstCmdReceived 两侧 + 超时不覆盖硬件故障）✅
  - RTC 回中（正向 / 负向 / 吸附到 0）✅
  - 速率限制（增加受限 / 回中不限速 / 直通）✅
  - 故障锁存（新 episode / 计数清除 / 锁存中强制中性）✅
  - 3 级 PWM 禁用（level 1/2/3 + 正常 0）✅
  - RTE 输出与 Dem DTC（四类 FAILED/PASSED/锁存保持）✅
- `Swc_Steering_GetAngle` 全部可执行行 ✅
  - 未初始化 → E_NOT_OK ✅
  - NULL 指针 → E_NOT_OK ✅
  - 正常读取 → E_OK ✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Steering.c.gcov.html` 实测（2026-08-16 全量套件 26 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **94.9%**（258 / 272 行） |
| **分支覆盖** | **91.8%**（101 / 110 分支） |
| **函数覆盖** | **100%**（6 / 6） |

覆盖到的函数（实测命中次数）：
`Steering_AbsDiffSint16`（11248）、`Steering_AngleToPwm`（4620）、
`Steering_ApplyRateLimit`（4620）、`Swc_Steering_Init`（74）、
`Swc_Steering_MainFunction`（4626）、`Swc_Steering_GetAngle`（13）。

> 下表「实测命中」为完整套件（26 个场景）运行后的累积值；每次运行因
> 容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（94.9%，258/272）

行覆盖反映**每一行是否被执行**。14 行未覆盖，全部为**防御性钳位 / 不可达
守卫**（见下方「未覆盖行说明」）。其余 258 行全部覆盖，逐行映射如下。

### 逐函数代码行覆盖映射

#### Swc_Steering_Init（L246-275）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L248-252 | `if (ConfigPtr == NULL)` → 保持未初始化 | `init_null_guard`（initNull=true） | 3 |
| L254-274 | 正常初始化（状态清零 + `Initialized=TRUE`） | 全部已初始化场景（healthy/fault 全链路） | 71 |

#### Steering_AngleToPwm（L159-185）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L173-174 | 线性映射 `pwm = 1500 + angle10×500/450` | 全部健康/回中场景 | 4620 |
| L184 | 返回 PWM | 全部 | 4620 |
| L165-170 | 角度钳位（`angle10 > +450` / `< -450`） | **未覆盖** — 防御性：范围检查 + 速率限制保证输出角 ∈ [-450,450] | 0 |
| L177-182 | PWM 钳位（`<1000` / `>2000`） | **未覆盖** — 防御性：角度 ∈ [-450,450] 映射恰为 [1000,2000]，永不出界 | 0 |

#### Steering_ApplyRateLimit（L199-240）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L208-210 | 取 `rateLimitDeg10ms`、计算 diff | 全部（每周期调用） | 4620 |
| L213-220 | `diff > max_step` 增加受限（含回中判定 L217-219） | `rate_limit_cap_increase`、`full_right_pwm_2000`、`rate_decrease_negative_center` | 2013 |
| L222-232 | `diff < -max_step` 负向受限（含回中判定 L227-229） | `rate_decrease_toward_center`、`full_left_pwm_1000` | 1347 |
| L234-237 | 速率内直通 | `healthy_center_pwm`、稳态回中 | 1260 |
| L205-207 | `if (CfgPtr == NULL)` 返回 0 | **未覆盖** — 不可达：MainFunction 已先判 CfgPtr（L298-300） | 0 |

#### Swc_Steering_MainFunction（L281-593）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L294-296 | 未初始化守卫 | `uninitialized_guard`（skipInit=true） | 6 |
| L298-300 | `if (CfgPtr == NULL)` 返回 | **未覆盖** — 不可达：`Init` 保证 `Initialized=TRUE` 时 `CfgPtr` 非空 | 0 |
| L301-303 | `new_fault = NO_FAULT` | 全部已初始化场景 | 4620 |
| L308-317 | RTE 命令读取（E_OK→cast / E_NOT_OK→保留旧值） | 健康/超时全链路 | 4620 |
| L324-329 | 新鲜命令 → 重置超时计数、置 FirstCmdReceived | 全部有命令场景 | 4438 |
| L330-333 | 无数据 → 计数++（`<0xFFFF` 守卫 true 侧） | `cmd_timeout_fault` 等超时场景 | 182 |
| L345-351 | SPI 读取 + 14-bit→度转换 | 全部场景 | 4620 |
| L352-353 | `ret != E_OK` → SPI_FAIL | `spi_fail_immediate` | — |
| L373-377 | 范围检查（cmd>45 / cmd<-45 → OUT_OF_RANGE） | `range_fault_above_max` / `range_fault_below_min` | 4617 |
| L387-405 | 合理性检查（PlausArmed 门控 / 消抖累计 / ≥50 触发 / 恢复清零） | `plausibility_fault_debounce` / `below_debounce` / `debounce_reset` | 4528 |
| L413-418 | 命令超时检测 + `new_fault` 不覆盖 | `cmd_timeout_fault`、`timeout_not_override_range`（L415 else 侧） | 14 |
| L425-446 | RTC 目标角（正向 L436-437 / 负向 L438-439 / 吸附 L440-443 / 正常 L444-446） | `rtc_positive_return` / `rtc_negative_return` / `rtc_snap_to_center` | 14 |
| L452-453 | 速率限制应用 | 全部 | 4620 |
| L457-483 | 故障处理（非超时故障锁存 L459-467 / 计数清除 L470-477 / 锁存中强制中性 L477-479） | `range_fault_*`、`latch_*` 系列 | 4620 |
| L488-490 | 更新 CurrentAngle/PrevAngle、PlausArmed=TRUE | 全部 | 4620 |
| L495-496 | 角度→PWM | 全部 | 4620 |
| L500-513 | 3 级禁用级别判定（episode≥3/≥2/≥1/0） | `fault_escalation_level3/2`、`latch_*`、健康场景 | 4620 |
| L517-544 | switch 3/2/1/0（双 Dio / Dio / 中性 / 正常） | level3/level2/level1/健康场景 | 4620 |
| L551-553 | Rte_Write（当前角/故障/PWM 禁用） | 全部 | 4620 |
| L559-565 | Dem 合理性 DTC（FAILED/PASSED/锁存保持） | `plausibility_fault_debounce` + 健康/锁存场景 | 4620 |
| L568-574 | Dem 范围 DTC | `range_fault_*` + 健康/锁存场景 | 4620 |
| L577-583 | Dem 超时 DTC | `cmd_timeout_fault` + 健康场景 | 4620 |
| L586-592 | Dem SPI DTC | `spi_fail_immediate` + 健康场景 | 4620 |

#### Swc_Steering_GetAngle（L599-613）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L601-603 | 未初始化 → E_NOT_OK | `uninitialized_guard`（skipInit+getAngle） | 3 |
| L605-607 | NULL 指针 → E_NOT_OK | `get_angle_null_pointer` | 3 |
| L610-612 | 正常读取当前角 → E_OK | `get_angle_reads_current`、`healthy_center_pwm` | 7 |

> 未列出的行号为声明、注释、空行或不可达分支占位行（llvm-cov/lcov 计入
> 非可执行行，见下方说明）。

---

## 未覆盖行说明（14 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L166-167 | `if (angle10 > +450)` 钳位 | **防御性**。范围检查（L372-377）与速率限制共同保证输出角恒 ∈ [-450,450]，`angle10` 永不超过 ±450，钳位分支不可达 |
| L169-170 | `if (angle10 < -450)` 钳位 | 同上 |
| L178-179 | `if (pwm < 1000us)` 钳位 | **防御性**。角度 ∈ [-450,450] 时 `pwm = 1500 + angle×500/450 ∈ [1000,2000]` 恰在边界，`pwm<1000` 不可能成立 |
| L181-182 | `if (pwm > 2000us)` 钳位 | 同上 |
| L206-207 | `ApplyRateLimit` 中 `if (CfgPtr==NULL) return 0` | **不可达**。`MainFunction` 已在 L298-300 前置判断 `CfgPtr` 非空；`Init` 保证 `Initialized=TRUE ⇔ CfgPtr≠NULL` |
| L299-300 | `MainFunction` 中 `if (CfgPtr==NULL) return` | **不可达**。`Swc_Steering_Init` 仅在传入非 NULL 配置时置 `Initialized=TRUE`；NULL 配置时 `Initialized=FALSE`，已在 L294-296 返回 |
| L433-434 | RTC 中 `if (rtc_step < 1) rtc_step = 1` | **不可达**。生产配置 `FZC_STEER_RTC_RATE_DEG_S=30`，`rtc_step = 30/10 = 3`，恒 ≥1。该守卫仅对异常低配置（<10°/s）生效 |

> 以上 14 行均为 ISO 26262 编码规范要求的防御性代码（双保险钳位 / 空指针
> 守卫 / 参数下限守卫），在**任何合法生产输入下都不可能触发**，属「结构
> 不可达」而非「测试遗漏」。单元测试中同样无法通过生产路径覆盖这些分支。

---

## 分支覆盖分析（91.8%，101/110）

未命中（not taken）的 9 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `angle10 > +450`（true 侧） | L165 | 防御性钳位，角度不可能越上界 |
| `angle10 < -450`（true 侧） | L168 | 防御性钳位，角度不可能越下界 |
| `pwm < 1000`（true 侧） | L175 | 防御性钳位，映射下界恰为 1000 |
| `pwm > 2000`（true 侧） | L180 | 防御性钳位，映射上界恰为 2000 |
| `CfgPtr == NULL`（ApplyRateLimit，true 侧） | L204 | 前置守卫（L298-300）已排除，不可达 |
| `CfgPtr == NULL`（MainFunction，true 侧） | L297 | Init 保证一致状态，不可达 |
| `CmdTimeoutCounter < 0xFFFF`（false 侧） | L332 | 需连续 65535 周期无命令才溢出；E2E 不可能，计数器饱和守卫 |
| `rtc_step < 1`（true 侧） | L431 | 生产配置 30°/s → step=3，恒 ≥1 |
| switch `default:`（非 0-3 值） | L540 | `PwmDisableLevel` 恒 ∈ {0,1,2,3}，case 0 覆盖后 default 不可达 |

> 全部 101 个命中的分支两侧均已覆盖；未命中分支全部为防御性/结构不可达。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 94.9%（258/272） | 14 行 | 全部为防御性钳位/不可达守卫 |
| 分支 | 91.8%（101/110） | 9 个 | 全部为防御性/不可达分支 |
| 函数 | 100%（6/6） | — | — |

**结论**：`Swc_Steering` 的全部可执行逻辑（含 6 个函数、4 类 DTC、3 级
禁用、RTC、速率限制、范围/合理性/超时/SPI 故障路径）均由 E2E 测试覆盖。
14 行未覆盖代码均为主机厂级防御性编程，通过生产输入无法触发，符合预期。
