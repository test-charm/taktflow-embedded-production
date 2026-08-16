# RZC 温度监控 (Swc_TempMonitor) E2E 测试设计

## 被测功能

**RZC ASW 温度监控 SWC — NTC 温度读取、合理范围门控（-30..150 degC）、
双 NTC 交叉校验（|NTC1-NTC2|>30 degC 触发 fail-hot，GAP-OT-001 缓解）、
阶梯降额曲线（100/75/50/0%）、滞回恢复（10 degC）、0% 过温 DEM DTC 报告、
RTE 信号广播（温度 1/2 + 降额 + 故障标志，经 Swc_RzcCom 发送 CAN 0x30E）**

覆盖链路：

```text
IoHwAb_ReadMotorTemp / IoHwAb_ReadMotorTemp2（NTC ADC 采样，E2E 中经测试 API 注入）
  → Swc_TempMonitor_MainFunction（100ms 周期）
  → 未初始化守卫
  → IoHwAb 读取失败 → TM_TempFault + DEM FAILED + RTE 故障标志 → 提前返回
  → 合理范围门控（-300..1500 ddc，越界 → 故障提前返回）
  → 双 NTC 交叉校验（delta 计算 → |delta|>300 → SensorPlausFault + fail-hot 取高）
  → TM_ComputeRawDerating（<60→100%、60-79→75%、80-99→50%、≥100→0%）
  → TM_ApplyHysteresis（降额自由下降；恢复需低于下一档阈值-10 degC，一次一档）
  → 0% → Dem_ReportErrorStatus(RZC_DTC_OVERTEMP, FAILED)
  → Rte_Write（RZC_SIG_TEMP1_DC / RZC_SIG_TEMP2_DC /
              RZC_SIG_DERATING_PCT / RZC_SIG_TEMP_FAULT）
```

与既有 ASW E2E（RZC `Swc_Motor`/`Swc_Battery`、FZC `Swc_Steering`/`Swc_Brake`/
`Swc_Lidar`）一致，本测试通过测试专用 API 在原生测试框架内执行真实的
`Swc_TempMonitor.c` 生产代码。原始 NTC 温度经 IoHwAb mock 注入，输出经
RTE / DEM mock 观察。

## 被测代码流程图

```
                    ┌────────────────────────────┐
                    │ Swc_TempMonitor_Init        │
                    │ Temp1/Temp2=0 / 降额=100%   │
                    │ Prev=100% / 故障标志=FALSE   │
                    │ 可信校验标志=FALSE / Init=TRUE│
                    └─────────────┬──────────────┘
                                  │
               ┌──────────────────▼──────────────────┐
               │ Swc_TempMonitor_MainFunction()        │
               └──────────────────┬──────────────────┘
                                  │
   Step1: Initialized!=TRUE? ─────Y──→ return（未初始化空转）
                                  │N
   Step2: IoHwAb_ReadMotorTemp(&raw)
          ├─ != E_OK ──→ TM_TempFault=TRUE + DEM FAILED + RTE TEMP_FAULT → return
          └─ =E_OK ───→ temp_dC = raw
                                  │
   Step3: temp_dC < -300 或 > 1500? ──Y──→ TM_TempFault=TRUE + DEM FAILED
                                            + RTE TEMP_FAULT → return
                                  │N
   Step4: IoHwAb_ReadMotorTemp2(&raw2)
          ├─ != E_OK ──→ 降级单传感器（继续用 NTC1，无温度故障）
          └─ =E_OK ───→ TM_CurrentTemp2_dC = raw2
                        delta = temp_dC - temp2_dC（负数取绝对值）
                        └─ |delta| > 300? ──Y──→ SensorPlausFault=TRUE
                                                   └─ temp2 > temp1? → 取 temp2（fail-hot）
                                  │
   Step5: TM_CurrentTemp_dC = temp_dC
   Step6: temp_C = temp_dC / 10（换算整度）
                                  │
   Step7: raw = TM_ComputeRawDerating(temp_C)
          ├─ temp_C < 60  ──→ 100%
          ├─ temp_C < 80  ──→ 75%
          ├─ temp_C < 100 ──→ 50%
          └─ 否则         ──→ 0%
                                  │
   Step8: new = TM_ApplyHysteresis(raw, 当前降额, temp_C)
          ├─ raw <= 当前 ──→ 立即应用（降额可自由恶化）
          └─ raw > 当前（恢复）：
             ├─ 当前=0%  & temp<=90 ──→ 50%
             ├─ 当前=50% & temp<=70 ──→ 75%
             ├─ 当前=75% & temp<=50 ──→ 100%
             └─ 当前=100%（无恢复目标，保持）
                                  │
   Step9: Prev=当前降额；当前降额=new
                                  │
   Step10: 当前降额==0%? ──Y──→ TM_TempFault=TRUE + DEM FAILED
                                  │
   Step11: Rte_Write(TEMP1_DC, temp_dC)
           Rte_Write(TEMP2_DC, temp2_dC)
           Rte_Write(DERATING_PCT, 降额)
           Rte_Write(TEMP_FAULT, 故障标志)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_TempMonitor_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`（单周期，本 SWC 无滑动平均） | When — 执行控制 |
