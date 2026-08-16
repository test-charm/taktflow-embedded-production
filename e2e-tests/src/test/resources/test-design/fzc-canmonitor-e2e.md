# FZC CAN 总线监控 (Swc_FzcCanMonitor) E2E 测试设计

## 被测功能

**FZC ASW CAN 总线健康监控 SWC — 总线丢失检测（bus-off 立即触发、200ms 静默、
错误警告持续 500ms 三种故障路径 → 安全状态锁存）+ 启动宽限期（500 周期抑制
监控）+ NotifyRx 静默复位（NO recovery — 断电前保持安全状态）**

覆盖链路：

```text
harness 注入（canMode / tec / rec / notifyRx）
  → Swc_FzcCanMonitor_Init()（状态复位：OK + 静默/错误警告/宽限计数清零）
  → Swc_FzcCanMonitor_Check()（10ms 周期，SWR-FZC-024）：
       · 未初始化守卫 → 直接 return
       · 安全状态锁存（生产路径）→ 重复应用安全状态 + return
       · 启动宽限期：GraceCycles < 500 → 递增 + 复位静默 + return
       · Check 1: Can_GetControllerMode(0) == CAN_CS_STOPPED
                    → Status=BUS_OFF, SafeLatched=TRUE, 应用安全状态, return
       · Check 2: SilenceCount >= 20 周期（200ms）
                    → Status=SILENCE, SafeLatched=TRUE, 应用安全状态, return
       · Check 3: TEC/REC >= 96 且持续 >= 50 周期（500ms）
                    → Status=ERROR_WARNING, SafeLatched=TRUE, 应用安全状态, return
                 两计数器均 < 96 → ErrWarnCount=0（复位重新计时）
       · 健康 → Status=OK
  → Swc_FzcCanMonitor_GetStatus()（状态观测）
  → Swc_FzcCanMonitor_NotifyRx()（RX 成功时复位静默计数器）
```

安全状态（`CanMon_ApplySafeState`）：
- `Rte_Write(FZC_SIG_BRAKE_CMD, 100)` — 制动 100%
- `Rte_Write(FZC_SIG_STEER_CMD, 0)` — 转向居中
- `Rte_Write(FZC_SIG_BUZZER_PATTERN, FZC_BUZZER_CONTINUOUS)` — 连续蜂鸣
- `Dem_ReportErrorStatus(FZC_DTC_CAN_BUS_OFF, DEM_EVENT_STATUS_FAILED)` — 上报 DTC

与既有 ASW E2E（CVC `Swc_CanMonitor`、FZC `Swc_Heartbeat` 等）一致，通过测试
专用 API 在原生测试框架内执行真实的 `Swc_FzcCanMonitor.c` 生产代码。CAN
控制器模式与错误计数器由 harness mock 注入，内部状态（静默计数器、宽限计数、
错误警告计数、安全锁存标志）经 `#ifdef UNIT_TEST` 观测 getter 断言。

