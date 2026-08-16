# RZC 电池电压监控 (Swc_Battery) E2E 测试设计

## 被测功能

**RZC ASW 电池电压监控 SWC — 4 样本滑动平均、5 段阈值状态
（DISABLE_LOW / WARN_LOW / NORMAL / WARN_HIGH / DISABLE_HIGH）、
滞回恢复（+500mV / −500mV）、DISABLE 状态 DEM DTC 报告、SOC 单调递减守卫、
RTE 信号广播（平均电压 + 状态码，经 Swc_RzcCom 发送 CAN 0x303）**

覆盖链路：

```text
IoHwAb_ReadBatteryVoltage（电压分压 ADC 采样，E2E 中经测试 API 注入）
  → Swc_Battery_MainFunction（100ms 周期）
  → 未初始化守卫
  → 4 样本滑动平均（Batt_ComputeAverage）
  → Batt_DetermineStatus（5 段阈值 + 4 条滞回恢复规则）
  → DISABLE_LOW / DISABLE_HIGH → Dem_ReportErrorStatus(RZC_DTC_BATTERY, FAILED)
  → SOC 线性映射（12600mV=100%、10500mV=0%）+ 单调递减守卫
  → Rte_Write（RZC_SIG_BATTERY_MV=平均电压 / RZC_SIG_BATTERY_STATUS=状态码）
```

与既有 ASW E2E（RZC `Swc_Motor`、FZC `Swc_Steering`/`Swc_Brake`/`Swc_Lidar`）
一致，本测试通过测试专用 API 在原生测试框架内执行真实的 `Swc_Battery.c`
生产代码。原始采样电压经 IoHwAb mock 注入，输出经 RTE / DEM mock 观察。

## 被测代码流程图

```
                    ┌─────────────────────┐
                    │ Swc_Battery_Init     │
                    │ 电压=标称12600       │
                    │ 状态=NORMAL          │
                    │ 均值缓冲=12600×4     │
                    │ SOC=100             │
                    │ Initialized=TRUE     │
                    └─────────┬───────────┘
                              │
               ┌──────────────▼──────────────┐
               │ Swc_Battery_MainFunction()   │
               └──────────────┬──────────────┘
                              │
   Step1: Initialized!=TRUE? ──Y──→ return（未初始化空转）
                              │N
   Step2: IoHwAb_ReadBatteryVoltage(&raw)  →  写入均值缓冲 + 索引环绕
                              │
   Step3: avg = Batt_ComputeAverage()（4 样本求和/4）
                              │
   Step4: status = Batt_DetermineStatus(avg, prev_status)
          ├─ avg < 8000  ────────────→ DISABLE_LOW(0)
          ├─ avg < 10500 ────────────→ WARN_LOW(1)
          ├─ avg < 15000 ────────────→ NORMAL(2)
          ├─ avg < 17000 ────────────→ WARN_HIGH(3)
          └─ 否则       ────────────→ DISABLE_HIGH(4)
          ┌─ 滞回恢复（prev 决定）：
          ├─ prev=DISABLE_LOW & avg<8500   → 保持 DISABLE_LOW
          ├─ prev=WARN_LOW   & avg<11000   → 保持 WARN_LOW
          ├─ prev=DISABLE_HIGH & avg≥16500 → 保持 DISABLE_HIGH
          └─ prev=WARN_HIGH  & avg≥14500   → 保持 WARN_HIGH
                              │
   Step5: DISABLE_LOW 或 DISABLE_HIGH? ──Y──→ Dem_ReportErrorStatus(BATTERY, FAILED)
                              │
   Step6: SOC 计算（单调递减守卫，仅本地跟踪）
          ├─ avg≥12600 → target=100
          ├─ avg≤10500 → target=0
          └─ 否则      → target=(avg-10500)*100/2100
          └─ target < Batt_Soc → Batt_Soc=target
                              │
   Step7: Rte_Write(RZC_SIG_BATTERY_MV, avg)
          Rte_Write(RZC_SIG_BATTERY_STATUS, status)
          （CAN TX 由 Swc_RzcCom 读取 RTE 信号发出，不在本 SWC 内）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Battery_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`/`2`（均值未收敛）、`4`（均值完全收敛） | When — 执行控制 |