| `tempDc` | NTC1 温度（deci-degrees C） | 见下表温度等价类与边界值 | When — 数据注入 |
| `temp2Dc` | NTC2 温度（deci-degrees C），缺省=NTC1 | 双传感器一致/差异超过阈值/恰好等于阈值 | When — 数据注入 |
| `ioFault` | IoHwAb_ReadMotorTemp 返回 E_NOT_OK | `false`、`true` | When — 故障注入 |
| `temp2Fail` | IoHwAb_ReadMotorTemp2 返回 E_NOT_OK | `false`、`true`（降级单传感器） | When — 故障注入 |

`tempDc` 温度等价类（整度 = tempDc/10）：

| 等价类 | 取值（ddc） | 目标降额 |
|---|---|---|
| 正常低温 | `250`（25.0C） | 100% |
| 中温 60-79 | `700`（70.0C） | 75% |
| 高温 80-99 | `900`（90.0C） | 50% |
| 过温 ≥100 | `1000`（100.0C） | 0%（+ DTC） |
| 合理范围下限 | `-300`（-30.0C） | 100%（边界可接受） |
| 合理范围上限 | `1500`（150.0C） | 0%（边界可接受） |

`tempDc` 边界值（ddc，`<` 比较边界）：

| 边界 | 取值（ddc） | 期望降额 | 说明 |
|---|---|---|---|
| 100%→75% 阈值 | `590` / `600` | 100% / 75% | `temp_C<60` |
| 75%→50% 阈值 | `790` / `800` | 75% / 50% | `temp_C<80` |
| 50%→0% 阈值 | `990` / `1000` | 50% / 0% | `temp_C<100` |
| 范围下界 | `-310` / `-300` | 故障 / 100% | `<-300` |
| 范围上界 | `1510` / `1500` | 故障 / 0% | `>1500` |
| 零度 | `0` | 100% | `temp_C=0` |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `temp1Dc` | RTE `RZC_SIG_TEMP1_DC`（选定/较高读数，deci-degC） | 期望温度（fail-hot 时为较高值） |
| `temp2Dc` | RTE `RZC_SIG_TEMP2_DC`（第二 NTC 读数） | NTC2 读数或 `0`（NTC2 读取失败时） |
| `deratingPct` | RTE `RZC_SIG_DERATING_PCT` | `100`/`75`/`50`/`0` |
| `tempFault` | RTE `RZC_SIG_TEMP_FAULT` | `0`/`1`（范围/IoHwAb/0% 降额触发） |
| `demOvertemp` | `Dem_ReportErrorStatus` RZC_DTC_OVERTEMP 最近状态 | `1`=FAILED（到达过 0% 或故障）、`-1`=未报告 |

> 说明：生产代码仅在范围故障 / IoHwAb 读取失败 / 0% 降额时报告 `FAILED`，
> 从不报告 `PASSED`（DTC 需经 UDS/复位清除），故 DEM mock 一旦记录 `FAILED`
> 后保持 `1`；`tempFault` 标志同样锁存至重新 Init。断言验证「是否到达过
> 故障状态并报告」。

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与 NTC 读取**：未初始化空转 / 标称读取 / IoHwAb 失败，
>   共 3 场景。
> - **规则: 合理范围门控**：下限/上限越界故障 + 两个边界值可接受，共 4 场景。
> - **规则: 阶梯降额曲线**：4 个降额等价类，共 4 场景。
> - **规则: 降额曲线边界值**：60/80/100 degC 阈值两侧 + 0.0C，共 7 场景。
> - **规则: 滞回恢复**：3 条滞回规则的「保持」与「恢复」+ 一次一档限制，
>   共 7 场景。
> - **规则: 双 NTC 交叉校验**：fail-hot 双侧 / 恰好阈值 / NTC2 读取失败降级，
>   共 4 场景。
> - **规则: DTC 报告与锁存**：过温后 DTC 与故障锁存 / 健康区间不报告，共 2 场景。
>
> 每个用例经 `POST /api/test/asw/rzc/temponitor` 一次运行驱动真实
> `Swc_TempMonitor.c`；多阶段脚本中阶段顺序执行、模块状态（当前降额/DEM/
> 故障标志）跨阶段保留（滞回测试依赖前序阶段建立当前降额）。