> **被测代码观测**：`CanMon_Initialized`、`CanMon_SilenceCount`、
> `CanMon_GraceCycles`、`CanMon_ErrWarnCount`、`CanMon_SafeLatched` 均为模块
> 静态状态，无法从外部直接读取。为支持 E2E 断言，在
> `Swc_FzcCanMonitor.c/.h` 增加了 **`#ifdef UNIT_TEST` 保护**的观测 getter
> （`GetInitialized` / `GetSilenceCount` / `GetGraceCycles` /
> `GetErrWarnCount` / `GetSafeLatched`）。生产固件构建不定义 `UNIT_TEST`，
> 这些访问器不进入交付固件；仅测试 harness 编译时生效。`CanMon_Status` 已有
> 生产 API `Swc_FzcCanMonitor_GetStatus()` 可观测。安全状态输出（brakeCmd /
> steerCmd / buzzerPattern）经 harness 的 mock RTE 信号表直接观测，DTC 上报
> 经 `Dem_ReportErrorStatus` mock 计数观测。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_FzcCanMonitor_Init       │
│ Status=OK, SilenceCount=0    │
│ ErrWarnCount=0               │
│ SafeLatched=FALSE            │
│ GraceCycles=0                │
│ Initialized=TRUE             │
└─────────────┬────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Swc_FzcCanMonitor_Check()    │
└─────────────┬────────────────┘
              │
 Initialized != TRUE? ──Y──→ return（未初始化空转）
              │N
 SafeLatched == TRUE? ──Y──→ ApplySafeState, return（生产：锁存至断电）
              │N
 GraceCycles < 500? ──────Y──→ GraceCycles++, SilenceCount=0, return
              │N
 canMode = Can_GetControllerMode(0)
 canMode == CAN_CS_STOPPED? ─Y──→ Status=BUS_OFF, SafeLatched=TRUE,
              │N              ApplySafeState, return
 SilenceCount++
 SilenceCount >= 20? ──────Y──→ Status=SILENCE, SafeLatched=TRUE,
              │N              ApplySafeState, return
 tec=0, rec=0
 Can_GetErrorCounters(0,&tec,&rec)
 (tec>=96) || (rec>=96)? ──N──→ ErrWarnCount = 0（复位）
              │Y
 ErrWarnCount++
 ErrWarnCount >= 50? ──────Y──→ Status=ERROR_WARNING, SafeLatched=TRUE,
              │N              ApplySafeState, return
              ▼
 Status = OK（健康）