| `voltageMv` | 注入的原始电池电压（mV） | 见下表状态等价类与边界值 | When — 数据注入 |

`voltageMv` 状态等价类（原始电压 → 4 样本平均收敛后的目标状态）：

| 等价类 | 取值（mV） | 目标状态 |
|---|---|---|
| 禁用低压 | `7000` | DISABLE_LOW(0) |
| 警告低压 | `9000` | WARN_LOW(1) |
| 正常 | `12600` | NORMAL(2) |
| 警告高压 | `16000` | WARN_HIGH(3) |
| 禁用高压 | `17500` | DISABLE_HIGH(4) |

`voltageMv` 边界值（mV，`<=`/`<` 比较边界）：

| 边界 | 取值（mV） | 期望状态 | 说明 |
|---|---|---|---|
| DISABLE_LOW 阈值 | `7999` / `8000` | DISABLE_LOW(0) / WARN_LOW(1) | `avg<8000` |
| WARN_LOW 阈值 | `10499` / `10500` | WARN_LOW(1) / NORMAL(2) | `avg<10500` |
| WARN_HIGH 阈值 | `14999` / `15000` | NORMAL(2) / WARN_HIGH(3) | `avg<15000` |
| DISABLE_HIGH 阈值 | `16999` / `17000` | WARN_HIGH(3) / DISABLE_HIGH(4) | `avg<17000` |
| DISABLE_LOW 恢复 | `8200` / `9000` | DISABLE_LOW(0) / WARN_LOW(1) | `8000+500=8500` |
| WARN_LOW 恢复 | `10600` / `11500` | WARN_LOW(1) / NORMAL(2) | `10500+500=11000` |
| WARN_HIGH 恢复 | `14600` / `14000` | WARN_HIGH(3) / NORMAL(2) | `15000-500=14500` |
| DISABLE_HIGH 恢复 | `16800` / `16000` | DISABLE_HIGH(4) / WARN_HIGH(3) | `17000-500=16500` |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `voltageMv` | RTE `RZC_SIG_BATTERY_MV`（4 样本平均电压） | 期望平均电压 |
| `status` | RTE `RZC_SIG_BATTERY_STATUS`（状态码） | `0`=DISABLE_LOW、`1`=WARN_LOW、`2`=NORMAL、`3`=WARN_HIGH、`4`=DISABLE_HIGH |
| `demBattery` | `Dem_ReportErrorStatus` RZC_DTC_BATTERY 最近状态 | `1`=FAILED（到达过 DISABLE 状态）、`-1`=未报告 |

> 说明：生产代码仅在 DISABLE 状态报告 `FAILED`，从不报告 `PASSED`（DTC 需经
> UDS/复位清除），故 DEM mock 一旦记录 `FAILED` 后保持 `1`。`demBattery` 断言
> 只验证「是否到达过 DISABLE 状态并报告」。

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与滑动平均**：未初始化空转 / 标称初始化 / 4 周期
>   收敛 / 部分收敛（1、2 周期），共 5 场景。
> - **规则: 状态阈值映射**：5 个状态等价类 + 8 个边界值，共 13 场景。
> - **规则: 滞回恢复**：4 条滞回规则的「保持」与「恢复」，共 8 场景。
> - **规则: DTC 报告**：DISABLE 状态报告 FAILED / 非 DISABLE 不报告，共 2 场景。
>
> 每个用例经 `POST /api/test/asw/rzc/battery` 一次运行驱动真实
> `Swc_Battery.c`；多阶段脚本中阶段顺序执行、模块状态（均值缓冲/状态/DEM）
> 跨阶段保留（滞回测试依赖前序阶段建立 prev_status）。

### 规则: 初始化守卫与滑动平均

| 用例 | 阶段序列（voltageMv, cycles） | 期望 voltageMv | 期望 status | 期望 demBattery |
|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true, voltageMv=7000, cycles=1 | 0 | 0 | -1 |
| nominal_after_init | P0: voltageMv=12600, cycles=1 | 12600 | 2 | -1 |
| avg_converges_4_cycles | P0: voltageMv=7000, cycles=4 | 7000 | 0 | 1 |
| avg_partial_first_cycle | P0: voltageMv=7000, cycles=1 | 11200 | 2 | -1 |
| avg_partial_second_cycle | P0: voltageMv=7000, cycles=2 | 9800 | 1 | -1 |