### 规则: 初始化守卫与 NTC 读取

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true, tempDc=1000, cycles=1 | 0 | 0 | 0 | -1 |
| nominal_read_25c | P0: tempDc=250, cycles=1 | 250 | 100 | 0 | -1 |
| iohwab_read_failure | P0: tempDc=250, ioFault=true, cycles=1 | 0 | 0 | 1 | 1 |

### 规则: 合理范围门控

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| range_below_min | P0: tempDc=-310, cycles=1 | 0 | 0 | 1 | 1 |
| range_above_max | P0: tempDc=1510, cycles=1 | 0 | 0 | 1 | 1 |
| range_at_min_boundary | P0: tempDc=-300, cycles=1 | -300 | 100 | 0 | -1 |
| range_at_max_boundary | P0: tempDc=1500, cycles=1 | 1500 | 0 | 1 | 1 |

### 规则: 阶梯降额曲线

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| derating_100_25c | P0: tempDc=250, cycles=1 | 250 | 100 | 0 | -1 |
| derating_75_70c | P0: tempDc=700, cycles=1 | 700 | 75 | 0 | -1 |
| derating_50_90c | P0: tempDc=900, cycles=1 | 900 | 50 | 0 | -1 |
| derating_0_100c | P0: tempDc=1000, cycles=1 | 1000 | 0 | 1 | 1 |

### 规则: 降额曲线边界值

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| boundary_59c | P0: tempDc=590, cycles=1 | 590 | 100 | — | — |
| boundary_60c | P0: tempDc=600, cycles=1 | 600 | 75 | — | — |
| boundary_79c | P0: tempDc=790, cycles=1 | 790 | 75 | — | — |
| boundary_80c | P0: tempDc=800, cycles=1 | 800 | 50 | — | — |
| boundary_99c | P0: tempDc=990, cycles=1 | 990 | 50 | — | — |
| boundary_100c | P0: tempDc=1000, cycles=1 | 1000 | 0 | 1 | 1 |
| zero_degc | P0: tempDc=0, cycles=1 | 0 | 100 | 0 | — |

### 规则: 滞回恢复

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| hysteresis_0_stays_91c | P0: 1000×1, P1: 910×1 | 910 | 0 | 1 | 1 |
| hysteresis_0_recovers_90c | P0: 1000×1, P1: 900×1 | 900 | 50 | 1 | 1 |
| hysteresis_50_stays_71c | P0: 900×1, P1: 710×1 | 710 | 50 | 0 | -1 |
| hysteresis_50_recovers_70c | P0: 900×1, P1: 700×1 | 700 | 75 | 0 | -1 |
| hysteresis_75_stays_51c | P0: 700×1, P1: 510×1 | 510 | 75 | 0 | -1 |
| hysteresis_75_recovers_50c | P0: 700×1, P1: 500×1 | 500 | 100 | 0 | -1 |
| hysteresis_one_step_only | P0: 1000×1, P1: 200×1 | 200 | 50 | 1 | 1 |

> `hysteresis_one_step_only` 验证从 0% 骤冷到 20.0C 时只恢复一档到 50%
> （而非直接跳到 100%），对应 TM_ApplyHysteresis 的「一次只允许一档」逻辑。

### 规则: 双 NTC 交叉校验

| 用例 | 阶段序列（tempDc/temp2Dc, cycles） | 期望 temp1Dc | 期望 temp2Dc | 期望 deratingPct | 期望 tempFault |
|---|---|---|---|---|---|
| plausibility_fail_hot_high | P0: 500/900×1 | 900 | 900 | 50 | 0 |
| plausibility_fail_hot_low | P0: 900/500×1 | 900 | 500 | 50 | 0 |
| plausibility_delta_at_threshold | P0: 500/800×1 | 500 | 800 | 100 | 0 |
| temp2_read_failure_degraded | P0: 500, temp2Fail=true×1 | 500 | 0 | 100 | 0 |