```

> `#ifdef PLATFORM_HIL` 分支（HIL 解锁恢复路径）不在原生 harness 编译范围内：
> 原生 E2E 以**生产固件配置**（fail-closed，锁存至断电）编译，HIL 解锁路径
> 由 HIL 测试（`sil_004_can_busoff_fzc.yaml`）覆盖，见「无法覆盖的代码说明」。

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_FzcCanMonitor_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | Check 调用次数 | `1`（单次/宽限起点）、`5`（静默基线）、`19`/`20`（静默边界）、`49`/`50`（错误警告边界）、`500`（宽限边界） | When — 执行控制 |
| `canMode` | `Can_GetControllerMode(0)` 返回值 | `CAN_CS_STARTED=2`（正常）、`CAN_CS_STOPPED=1`（总线关闭） | When — 故障注入 |
| `tec` | TEC 发送错误计数 | `0`（正常）、`95`（<96 边界）、`96`（≥96 阈值） | When — 故障注入 |
| `rec` | REC 接收错误计数 | `0`（正常）、`95`（<96 边界）、`96`（≥96 阈值，单独触发） | When — 故障注入 |
| `notifyRx` | 每 Check 前调用 `Swc_FzcCanMonitor_NotifyRx` | `false`（静默：计数器累积）、`true`（消息到达：复位静默） | When — 状态注入 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `status` | `Swc_FzcCanMonitor_GetStatus()` | OK(0)/BUS_OFF(1)/SILENCE(2)/ERROR_WARNING(3) |
| `initialized` | `CanMon_Initialized`（getter） | Init 后 1；skipInit 后 0 |
| `silenceCount` | `CanMon_SilenceCount`（getter） | 静默累积周期数；NotifyRx 后复位 |
| `graceCycles` | `CanMon_GraceCycles`（getter） | 宽限计数（0..500） |
| `errWarnCount` | `CanMon_ErrWarnCount`（getter） | 错误警告持续周期数 |
| `safeLatched` | `CanMon_SafeLatched`（getter） | 0/1（安全状态锁存） |
| `brakeCmd` | RTE `FZC_SIG_BRAKE_CMD`（=31） | 安全状态：100；正常：0 |
| `steerCmd` | RTE `FZC_SIG_STEER_CMD`（=150） | 安全状态：0（居中） |
| `buzzerPattern` | RTE `FZC_SIG_BUZZER_PATTERN`（=198） | 安全状态：4（连续蜂鸣）；正常：0 |
| `dtcReported` | `Dem_ReportErrorStatus(FZC_DTC_CAN_BUS_OFF, FAILED)` 调用次数 | 每次应用安全状态 +1 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: 初始化与未初始化守卫**：Init 默认值 / 未初始化 Check 空转，共 2 场景。
> - **规则: 启动宽限期**：宽限期内抑制 bus-off / 宽限期后监控生效，共 2 场景。
> - **规则: 总线关闭检测**：bus-off 立即安全状态 / 锁存后总线恢复不解除，共 2 场景。
> - **规则: 静默检测**：不足 20 周期 / 恰达 20 周期 / NotifyRx 复位，共 3 场景。
> - **规则: 错误警告检测**：不足 50 周期 / 恰达 50 周期 / REC 单独触发 /
>   低于阈值复位 / 清零后重新计时，共 5 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/canmonitor/setup` 存储）：设置前置总线
>   监控状态（如通过 500 周期宽限期、bus-off 锁存基线）。无前置状态时存空
>   `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/fzc/canmonitor` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`cycles=1`、
> `canMode=STARTED(2)`、`tec=0`、`rec=0`、`notifyRx=false`、`skipInit=false`）。

### 规则: 初始化与未初始化守卫

| 用例 | 阶段序列 | 期望 status | 期望 initialized | 期望 graceCycles |
|---|---|---|---|---|
| init_defaults_ok | P0: cycles=1 | OK(0) | 1 | 1（宽限第 1 周期） |
| uninitialized_check_noop | P0: cycles=1, skipInit=true | OK(0)（守卫返回） | 0 | 0 |

### 规则: 启动宽限期

| 用例 | 阶段序列 | 期望 status | 期望 safeLatched | 期望 graceCycles |
|---|---|---|---|---|
| grace_suppresses_busoff | P0: cycles=1, canMode=1（宽限内 STOPPED） | OK(0)（宽限抑制） | 0 | 1 |
| grace_ends_monitoring_busoff | P0: cycles=500（宽限通过）; P1: cycles=1, canMode=1 | BUS_OFF(1) | 1 | 500 |

### 规则: 总线关闭检测

| 用例 | 阶段序列 | 期望 status | 期望 brakeCmd | 期望 dtcReported |
|---|---|---|---|---|
| busoff_immediate_safe_state | 前置: cycles=500; P0: cycles=1, canMode=1 | BUS_OFF(1) | 100 | 1 |
| busoff_latched_no_recovery | 前置: cycles=500, canMode=1（bus-off 锁存）; P0: cycles=1, canMode=2（总线恢复） | BUS_OFF(1)（不解除） | 100 | 2（再次应用安全状态） |

### 规则: 静默检测

| 用例 | 阶段序列 | 期望 status | 期望 silenceCount | 期望 safeLatched |
|---|---|---|---|---|
| silence_under_threshold | 前置: cycles=500; P0: cycles=19 | OK(0) | 19 | 0 |
| silence_at_threshold | 前置: cycles=500; P0: cycles=20 | SILENCE(2) | 20 | 1 |
| notify_rx_resets_silence | 前置: cycles=500; P0: cycles=5（静默 5）; P1: cycles=5, notifyRx=true（复位） | OK(0) | 1 | 0 |

> 说明：`notify_rx_resets_silence` 中 P0 连续 5 周期无消息，静默计数累积至 5；
> P1 每周期 Check 前调用 NotifyRx 把计数复位为 0，随后 Check 再自增为 1，
> 最终 `silenceCount=1`，证明 NotifyRx 复位生效（若不复位则已达 10）。

### 规则: 错误警告检测

| 用例 | 阶段序列 | 期望 status | 期望 errWarnCount | 期望 safeLatched |
|---|---|---|---|---|
| err_warn_under_threshold | 前置: cycles=500; P0: cycles=49, tec=96, notifyRx=true | OK(0) | 49 | 0 |
| err_warn_at_threshold | 前置: cycles=500; P0: cycles=50, tec=96, notifyRx=true | ERROR_WARNING(3) | 50 | 1 |
| err_warn_rec_triggers | 前置: cycles=500; P0: cycles=50, rec=96, notifyRx=true | ERROR_WARNING(3) | 50 | 1 |
| err_warn_below_threshold_reset | 前置: cycles=500; P0: cycles=1, tec=95, rec=95, notifyRx=true | OK(0) | 0（else 复位分支） | 0 |
| err_warn_cleared_restarts | 前置: cycles=500; P0: cycles=10, tec=96, notifyRx=true; P1: cycles=1, tec=0, rec=0, notifyRx=true（清零）; P2: cycles=49, tec=96, notifyRx=true | OK(0) | 49（重新计时） | 0 |

> 说明：错误警告用例均配合 `notifyRx=true`（每周期消息到达），避免静默检测
> 抢先触发（静默阈值 20 周期 < 错误警告 50 周期）。`err_warn_below_threshold_reset`
> 覆盖 `(tec>=96)||(rec>=96)` 为假的 else 复位分支。`err_warn_cleared_restarts`
> 验证两计数器 <96 后 `ErrWarnCount` 复位为 0 并从新起算：若未复位，P2 末
> 应为 10+49=59 ≥ 50 早已触发；实际 49 < 50 不触发，证明清零生效。

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_defaults_ok` | 初始化后默认状态为 OK |
> | `uninitialized_check_noop` | 未初始化时 Check 不动作 |
> | `grace_suppresses_busoff` | 宽限期内 bus-off 被抑制 |
> | `grace_ends_monitoring_busoff` | 宽限期结束后监控生效（bus-off 立即触发） |
> | `busoff_immediate_safe_state` | 总线关闭立即应用安全状态 |
> | `busoff_latched_no_recovery` | 总线关闭锁存后总线恢复不解除安全状态 |
> | `silence_under_threshold` | 静默不足 20 周期不触发 |
> | `silence_at_threshold` | 静默恰达 20 周期触发安全状态 |
> | `notify_rx_resets_silence` | NotifyRx 到达复位静默计数器 |
> | `err_warn_under_threshold` | 错误警告持续不足 50 周期不触发 |
> | `err_warn_at_threshold` | 错误警告持续恰达 50 周期触发安全状态 |
> | `err_warn_rec_triggers` | REC 单独达到阈值同样触发错误警告 |
> | `err_warn_below_threshold_reset` | TEC 与 REC 均低于阈值时错误警告计数复位 |
> | `err_warn_cleared_restarts` | 错误警告计数清零后重新计时 |