### 规则: 状态阈值映射

| 用例 | 阶段序列（voltageMv, cycles） | 期望 voltageMv | 期望 status | 期望 demBattery |
|---|---|---|---|---|
| disable_low | P0: voltageMv=7000, cycles=4 | 7000 | 0 | 1 |
| warn_low | P0: voltageMv=9000, cycles=4 | 9000 | 1 | -1 |
| normal | P0: voltageMv=12600, cycles=4 | 12600 | 2 | -1 |
| warn_high | P0: voltageMv=16000, cycles=4 | 16000 | 3 | -1 |
| disable_high | P0: voltageMv=17500, cycles=4 | 17500 | 4 | 1 |
| boundary_7999 | P0: voltageMv=7999, cycles=4 | 7999 | 0 | 1 |
| boundary_8000 | P0: voltageMv=8000, cycles=4 | 8000 | 1 | -1 |
| boundary_10499 | P0: voltageMv=10499, cycles=4 | 10499 | 1 | -1 |
| boundary_10500 | P0: voltageMv=10500, cycles=4 | 10500 | 2 | -1 |
| boundary_14999 | P0: voltageMv=14999, cycles=4 | 14999 | 2 | -1 |
| boundary_15000 | P0: voltageMv=15000, cycles=4 | 15000 | 3 | -1 |
| boundary_16999 | P0: voltageMv=16999, cycles=4 | 16999 | 3 | -1 |
| boundary_17000 | P0: voltageMv=17000, cycles=4 | 17000 | 4 | 1 |

### 规则: 滞回恢复

| 用例 | 阶段序列（voltageMv, cycles） | 期望 voltageMv | 期望 status | 期望 demBattery |
|---|---|---|---|---|
| hysteresis_disable_low_stays | P0: 7000×4, P1: 8200×4 | 8200 | 0 | 1 |
| hysteresis_disable_low_recovers | P0: 7000×4, P1: 9000×4 | 9000 | 1 | 1 |
| hysteresis_warn_low_stays | P0: 9000×4, P1: 10600×4 | 10600 | 1 | -1 |
| hysteresis_warn_low_recovers | P0: 9000×4, P1: 11500×4 | 11500 | 2 | -1 |
| hysteresis_disable_high_stays | P0: 17500×4, P1: 16800×4 | 16800 | 4 | 1 |
| hysteresis_disable_high_recovers | P0: 17500×4, P1: 16000×4 | 16000 | 3 | 1 |
| hysteresis_warn_high_stays | P0: 16000×4, P1: 14600×4 | 14600 | 3 | -1 |
| hysteresis_warn_high_recovers | P0: 16000×4, P1: 14000×4 | 14000 | 2 | -1 |

### 规则: DTC 报告

| 用例 | 阶段序列（voltageMv, cycles） | 期望 voltageMv | 期望 status | 期望 demBattery |
|---|---|---|---|---|
| dtc_reported_on_disable_low | P0: 7000×4 | 7000 | 0 | 1 |
| dtc_reported_on_disable_high | P0: 17500×4 | 17500 | 4 | 1 |

> `demBattery=-1`（未报告）断言已蕴含在 warn_low / normal / warn_high /
> 滞回 warn_* 用例中：非 DISABLE 状态不触发 DEM 报告。

## 代码路径覆盖

- `Swc_Battery_Init` 全部可执行行 ✅
  - 电压=标称 / 状态=NORMAL / 缓冲索引清零 ✅
  - 均值缓冲 4 项填充标称 12600 ✅
  - SOC=100 / `Initialized=TRUE` ✅
- `Batt_ComputeAverage` 全部可执行行 ✅
  - 4 样本求和循环 ✅
  - `sum / RZC_BATT_AVG_WINDOW` 返回 ✅
- `Batt_DetermineStatus` 全部可执行行 ✅
  - 5 段阈值链（`avg<8000 / <10500 / <15000 / <17000 / else`）✅
  - DISABLE_LOW 滞回（`avg<8500` 保持）✅
  - WARN_LOW 滞回（`new_status==NORMAL && avg<11000` 保持）✅
  - DISABLE_HIGH 滞回（`avg>=16500` 保持）✅
  - WARN_HIGH 滞回（`new_status==NORMAL && avg>=14500` 保持）✅
