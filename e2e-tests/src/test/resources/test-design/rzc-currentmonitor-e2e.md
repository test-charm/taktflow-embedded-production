# RZC 电流监控 (Swc_CurrentMonitor) E2E 测试设计

## 被测功能

**RZC ASW 电流监控 SWC — 64 样本零点校准（2048±200mA）、4 样本移动平均、
>25A 连续 10 周期过流去抖、过流后关闭电机并上报 DEM、过流持续期重复上报、
以及 500ms 低于阈值恢复与“平均值已回落但原始采样重新尖峰”时的恢复计数清零。**

覆盖链路：

```text
IoHwAb_ReadMotorCurrent（测试 API 注入原始 mA）
  → Swc_CurrentMonitor_Init
    → 状态清零
    → 64 次零点校准采样求均值
    → 2048±200 窗口校验
    → ZERO_CAL 失败时 Dem_ReportErrorStatus(RZC_DTC_ZERO_CAL, FAILED)
  → Swc_CurrentMonitor_MainFunction（1ms 周期）
    → 未初始化守卫
    → 原始电流采样
    → 4 样本滑动窗口 + 均值计算
    → avg>25000mA ? 去抖/确认为过流/重复上报 : 正常复位去抖 or 过流恢复计数
    → 过流确认时 Dio_WriteChannel(R_EN/L_EN, LOW)
    → Rte_Write(RZC_SIG_CURRENT_MA, avg)
    → Rte_Write(RZC_SIG_OVERCURRENT, flag)
```

与既有 `rzc_motor.feature` / `rzc_temponitor.feature` 一致，本测试通过
`/api/test/asw/rzc/currentmonitor` 调用原生 harness，执行真实
`Swc_CurrentMonitor.c` 生产代码。零点校准输入、运行期电流注入、RTE 当前值、
过流标志、DEM 最近状态/计数，以及 DIO 写次数全部由测试专用 API 观测。

## 被测代码流程图

```text
               ┌────────────────────────────┐
               │ Swc_CurrentMonitor_Init     │
               └─────────────┬──────────────┘
                             │
  Step1: 状态/缓冲区清零 ────┤
  Step2: 64 次读取 raw_mA ───┤
  Step3: avg = sum / 64 ─────┤
  Step4: avg 在 2048±200 内? ─┬─ Y → ZeroOffset=avg, ZeroCalDone=TRUE
                              └─ N → DEM ZERO_CAL FAILED, ZeroCalDone=FALSE
                             │
                      Initialized = TRUE
                             │
               ┌─────────────▼──────────────┐
               │ MainFunction (1ms)          │
               └─────────────┬──────────────┘
                             │
    Initialized!=TRUE? ──────┬─ Y → return
                             └─ N
                             │
    读取 raw_mA → 写入 4 样本缓冲 → 计算 avg_mA
                             │
    avg_mA > 25000 ? ────────┬─ Y
    │                        │
    │   OvercurrentActive? ──┬─ N → debounce++ → >=10 ? 
    │                        │          ├─ Y → Active=TRUE, Recovery=0,
    │                        │          │        DisableMotor, DEM FAILED
    │                        │          └─ N → 等待更多样本
    │                        └─ Y → Recovery=0, DEM FAILED
    │
    └─ N
         OvercurrentActive? ─┬─ N → debounce=0
                             └─ Y
                                  raw_mA > 25000 ?
                                    ├─ Y → Recovery=0
                                    └─ N → Recovery++ → >=500 ?
                                              ├─ Y → Active=FALSE, debounce=0, Recovery=0
                                              └─ N → 保持过流
                             │
    Rte_Write(current_mA=avg_mA, overcurrent=flag)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_CurrentMonitor_Init()` | `false`、`true` | When — 执行控制 |
| `cycles` | 当前 phase 的 MainFunction 调用次数 | `0`（仅作为零点校准输入）、`1`、`4`、`9`、`10`、`11`、`20`、`250`、`499`、`500` | When — 执行控制 |
| `currentMa` | 原始电流注入值（mA） | `1000`、`1800`、`1848`、`2048`、`2248`、`2249`、`24999`、`25000`、`25001`、`26000`、`65535` | When — 数据注入 |