## 代码路径覆盖

- `Swc_FzcCanMonitor_Init` 全部可执行行 ✅
- `Swc_FzcCanMonitor_Check` 全部可执行行 ✅
  - 未初始化守卫（`CanMon_Initialized != TRUE` → return）✅
  - 生产路径安全锁存（`SafeLatched == TRUE` → ApplySafeState + return）✅
  - 宽限期（`GraceCycles < 500` 两侧：抑制 / 结束监控）✅
  - bus-off 立即安全状态 ✅（两侧：`canMode == CAN_CS_STOPPED`）
  - 静默 20 周期触发 / 未达阈值 / NotifyRx 复位 ✅（两侧）
  - 错误警告 `(tec>=96)||(rec>=96)`：TEC 触发 / REC 单独触发 / 双低复位 ✅（三侧）
  - 错误警告 50 周期触发 / 未达阈值 / 清零重计时 ✅（两侧）
  - 健康返回 OK ✅
- `Swc_FzcCanMonitor_GetStatus` ✅（每场景 harness 输出读取）
- `Swc_FzcCanMonitor_NotifyRx` ✅（notifyRx 场景直接调用）
- `CanMon_ApplySafeState` ✅（三类故障触发时执行）
- UNIT_TEST 观测 getters（仅测试编译）✅ 由 harness 输出读取，全部命中

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_FzcCanMonitor.c.gcov.html` 实测（2026-08-16 完整套件 413 场景运行后，
含本 feature 14 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（83 / 83 行） |
| **分支覆盖** | **100%**（16 / 16 分支） |
| **函数覆盖** | **100%**（10 / 10 函数） |

覆盖到的函数：`Swc_FzcCanMonitor_Init`、`Swc_FzcCanMonitor_Check`、
`Swc_FzcCanMonitor_GetStatus`、`Swc_FzcCanMonitor_NotifyRx`（生产 API），
`CanMon_ApplySafeState`（私有静态函数），以及 5 个 `#ifdef UNIT_TEST` 观测
getter（`GetInitialized`、`GetSilenceCount`、`GetGraceCycles`、
`GetErrWarnCount`、`GetSafeLatched`）。