- `Swc_Battery_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅ `uninitialized_guard`
  - IoHwAb 读取 / 缓冲写入 / 索引环绕 ✅
  - 均值计算与电压更新 ✅
  - 状态判定（含滞回）✅
  - DISABLE 状态 → DEM FAILED ✅ `disable_low` / `disable_high`
  - SOC 线性映射三分支（≥12600 / ≤10500 / 中间）+ 单调守卫双侧 ✅
  - Rte_Write 2 信号（MV / STATUS）✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Battery.c.gcov.html` 实测（2026-08-16 全量套件 223 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100.0%**（94 / 94 行） |
| **分支覆盖** | **100.0%**（46 / 46 分支） |
| **函数覆盖** | **100.0%**（4 / 4） |

覆盖到的函数（实测命中次数）：
`Swc_Battery_Init`（55）、`Batt_ComputeAverage`（284）、
`Batt_DetermineStatus`（284）、`Swc_Battery_MainFunction`（286）。

> 下表「实测命中」为容器生命周期内累积值（含 2 次套件运行 + 1 次手工探测）；
> 每次容器重建后会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（100.0%，94/94）

`Swc_Battery.c` 全部 94 行可执行代码均被端到端测试覆盖，**没有任何未覆盖行**。
与 `Swc_Motor.c`（15 行防御性锁存/上限分支不可达）不同，本 SWC 无结构不可达
分支 —— 5 段阈值链、4 条滞回规则、DISABLE 状态 DTC、SOC 单调守卫的全部
条件分支两侧均已触发。逐行映射如下。

### 逐函数代码行覆盖映射

#### Batt_ComputeAverage（L51-62）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L52-56 | 局部变量与 `sum=0` | 全部已初始化场景（每次 MainFunction 周期） | 284 |
| L57-59 | 4 样本求和循环（含循环分支双侧） | 全部已初始化场景（4 次迭代全覆盖，命中 1136=284×4） | 284/1136 |
| L61 | `sum / RZC_BATT_AVG_WINDOW` 返回 | 全部已初始化场景 | 284 |

#### Batt_DetermineStatus（L68-124）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L74-76 | `avg<8000` → DISABLE_LOW | `disable_low`（7000）、`boundary_7999`、滞回阶段 7000 | 23 |
| L77-79 | `avg<10500` → WARN_LOW | `warn_low`（9000）、`boundary_8000`/`boundary_10499`、滞回 9000/10600/8200 | 72 |
| L80-82 | `avg<15000` → NORMAL | `normal`（12600）、`boundary_10500`/`boundary_14999`、部分收敛 11200、滞回恢复 11500/14000 | 119 |
| L83-85 | `avg<17000` → WARN_HIGH | `warn_high`（16000）、`boundary_15000`/`boundary_16999`、滞回 16000/16800/14600 | 56 |
| L86-88 | 否则 → DISABLE_HIGH | `disable_high`（17500）、`boundary_17000`、滞回阶段 17500/16800 | 14 |
| L94-97 | `prev==DISABLE_LOW && avg<8500` 保持 DISABLE_LOW | `hysteresis_disable_low_stays`（7000→8200 第二阶段 4 周期） | 16 |
| L101-105 | `prev==WARN_LOW && NORMAL && avg<11000` 保持 WARN_LOW | `hysteresis_warn_low_stays`（9000→10600 第二阶段） | 4 |
| L109-112 | `prev==DISABLE_HIGH && avg>=16500` 保持 DISABLE_HIGH | `hysteresis_disable_high_stays`（17500→16800 第二阶段） | 12 |
| L116-120 | `prev==WARN_HIGH && NORMAL && avg>=14500` 保持 WARN_HIGH | `hysteresis_warn_high_stays`（16000→14600 第二阶段） | 6 |
| L123-124 | `return new_status` | 全部已初始化场景 | 284 |

> 阈值链 5 个比较（L74/77/80/83）的 `<` 两侧、滞回 4 条规则的内外两侧共
> 21 个条件分支全部覆盖（见下方分支覆盖分析）。