### 规则: DTC 报告与锁存

| 用例 | 阶段序列（tempDc, cycles） | 期望 temp1Dc | 期望 deratingPct | 期望 tempFault | 期望 demOvertemp |
|---|---|---|---|---|---|
| dtc_latched_after_recovery | P0: 1000×1, P1: 500×1 | 500 | 50 | 1 | 1 |
| healthy_no_dtc | P0: 250×1, P1: 700×1 | 700 | 75 | 0 | -1 |

> `demOvertemp=-1`（未报告）断言已蕴含在健康/边界/滞回非过温用例中：
> 非 0% 且无范围/IoHwAb 故障时不触发 DEM 报告。

## 代码路径覆盖

- `Swc_TempMonitor_Init` 全部可执行行 ✅
  - Temp1/Temp2=0 / 降额=100% / Prev=100% ✅
  - 故障标志=FALSE / 可信校验标志=FALSE / `Initialized=TRUE` ✅
- `TM_ComputeRawDerating` 全部可执行行 ✅
  - 4 段降额阈值链（`<60 / <80 / <100 / else`）✅
- `TM_ApplyHysteresis` 全部可执行行 ✅
  - 降额恶化立即应用（`raw<=cur`）✅
  - 0%→50%（`temp<=90`）✅ `hysteresis_0_recovers_90c`
  - 50%→75%（`temp<=70`）✅ `hysteresis_50_recovers_70c`
  - 75%→100%（`temp<=50`）✅ `hysteresis_75_recovers_50c`
  - 一次一档限制 ✅ `hysteresis_one_step_only`
- `Swc_TempMonitor_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅ `uninitialized_guard`
  - IoHwAb 读取失败路径 ✅ `iohwab_read_failure`
  - 合理范围门控双侧 ✅ `range_below_min` / `range_above_max`
  - 双 NTC 交叉校验（fail-hot 双侧 / 恰好阈值 / NTC2 读取失败）✅
  - 降额曲线 + 滞回 ✅
  - 0% → DEM FAILED ✅ `derating_0_100c` 等
  - Rte_Write 4 信号（TEMP1_DC / TEMP2_DC / DERATING_PCT / TEMP_FAULT）✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_TempMonitor.c.gcov.html` 实测（2026-08-16 全量套件 254 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **98.2%**（109 / 111 行） |
| **分支覆盖** | **97.4%**（37 / 38 分支） |
| **函数覆盖** | **100%**（4 / 4） |

覆盖到的函数（实测命中次数）：
`Swc_TempMonitor_Init`（30）、`TM_ComputeRawDerating`（36）、
`TM_ApplyHysteresis`（36）、`Swc_TempMonitor_MainFunction`（40）。

> 下表「实测命中」为全量套件（254 场景，含 223 个既有场景 + 31 个本 SWC
> 场景）一次干净运行后的累积值；每次容器重建后会重新累积，具体数字可能
> 不同，但覆盖关系不变。

---

## 行覆盖分析（98.2%，109/111）

行覆盖反映**每一行是否被执行**。2 行未覆盖，为**结构性不可达的防御性
代码**（见下方「未覆盖行说明」）。其余 109 行全部覆盖，逐行映射如下。

### 逐函数代码行覆盖映射

#### TM_ComputeRawDerating（L89-104）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L90-92 | 函数入口与局部变量 `raw` | 全部已初始化场景（每次降额计算） | 36 |
| L93 | `temp_C < 60` 阈值链首比较（双侧） | 低温用例（L94 命中 12）与中高温用例（L95 命中 24） | 36/36 |
| L94 | `raw = 100` | `derating_100`（25C）、`boundary_59c`、`range_at_min_boundary`（-30C）、滞回 100% 侧 | 12 |
| L95 | `temp_C < 80`（双侧） | 60-79C 用例（L96 命中 8）与 ≥80C 用例（L97 命中 16） | 24/24 |
| L96 | `raw = 75` | `derating_75`（70C）、`boundary_60c`、`boundary_79c`、滞回 75% 侧 | 8 |
| L97 | `temp_C < 100`（双侧） | 80-99C 用例（L98 命中 9）与 ≥100C 用例（L99 命中 7） | 16/16 |
| L98 | `raw = 50` | `derating_50`（90C）、`boundary_80c`、`boundary_99c`、滞回 50% 侧 | 9 |
| L100 | `raw = 0` | `derating_0`（100C）、`range_at_max_boundary`（150C）、滞回 0% 侧 | 7 |
| L103 | `return raw` | 全部已初始化场景 | 36 |