> 下表「实测命中」为完整套件（413 场景）单次运行后的累积值：本 feature 14 场景
> 共触发 **14 次 harness 调用**（其中 1 次 `skipInit` 跳过 Init，故
> `Swc_FzcCanMonitor_Init` 命中 13 次），`Check` 合计进入 **5766 次**，与各
> 场景 cycles 之和（1+1+1+501+501+502+519+520+510+549+550+550+501+560=5766）
> 一致。`NotifyRx` 命中 **215 次**，与 notifyRx 阶段 cycles 之和
> （5+49+50+50+1+60=215）一致。每次运行因容器重启会重新累积，具体数字可能
> 不同，但覆盖关系不变。生产固件编译不定义 `UNIT_TEST`，getter 相关行不计入
> 交付固件的有效代码。

---

## 行覆盖分析（100%，83/83）

行覆盖反映**每一行是否被执行**。83 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_FzcCanMonitor_Init（L65-73）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L65-66 | 函数签名 + 入口 `{` | 全部已初始化场景（每 harness 运行先 Init） | 13 |
| L67 | `CanMon_Status = FZC_CAN_OK` | `init_defaults_ok`（status=0 断言）及全部已初始化场景 | 13 |
| L68 | `CanMon_SilenceCount = 0u` | `init_defaults_ok`（silenceCount=0 断言）及全部已初始化场景 | 13 |
| L69 | `CanMon_ErrWarnCount = 0u` | `init_defaults_ok`（errWarnCount=0 断言）及全部已初始化场景 | 13 |
| L70 | `CanMon_SafeLatched = FALSE` | `init_defaults_ok`（safeLatched=0 断言）及全部已初始化场景 | 13 |
| L71 | `CanMon_GraceCycles = 0u` | `init_defaults_ok`（graceCycles=1 的起点）及全部已初始化场景 | 13 |
| L72 | `CanMon_Initialized = TRUE` | `init_defaults_ok`（initialized=1 断言）及全部已初始化场景 | 13 |
| L73 | 函数结束 `}` | 全部已初始化场景 | 13 |

#### CanMon_ApplySafeState（L82-95，私有静态）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L82-83 | 函数签名 + 入口 `{` | 三类故障触发场景 + 锁存重复应用 | 7 |
| L85 | `Rte_Write(FZC_SIG_BRAKE_CMD, 100)` | 故障场景（brakeCmd=100 断言） | 7 |
| L88 | `Rte_Write(FZC_SIG_STEER_CMD, 0)` | 故障场景（steerCmd=0 断言） | 7 |
| L91 | `Rte_Write(FZC_SIG_BUZZER_PATTERN, CONTINUOUS)` | 故障场景（buzzerPattern=4 断言） | 7 |
| L94 | `Dem_ReportErrorStatus(FZC_DTC_CAN_BUS_OFF, FAILED)` | 故障场景（dtcReported 断言） | 7 |
| L95 | 函数结束 `}` | 故障场景 | 7 |

> 命中 7 次的组成：`grace_ends_monitoring_busoff`（1）+ `busoff_immediate_safe_state`
> （1）+ `busoff_latched_no_recovery`（Given 1 次 + 刺激锁存重复应用 1 次 = 2）
> + `silence_at_threshold`（1）+ `err_warn_at_threshold`（1）+ `err_warn_rec_triggers`
> （1）= 7。✅