说明：

1. **首个 phase 的 `currentMa` 会先驱动 Init 的 64 次零点校准采样**，随后该 phase
   的 `cycles` 次数再驱动 MainFunction。
2. 为了得到“健康校准 + 指定运行电流”的组合，用例可先放一个
   `{ currentMa: 2048, cycles: 0 }` 的零时长 phase，仅作为校准前置。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `currentMa` | RTE `RZC_SIG_CURRENT_MA`（平均电流） | `0..65535` |
| `overcurrent` | RTE `RZC_SIG_OVERCURRENT` | `0`/`1` |
| `dioCh5`/`dioCh6` | R_EN / L_EN 当前电平 | `0` |
| `dioWrites` | `Dio_WriteChannel` 总调用次数 | `0` 或 `2` |
| `demOvercurrent` | `RZC_DTC_OVERCURRENT` 最近状态 | `1`=FAILED、`-1`=未报告 |
| `demOvercurrentCount` | `RZC_DTC_OVERCURRENT` 报告次数 | `0..n` |
| `demZeroCal` | `RZC_DTC_ZERO_CAL` 最近状态 | `1`=FAILED、`-1`=未报告 |
| `demZeroCalCount` | `RZC_DTC_ZERO_CAL` 报告次数 | `0`/`1` |

## 测试用例

### 规则: 初始化守卫与零点校准

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `uninitialized_guard` | `[skipInit=true,current=5000,cycles=1]` | `currentMa=0`, `overcurrent=0`, `dem*= -1` |
| `zerocal_nominal_2048` | `[2048×1]` | `currentMa=2048`, `demZeroCal=-1` |
| `zerocal_low_boundary_1848` | `[1848×1]` | `currentMa=1848`, `demZeroCal=-1`, `demZeroCalCount=0` |
| `zerocal_high_boundary_2248` | `[2248×1]` | `currentMa=2248`, `demZeroCal=-1`, `demZeroCalCount=0` |
| `zerocal_high_out_of_range_2249` | `[2249×1]` | `currentMa=2249`, `demZeroCal=1`, `demZeroCalCount=1` |
| `zerocal_out_of_range_1800` | `[1800×1]` | `currentMa=1800`, `demZeroCal=1`, `demZeroCalCount=1` |

### 规则: 4 样本移动平均

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `steady_5000_after_4` | `[2048×0] → [5000×4]` | `currentMa=5000`, `overcurrent=0` |
| `avg_1000_2000_3000_4000` | `[2048×0] → [1000] → [2000] → [3000] → [4000]` | `currentMa=2500` |
| `window_wraps_on_fifth_sample` | `[2048×0] → [1000] → [2000] → [3000] → [4000] → [5000]` | `currentMa=3500` |

### 规则: 过流检测与去抖

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `below_threshold_24999` | `[2048×0] → [24999×20]` | `overcurrent=0`, `demOvercurrentCount=0` |
| `exact_threshold_25000` | `[2048×0] → [25000×20]` | `overcurrent=0`, `demOvercurrent=-1` |
| `trip_at_25001_after_10` | `[2048×0] → [25001×10]` | `overcurrent=1`, `dioWrites=2`, `demOvercurrentCount=1` |
| `debounce_resets_on_normal_sample` | `[2048×0] → [26000×9] → [1000×1] → [26000×9]` | `overcurrent=0`, `demOvercurrentCount=0` |
| `active_overcurrent_reports_each_cycle` | `[2048×0] → [26000×11]` | `overcurrent=1`, `demOvercurrentCount=2` |

### 规则: 500ms 恢复窗口

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `recovery_after_500_cycles` | `[2048×0] → [26000×10] → [1000×500]` | `overcurrent=0` |
| `recovery_not_before_499` | `[2048×0] → [26000×10] → [1000×499]` | `overcurrent=1` |
| `recovery_spike_resets_counter` | `[2048×0] → [26000×10] → [1000×250] → [26000×1] → [1000×499]` | `overcurrent=1` |
| `max_uint16_does_not_overflow_average` | `[2048×0] → [65535×4]` | `currentMa=65535` |