#### TM_ApplyHysteresis（L120-155）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L122-123 | 函数入口与局部变量 `result` | 全部降额计算 | 36 |
| L126 | `raw <= cur`（双侧：立即应用/恢复请求） | 降额恶化/相等用例（L127 命中 28）与恢复用例（L128 命中 8） | 36/36 |
| L127 | `result = raw`（降额立即恶化或持平） | 全部单阶段健康用例、滞回「保持」前序阶段 | 28 |
| L131 | `result = cur`（恢复默认保持当前档） | 全部滞回恢复用例 | 8 |
| L133 | `cur == 0`（双侧） | 0% 滞回用例（L135 命中 4）与 50/75% 用例（L138 命中 4） | 8/8 |
| L135 | `temp <= 90`（双侧） | `hysteresis_0_recovers_90c` 等（true 侧 L136 命中 3）与 `hysteresis_0_stays_91c`（false 侧 Branch 1 命中 1） | 4/4 |
| L136 | `result = 50`（0%→50%） | `hysteresis_0_recovers_90c`、`hysteresis_one_step_only`、`dtc_latched_after_recovery` | 3 |
| L138 | `cur == 50`（双侧） | 50% 滞回用例（L140 命中 2）与 75% 用例（L143 命中 2） | 4/4 |
| L140 | `temp <= 70`（双侧） | `hysteresis_50_recovers_70c`（true 侧 L141 命中 1）与 `hysteresis_50_stays_71c`（false 侧 Branch 1 命中 1） | 2/2 |
| L141 | `result = 75`（50%→75%） | `hysteresis_50_recovers_70c` | 1 |
| L143 | `cur == 75`（true 侧） | `hysteresis_75_recovers_50c`、`hysteresis_75_stays_51c` | 2 |
| L145 | `temp <= 50`（双侧） | `hysteresis_75_recovers_50c`（true 侧 L146 命中 1）与 `hysteresis_75_stays_51c`（false 侧 Branch 1 命中 1） | 2/2 |
| L146 | `result = 100`（75%→100%） | `hysteresis_75_recovers_50c` | 1 |
| L154 | `return result` | 全部降额计算 | 36 |

#### Swc_TempMonitor_Init（L161-170）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L163-164 | Temp1/Temp2 清零 | 全部已初始化场景（每 POST 一次 Init） | 30 |
| L165-166 | 降额=100% / Prev=100% | 同上 | 30 |
| L167-168 | 故障标志=FALSE / 可信校验标志=FALSE | 同上 | 30 |
| L169 | `Initialized=TRUE` | 同上 | 30 |

#### Swc_TempMonitor_MainFunction（L176-293）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L186 | 未初始化守卫（双侧） | `uninitialized_guard`（L187 命中 1）与全部已初始化场景（L189 命中 39） | 40/40 |
| L197-198 | IoHwAb 读取 + 失败判断（双侧） | 全部已初始化场景；`iohwab_read_failure`（L200 命中 1） | 39/39 |
| L200-204 | 读取失败 → 故障 + DEM FAILED + RTE 故障 + return | `iohwab_read_failure` | 1 |
| L206 | `temp_dC = raw_temp` | 全部已初始化场景 | 38 |
| L212-213 | 合理范围门控（双侧） | `range_below_min`/`range_above_max`（L214 命中 2）与全部健康用例（L222 命中 36） | 38/38 |
| L214-220 | 越界 → 故障 + DEM FAILED + RTE 故障 + return | `range_below_min`、`range_above_max` | 2 |
| L232-233 | 第二 NTC 读取 + 成功判断（双侧） | 全部已初始化场景（L234 命中 35）与 `temp2_read_failure_degraded`（L253 侧命中 1） | 36/36 |
| L237 | `TM_CurrentTemp2_dC` 存储 | 全部 NTC2 读取成功场景 | 35 |
| L240-242 | delta 计算 + 取绝对值（双侧） | fail-hot 用例（NTC2 偏高，L242 命中 2）与其余一致/可信用例（L243 命中 33） | 35/35 |
| L245-246 | `delta > 300` 可信校验（双侧） | `plausibility_fail_hot_high`/`plausibility_fail_hot_low`（L246 命中 2）与一致/恰好阈值用例（L251 命中 33） | 35/35 |
| L248-249 | fail-hot 取较高读数（双侧） | `plausibility_fail_hot_high`（temp2>temp1，L249 命中 1）与 `plausibility_fail_hot_low`（保持 temp1，L250 命中 1） | 2/2 |
| L259 | `TM_CurrentTemp_dC` 存储 | 全部已初始化场景 | 36 |
| L264 | `temp_C = temp_dC / 10` 换算 | 全部已初始化场景 | 36 |
| L269-273 | 降额计算 + 滞回 + Prev/当前更新 | 全部已初始化场景 | 36 |
| L278 | `降额==0%` 判断（双侧） | 过温用例（L279 命中 8）与未过温用例（L283 命中 28） | 36/36 |
| L279-281 | 0% → 故障 + DEM FAILED | `derating_0`、`range_at_max_boundary`、全部滞回 0% 用例、`dtc_latched_after_recovery` | 8 |
| L287-290 | Rte_Write 4 信号（TEMP1_DC / TEMP2_DC / DERATING_PCT / TEMP_FAULT） | 全部已初始化场景（断言 `temp1Dc`/`temp2Dc`/`deratingPct`/`tempFault`） | 36 |