#### Swc_FzcCanMonitor_Check（L101-182）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L101-105 | 函数签名 + 局部变量声明 | 全部场景（每 Check 调用进入） | 5766 |
| L107 | `if (CanMon_Initialized != TRUE)` 守卫 | true 侧：`uninitialized_check_noop`（1 次）；false 侧：全部已初始化周期 | 5766 |
| L108-109 | `return; }`（未初始化空转） | `uninitialized_check_noop`（skipInit=true） | 1 |
| L131 | `if (CanMon_SafeLatched == TRUE)`（生产锁存路径） | true 侧：`busoff_latched_no_recovery` 刺激阶段（总线恢复仍锁存）；false 侧：其余周期 | 5765 |
| L132-134 | `CanMon_ApplySafeState(); return;` | `busoff_latched_no_recovery`（锁存后再次 Check → 重复应用安全状态，dtcReported=2 断言） | 1 |
| L140 | `if (CanMon_GraceCycles < 500)` 宽限期 | true 侧：宽限周期（graceCycles 递增）；false 侧：宽限结束后的监控周期 | 5764 |
| L141-144 | `GraceCycles++; SilenceCount=0; return;` | 宽限场景（`init_defaults_ok`、`grace_suppresses_busoff` 等宽限内周期） | 5502 |
| L147 | `canMode = Can_GetControllerMode(0u)` | 宽限结束后的全部监控周期（bus-off / 静默 / 错误警告 / 健康场景） | 262 |
| L148 | `if (canMode == CAN_CS_STOPPED)` | true 侧：`grace_ends_monitoring_busoff`、`busoff_immediate_safe_state`、`busoff_latched_no_recovery`（Given）；false 侧：其余监控周期 | 262 |
| L149-153 | `Status=BUS_OFF; SafeLatched=TRUE; ApplySafeState; return;` | bus-off 触发场景（status=BUS_OFF 断言） | 3 |
| L156 | `CanMon_SilenceCount++` | 非 bus-off 的全部监控周期 | 259 |
| L157 | `if (SilenceCount >= 20)` | true 侧：`silence_at_threshold`（第 20 周期）；false 侧：未达阈值周期 | 259 |
| L158-162 | `Status=SILENCE; SafeLatched=TRUE; ApplySafeState; return;` | `silence_at_threshold`（status=SILENCE 断言） | 1 |
| L165-167 | `tec=0; rec=0; Can_GetErrorCounters(0,&tec,&rec)` | 未触发静默的全部监控周期 | 258 |
| L168 | `if ((tec >= 96u) || (rec >= 96u))` | true 侧：`err_warn_*` 五场景（tec 或 rec ≥ 96）；false 侧：健康/静默/低阈值周期 | 258 |
| L169 | `CanMon_ErrWarnCount++` | 错误警告激活周期（tec/rec ≥ 96） | 208 |
| L170 | `if (ErrWarnCount >= 50)` | true 侧：`err_warn_at_threshold`、`err_warn_rec_triggers`（第 50 周期）；false 侧：不足 50 周期 | 208 |
| L171-175 | `Status=ERROR_WARNING; SafeLatched=TRUE; ApplySafeState; return;` | 错误警告触发场景（status=ERROR_WARNING 断言） | 2 |
| L177 | `else { CanMon_ErrWarnCount = 0u; }`（复位分支） | `err_warn_below_threshold_reset`（tec/rec 均 <96）、`err_warn_cleared_restarts` 清零阶段、静默/健康周期 | 50 |
| L181-182 | `CanMon_Status = FZC_CAN_OK; }` | 健康周期（status=OK 断言） | 256 |

#### Swc_FzcCanMonitor_GetStatus（L188-191）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L188-191 | 返回 `CanMon_Status` | 全部场景（harness 输出 JSON 的 status 字段逐次调用） | 14 |

#### Swc_FzcCanMonitor_NotifyRx（L197-200）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L197-200 | `CanMon_SilenceCount = 0u` | 全部 `notifyRx=true` 阶段（`notify_rx_resets_silence` 及五个 `err_warn_*` 场景） | 215 |

> 命中 215 次与 notifyRx 阶段 cycles 之和（`notify_rx_resets_silence` P1=5 +
> `err_warn_under_threshold`=49 + `err_warn_at_threshold`=50 +
> `err_warn_rec_triggers`=50 + `err_warn_below_threshold_reset`=1 +
> `err_warn_cleared_restarts`=10+1+49）一致。✅

#### UNIT_TEST 观测 getters（L211-234，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L211-214 | `GetInitialized` 返回初始化标志 | 全部场景（harness 输出 JSON 逐次调用） | 14 |
| L216-219 | `GetSilenceCount` 返回静默计数 | 全部场景 | 14 |
| L221-224 | `GetGraceCycles` 返回宽限计数 | 全部场景 | 14 |
| L226-229 | `GetErrWarnCount` 返回错误警告计数 | 全部场景 | 14 |
| L231-234 | `GetSafeLatched` 返回锁存标志 | 全部场景 | 14 |