#### Swc_Battery_Init（L130-143）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L134 | 电压=标称 12600 | 全部已初始化场景（每 POST 一次 Init） | 55 |
| L135 | 状态=NORMAL | 同上 | 55 |
| L136 | 缓冲索引=0 | 同上 | 55 |
| L137-139 | 均值缓冲 4 项填充 12600（循环双侧） | 同上（220=55×4） | 55/220 |
| L141-142 | SOC=100、`Initialized=TRUE` | 同上 | 55 |

#### Swc_Battery_MainFunction（L149-220）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L150-156 | 未初始化守卫 → return | `uninitialized_guard`（skipInit=true） | 2 |
| L161-162 | IoHwAb 读取原始电压 | 全部已初始化场景 | 284 |
| L164-168 | 缓冲写入 + 索引递增与环绕（L166 环绕分支双侧） | 全部已初始化场景（L167 环绕 69 次 ≈ 284/4） | 284/69 |
| L170-171 | 均值计算 + `Batt_Voltage_mV` 更新 | 全部已初始化场景 | 284 |
| L176 | `Batt_DetermineStatus` 状态更新 | 全部已初始化场景 | 284 |
| L181-184 | DISABLE_LOW/DISABLE_HIGH → DEM FAILED | `disable_low`/`disable_high`/`boundary_7999`/`boundary_17000`/滞回 DISABLE 阶段（L183 报告 49 次） | 49 |
| L190-198 | SOC 线性映射三分支：`avg>=12600`→100（L193 138 次）、`avg<=10500`→0（L195 97 次）、中间线性（L197 49 次） | 高压/低压/中间电压用例全覆盖 | 284 |
| L202-204 | 单调递减守卫 `target<Batt_Soc` 更新（双侧） | 低压场景 SOC 下降（70 次 true）；标称/高压场景 false | 284 |
| L210 | `Rte_Write(RZC_SIG_BATTERY_MV, avg)` | 全部已初始化场景（断言 `voltageMv`） | 284 |
| L216 | `Rte_Write(RZC_SIG_BATTERY_STATUS, status)` | 全部已初始化场景（断言 `status`） | 284 |
| L217 | `(void)Batt_Soc` | 全部已初始化场景 | 284 |
| L220 | 函数结束 | 全部已初始化场景 | 284 |

> 未列出的行号为注释、空行、声明或大括号占位（llvm-cov/lcov 不计入可执行行）。

### SOC 单调守卫的可观测性说明

`Batt_Soc` 为 file-scope `static` 变量，仅用于单调递减守卫，**从不写入任何
RTE 信号**（代码注释说明 `RZC_SIG_BATTERY_SOC` 与 `RZC_SIG_BATTERY_STATUS`
在 `Rzc_Cfg.h` 中别名到同一信号槽 25 —— 已知 codegen 缺陷，状态码最后写入
覆盖 SOC 值，SOC 不会被 CAN 0x303 广播）。因此 SOC 的数值变化**无法从 SWC
外部观测**，E2E 测试只能通过覆盖率确认 L190-205 的映射与守卫分支已执行，
无法对其行为做断言 —— 这属于被测系统的可观测性限制，而非测试遗漏。若未来
SOC 需要参与 CAN 广播（如重新映射到独立信号槽），应补充对应断言。

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 100.0%（94/94） | 0 行 | — |
| 分支 | 100.0%（46/46） | 0 个 | — |
| 函数 | 100.0%（4/4） | — | — |

**结论**：`Swc_Battery` 的全部可执行逻辑（4 个函数、5 段阈值链、4 条滞回
恢复规则、DISABLE 状态 DTC 报告、SOC 单调守卫、RTE 双信号广播）均由 E2E
测试 100% 覆盖。与 `Swc_Motor` 不同，本 SWC 不存在结构不可达的防御性分支
—— 所有条件分支两侧均已通过合法电压注入触发，达到行/分支/函数三重 100%。

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-16 | 初版设计文档（输入/输出因子、28 个用例、流程图） |
| 2026-08-16 | 新增 `rzc_battery.feature`（28 场景全部通过）、`rzc_battery_harness.c`、`/api/test/asw/rzc/battery` 测试 API；全量套件 223 场景通过；填写实测覆盖率（100% 行 / 100% 分支 / 100% 函数） |