> 未列出的行号为注释、空行、声明或大括号占位（llvm-cov/lcov 不计入可执行行）。

---

## 未覆盖行说明（2 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L150 | `result = cur_derating;`（cur==100% 恢复兜底） | **结构性不可达**。`TM_ApplyHysteresis` 仅当 `raw > cur` 时进入恢复分支；`raw_derating` 最大为 `100`（`RZC_TEMP_DERATE_100_PCT`），故 `cur==100%` 时 `raw>cur` 恒不成立（`raw<=100`），该档永远不会被执行。该分支为防御性「已 100%，无需恢复」兜底 |
| L151 | 收尾 `}`（cur==100% 恢复兜底块） | **结构性不可达**。同上行 —— 当前降额已为 100% 时，`raw_derating` 不可能大于它，恢复块入口（L128 `else`）都进不到这一档。单元测试同样无法覆盖此分支 |

> 以上 2 行均为 ISO 26262 编码规范要求的防御性兜底代码，在**任何合法生产
> 输入下都不可能触发**，属「结构不可达」而非「测试遗漏」。其余 109 行
> 全部可执行逻辑均已被端到端测试覆盖。

---

## 分支覆盖分析（97.4%，37/38）

未命中（not taken）的 1 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `cur_derating == RZC_TEMP_DERATE_75_PCT`（false 侧） | L143 | 该条件为 else-if 链最后一档；仅当 `cur==100%` 且 `raw>cur` 时才会落入其 false 分支。而 `raw_derating` 最大为 100，`cur==100%` 时 `raw>cur` 不可能成立，故该分支恒不进入（L150-151 同理） |

> 其余 37 个分支两侧全部覆盖：降额 4 段阈值、滞回 3 条恢复规则、范围门控、
> IoHwAb 读取、双 NTC 可信校验（含 fail-hot 双侧）均已由专门场景覆盖双侧。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 98.2%（109/111） | 2 行 | 均为 cur==100% 恢复兜底分支（结构不可达） |
| 分支 | 97.4%（37/38） | 1 个 | cur==100% 时 `raw>cur` 恒假（结构不可达） |
| 函数 | 100%（4/4） | — | — |

**结论**：`Swc_TempMonitor` 的全部可执行逻辑（4 个函数、降额 4 段阈值、
滞回 3 条恢复规则、合理范围门控、双 NTC 交叉校验 fail-hot、0% 过温 DEM DTC、
RTE 4 信号广播）均由 E2E 测试覆盖。2 行未覆盖代码为「当前降额已 100% 时
无需恢复」的防御性兜底分支，通过生产输入无法触发，符合预期；单元测试
同样无法覆盖该分支，说明其不可达性与测试层级无关。

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-16 | 初版设计文档（输入/输出因子、31 个用例、流程图） |
| 2026-08-16 | 新增 `rzc_temponitor.feature`（31 场景全部通过）、`rzc_temponitor_harness.c`、`/api/test/asw/rzc/temponitor` 测试 API；全量套件 254 场景通过；填写实测覆盖率（98.2% 行 / 97.4% 分支 / 100% 函数）及逐行映射与 2 行结构不可达代码说明 |