## 覆盖目标与充分性判断

1. **所有输入取值均至少出现一次**：零点校准窗口边界、运行阈值边界、
   恢复时间边界、滑动窗口环绕都已覆盖。
2. **所有条件分支的判断点均有双侧用例**：
   - `CM_Initialized != TRUE`
   - `avg` 是否落在 2048±200
   - `avg_mA > 25000`
   - `CM_OvercurrentActive == FALSE`
   - `CM_OcDebounceCount >= 10`
   - `raw_mA > 25000`（恢复期间尖峰）
   - `CM_RecoveryCycles >= 500`
3. **流程图中所有公开 API 路径均被至少一个用例命中**。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_CurrentMonitor.c.gcov.html` 实测（2026-08-17，全量套件 **482 场景 / 2917 步**
全部通过）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **96.2%**（102 / 106 行） |
| **分支覆盖** | **94.1%**（32 / 34 分支） |
| **函数覆盖** | **100.0%**（4 / 4 函数） |

覆盖到的函数（实测命中次数）：
`CM_ComputeAverage`（7522）、`CM_DisableMotor`（20）、
`Swc_CurrentMonitor_Init`（66）、`Swc_CurrentMonitor_MainFunction`（7527）。

> 命中次数来自整套 `./gradlew cucumber` 执行后的覆盖 HTML；数值会随套件规模变化，
> 但“哪些行由哪些场景覆盖”这一映射关系保持不变。

### 行覆盖分析（96.2%，102/106）

4 行未命中代码均位于 `CM_ComputeAverage` 的防御性守卫：`count==0` 与
`count>RZC_CURRENT_AVG_WINDOW`。其余 102 行全部由端到端场景覆盖。

#### CM_ComputeAverage（L67-88）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L68-73 | 局部变量、`sum=0`、`count=CM_AvgCount` | 全部已初始化运行场景（每次 MainFunction 周期） | 7522 |
| L75-77 | `count == 0u` → `return 0u` | **不可覆盖**。`Swc_CurrentMonitor_MainFunction` 先写入一个样本并在 L184-185 将 `CM_AvgCount` 至少增至 1，随后才调用 `CM_ComputeAverage`；公开 API 下不存在 `count==0` 调用路径 | 0 |
| L79-81 | `count > RZC_CURRENT_AVG_WINDOW` → 钳位 | **不可覆盖**。`CM_AvgCount` 仅在 L184-185 的 `< window` 分支中递增，达到 4 后停止，永不大于窗口大小 | 0 |
| L83-85 | `for (i < count)` 求和循环 | `steady_5000_after_4`、`avg_1000_2000_3000_4000`、`window_wraps_on_fifth_sample` 及全部过流/恢复场景 | 7522 / 29746 |
| L87 | `sum / count` 返回平均值 | 全部已初始化运行场景 | 7522 |

#### CM_DisableMotor（L95-98）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L96-97 | `Dio_WriteChannel(R_EN/L_EN, 0u)` | `trip_at_25001_after_10`、`active_overcurrent_reports_each_cycle`、`recovery_after_500_cycles`、`recovery_not_before_499`、`recovery_spike_resets_counter` | 20 |

#### Swc_CurrentMonitor_Init（L105-149）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L106-119 | Init 局部变量与全部状态清零 | 除 `uninitialized_guard` 外的全部场景 | 66 |
| L121-123 | 4 项平均缓冲区清零循环 | 同上（264=66×4） | 66 / 264 |
| L128-135 | `sum=0`、64 次零点采样、`avg=sum/64` | 全部已初始化场景；低/高边界与双侧越界场景共同驱动 | 66 / 4224 |
| L138-141 | 零点校准窗口通过（下界、上界与健康值） | `zerocal_nominal_2048`、`zerocal_low_boundary_1848`、`zerocal_high_boundary_2248`，以及所有以前置 `2048×0` 做健康校准的运行场景 | 66 / 60 |
| L144-145 | ZERO_CAL 失败并报告 DEM | `zerocal_out_of_range_1800`、`zerocal_high_out_of_range_2249` | 6 |
| L148 | `CM_Initialized = TRUE` | 全部已初始化场景 | 66 |

> L138-L145 的双侧都被覆盖：低侧越界（1800）命中第一半条件 false， 高侧越界
> （2249）命中第二半条件 false，从而完整覆盖 `2048±200` 窗口判断。

#### Swc_CurrentMonitor_MainFunction（L156-257）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L163-165 | 未初始化守卫 → return | `uninitialized_guard` | 3 |
| L173-191 | 原始采样、环形缓冲写入、索引环绕、`CM_AvgCount` 递增/饱和、调用 `CM_ComputeAverage` | 全部已初始化运行场景；`window_wraps_on_fifth_sample` 覆盖索引环绕，`steady_5000_after_4` 覆盖 `CM_AvgCount` 从 1 增到 4，再由长运行场景覆盖饱和后的 false 侧 | 7522 / 1866 / 208 |
| L198-201 | `avg_mA > 25000` 且尚未激活过流时去抖 | `trip_at_25001_after_10`、`active_overcurrent_reports_each_cycle`、全部恢复场景 | 280 / 276 |
| L203-211 | 第 10 次高均值确认过流、关闭电机、DEM FAILED | `trip_at_25001_after_10`、`active_overcurrent_reports_each_cycle`、全部恢复场景 | 20 |
| L213-220 | 已处于过流时持续高均值 → 恢复计数清零 + 再次上报 DEM | `active_overcurrent_reports_each_cycle`（第 11 周期）、`recovery_spike_resets_counter`（恢复中单次高原始采样造成高均值前沿） | 4 |
| L224-226 | 正常路径下阈值以下 → 去抖计数清零 | `below_threshold_24999`、`exact_threshold_25000`、`debounce_resets_on_normal_sample` | 246 |
| L236-237 | 过流恢复期间原始采样再次尖峰 → 恢复计数清零 | `recovery_spike_resets_counter` | 4 |
| L239-244 | 过流恢复计数递增，满 500 周期清除过流 | `recovery_not_before_499`、`recovery_after_500_cycles` | 6992 / 4 |
| L252-254 | `Rte_Write(current_mA, overcurrent)` 与三元表达式双侧 | 全部已初始化运行场景；`overcurrent` true/false 两侧分别由 trip/recovery 与健康场景覆盖 | 7522 |

### 未覆盖行说明（4 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L76 | `return 0u;` | `CM_ComputeAverage` 只在 MainFunction 完成“写样本 + `CM_AvgCount++`”后调用；公开 API 下 `count` 不可能为 0 |
| L77 | `}`（`count==0` 分支收尾） | 同上 |
| L80 | `count = RZC_CURRENT_AVG_WINDOW;` | `CM_AvgCount` 通过 `< window` 守卫递增，最大只能等于 4，永不大于 4 |
| L81 | `}`（`count>window` 分支收尾） | 同上 |

### 分支覆盖分析（94.1%，32/34）

未命中的 2 个分支均为 `CM_ComputeAverage` 的防御性 true 侧：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `count == 0u` 的 true 侧 | L75 | MainFunction 调用顺序保证 `CM_AvgCount >= 1` 后才会进入 `CM_ComputeAverage` |
| `count > RZC_CURRENT_AVG_WINDOW` 的 true 侧 | L79 | `CM_AvgCount` 受 L184-185 的 `< window` 约束，只会增长到 4，不会超过 4 |

### 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 96.2%（102/106） | 4 行 | 均为 `CM_ComputeAverage` 的结构性防御守卫 |
| 分支 | 94.1%（32/34） | 2 个 | 同上：`count==0` / `count>window` 的 true 侧 |
| 函数 | 100.0%（4/4） | — | — |

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-17 | 初版设计文档（输入/输出因子、流程图、18 个 E2E 用例） |
| 2026-08-17 | 新增 `rzc_currentmonitor.feature`（18 场景全部通过）、`rzc_currentmonitor_harness.c`、`/api/test/asw/rzc/currentmonitor` 测试 API；全量 `./gradlew cucumber` 实测 **482 场景 / 2917 步全部通过**，并补充覆盖率（96.2% 行 / 94.1% 分支 / 100% 函数）与逐行映射 |