> 常量/静态声明（L41-59 静态变量、L53 宏）为非执行行，不计入行覆盖。genhtml 的
> 行统计另含 8 个「带分支计数的条件行」：`L107`（`Initialized != TRUE`）、`L131`
> （`SafeLatched == TRUE`）、`L140`（`GraceCycles < 500`）、`L148`
> （`canMode == CAN_CS_STOPPED`）、`L157`（`SilenceCount >= 20`）、`L168`
> （`(tec>=96)||(rec>=96)`，双条件）、`L170`（`ErrWarnCount >= 50`）。全部由
> 上述场景命中两侧，故 83/83 行全部覆盖。

---

## 分支覆盖分析（100%，16/16）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `CanMon_Initialized != TRUE` | L107 | ✅ 两侧 | `uninitialized_check_noop`（true，1 次）/ 全部已初始化场景（false，5765 次） |
| `CanMon_SafeLatched == TRUE` | L131 | ✅ 两侧 | `busoff_latched_no_recovery` 刺激（true，1 次）/ 其余周期（false，5764 次） |
| `CanMon_GraceCycles < 500` | L140 | ✅ 两侧 | 宽限周期（true，5502 次）/ 监控周期（false，262 次） |
| `canMode == CAN_CS_STOPPED` | L148 | ✅ 两侧 | bus-off 场景（true，3 次）/ 其余监控周期（false，259 次） |
| `CanMon_SilenceCount >= 20` | L157 | ✅ 两侧 | `silence_at_threshold`（true，1 次）/ 未达阈值（false，258 次） |
| `tec >= 96u`（`\|\|` 左操作数） | L168 | ✅ 两侧 | `err_warn_*` 五场景（true）/ 健康/静默周期（false） |
| `rec >= 96u`（`\|\|` 右操作数） | L168 | ✅ 两侧 | `err_warn_rec_triggers`（rec=96，true）/ `err_warn_below_threshold_reset`、健康周期（false） |
| `CanMon_ErrWarnCount >= 50` | L170 | ✅ 两侧 | `err_warn_at_threshold`、`err_warn_rec_triggers`（true，2 次）/ 不足 50 周期（false，206 次） |

> 全部 8 个分支点两侧均已覆盖，无无法覆盖的分支。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（83/83） | 0 行 | — |
| 分支 | 100%（16/16） | 0 个 | — |
| 函数 | 100%（10/10） | — | — |

> **无法覆盖的代码说明**：`Swc_FzcCanMonitor.c` 中 L114-129 的
> `#ifdef PLATFORM_HIL` 分支（HIL 解锁恢复路径：条件清除后解除安全锁存并复位
> 状态）**不参与原生 ASW E2E 覆盖**。原因：
> 1. 原生 harness 以**生产固件配置**编译（不定义 `PLATFORM_HIL`），与交付固件
>    行为一致 —— 安全状态锁存至断电（fail-closed，L131-134）。
> 2. `PLATFORM_HIL` 是 HIL 平台专用编译标志，该解锁恢复逻辑由 HIL 测试
>    （`sil_004_can_busoff_fzc.yaml`，HIL 台架）覆盖，属 HIL 环境特性而非生产
>    固件行为。
> 3. gcov/lcov 只统计实际编译进 harness 二进制的行，该分支被预处理器排除，
>    不计入 83 行总数，也不产生未覆盖缺口。
>
> 其余全部生产代码路径（含所有分支两侧）均经公开 API（`Init` + `Check` +
> `NotifyRx` + `GetStatus`）驱动，无防御性不可达分支：本模块无配置指针参数，
> 与 `Swc_Watchdog`/`Swc_Scheduler` 中不可达的 `CfgPtr == NULL_PTR` 守卫不同，
> **无豁免项**。观测 getter（`#ifdef UNIT_TEST`）不计入交付固件，仅为测试编译
> 产物。

