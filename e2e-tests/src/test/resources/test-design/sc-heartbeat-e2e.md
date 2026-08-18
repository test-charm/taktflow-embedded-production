# SC 心跳监控 (sc_heartbeat) E2E 测试设计

## 被测功能

**SC 心跳监控（SWR-SC-004/005/006/027/028，ASIL D）**

三路独立 ECU（CVC/FZC/RZC）心跳监控模块：

- **SWR-SC-004 — 独立超时计数器**：每 ECU 独立 `hb_counter[3]`（10ms tick），
  `SC_Heartbeat_NotifyRx` 复位该 ECU 计数器；`SC_Heartbeat_Init` 清零全部
  计数与标志。
- **SWR-SC-005 — 150ms 超时检测**：`SC_Heartbeat_Monitor` 每 tick 递增计数，
  达到 `SC_HB_TIMEOUT_TICKS`（150）时置 `hb_timed_out`，同时点亮对应故障
  LED（`gioSetBit` 写 1）。超时前每 tick 清除故障 LED。
- **SWR-SC-006 — 200ms 确认窗口**：超时后再经 `SC_HB_CONFIRM_TICKS`（20）
  个 tick 将 `hb_confirmed` 锁存（不可恢复，仅断电复位）。确认窗口期间收到
  心跳可取消（需 `SC_HB_RECOVERY_THRESHOLD`=3 个连续心跳去抖）。
- **启动宽限期**：`SC_HB_STARTUP_GRACE_TICKS`（1500）内 Monitor 直接返回，
  避免 ECU 启动期间误报超时。
- **SWR-SC-027/028 — 内容校验**：`SC_Heartbeat_ValidateContent` 解析
  heartbeat byte3（低 4 位 OperatingMode、高 4 位 FaultStatus）；
  DEGRADED/LIMP 模式累计 `hb_stuck_degraded_cnt`（阈值 100）、FaultStatus
  ≥2 bit 累计 `hb_fault_escalate_cnt`（阈值 20），任一超限锁存
  `hb_content_fault`。
- **辅助查询**：`IsTimedOut`（越界→FALSE）、`IsAnyConfirmed`、`IsContentFault`
  （越界→FALSE）、`IsFzcBrakeFault`（FZC last_fault_status bit1）。

覆盖链路：

```text
测试 API 注入（op / ticks / ecu / repeats / payload3 / skipInit）
  → SC_Heartbeat_Init()：
       · 清零 counter / timed_out / confirm / recovery / content 状态
       · hb_startup_grace = SC_HB_STARTUP_GRACE_TICKS
  → SC_Heartbeat_NotifyRx(ecu)：
       · 越界 → 直接返回
       · hb_counter[ecu] = 0
       · 已确认 → 直接返回（锁存不可恢复）
       · 超时中 → recovery_count++，达阈值则取消超时并清 LED
  → SC_Heartbeat_Monitor()：
       · 宽限期内 → grace-- 并返回
       · 每 ECU：已确认跳过；counter++（<0xFFFF）
       · 未超时 → 清 LED；达 150 → 置超时 + 点 LED
       · 已超时 → 复位 recovery、confirm_counter++；达 20 → 确认锁存
  → SC_Heartbeat_ValidateContent(ecu, payload)：
       · 越界 / NULL → 直接返回
       · 解析 mode/faults；更新 last_fault_status
       · DEGRADED/LIMP → stuck++；否则清零
       · faults ≥2 bit → escalate++；否则清零
       · 任一超限 → content_fault 锁存
  → 观测（harness 输出）：results[] 每操作 state 快照
       · counters[3] / timedOut[3] / confirmed[3] / recoveryCounts[3] /
         confirmCounters[3] / stuckDegraded[3] / faultEscalate[3] /
         contentFault[3] / lastFaultStatus[3] / startupGrace /
         anyConfirmed / fzcBrakeFault / leds[3]
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_heartbeat.c` 生产代码。由于模块内部状态全部为 `static` 文件作用域，且
`PLATFORM_HIL` 分支（Monitor 跳过 FZC）在 harness 以生产配置编译时被预处理器
排除，参照 `sc_state` / `Swc_RzcNvm` 的既有做法，在 `sc_heartbeat.c/.h`
增加 **UNIT_TEST 保护的观测 getter**（仅测试编译，不影响交付固件）：

- `SC_Heartbeat_TestGetCounter(ecu)` / `TestGetStartupGrace()` —
  观测超时计数器与启动宽限计数器；
- `SC_Heartbeat_TestGetTimedOut(ecu)` / `TestGetConfirmed(ecu)` /
  `TestGetRecoveryCount(ecu)` / `TestGetConfirmCounter(ecu)` —
  观测超时/确认/恢复去抖中间状态；
- `SC_Heartbeat_TestGetStuckDegradedCnt(ecu)` / `TestGetFaultEscalateCnt(ecu)` /
  `TestGetContentFault(ecu)` / `TestGetLastFaultStatus(ecu)` —
  观测内容校验计数与锁存。

> **被测代码观测**：生产固件（TMS570）不定义 `UNIT_TEST`，上述 getter 绝不
> 进入交付固件。LED 状态经 harness 的 `gioSetBit` mock 计数观测；超时/确认/
> 内容故障经既有公开 API（`IsTimedOut` / `IsAnyConfirmed` / `IsContentFault` /
> `IsFzcBrakeFault`）交叉验证。

## 被测代码流程图

### SC_Heartbeat_Init（L72-L87）

```text
[Init]
  ═══→ [for i in 0..3)
           counter=0 / timed_out=F / stuck=0 / escalate=0 /
           content_fault=F / last_fault=0 / confirm=0 / confirmed=F / recovery=0
  ═══→ [startup_grace = 1500]
```

### SC_Heartbeat_NotifyRx（L89-L114）

```text
[NotifyRx(ecu)]
  ═══→ {ecu >= 3?} ─Y→ [return]
   ↓ N
  [counter[ecu] = 0]
  {confirmed[ecu]?} ─Y→ [return]（锁存不可恢复）
   ↓ N
  {timed_out[ecu]?} ─Y→ [recovery_count[ecu]++]
                              ↓ {>= 3?}
                                  ├─ Y → [timed_out=F / confirm=0 / recovery=0 / 清 LED]
                                  └─ N → [return]
   ↓ N（未超时，计数复位即可）
  [return]
```

### SC_Heartbeat_Monitor（L116-L169）

```text
[Monitor]
  ═══→ {startup_grace > 0?} ─Y→ [grace--] → [return]
   ↓ N
  [for i in 0..3)
     {confirmed[i]?} ─Y→ [continue]
      ↓ N
     [counter[i]++（<0xFFFF 饱和）]
     {未超时 && 未确认?} ─Y→ [清 LED]
      ↓
     {counter[i] >= 150?}
        ├─ N → [continue]
        └─ Y → {timed_out[i]?}
                  ├─ N（首次）→ [timed_out=T / confirm=0 / 点 LED]
                  └─ Y（持续）→ [recovery=0 / confirm_counter++]
                                   ↓ {>= 20?} → [confirmed=T]
```

### SC_Heartbeat_ValidateContent（L197-L243）

```text
[ValidateContent(ecu, payload)]
  ═══→ {ecu >= 3 || payload == NULL?} ─Y→ [return]
   ↓ N
  [mode = payload[3] & 0x0F; faults = payload[3] >> 4]
  [last_fault_status[ecu] = faults]
  {mode == 2 || mode == 3?} ─Y→ [stuck_degraded++（<0xFF）]
                              └─N→ [stuck_degraded = 0]
  [bit_count = popcount(faults)]
  {bit_count >= 2?} ─Y→ [fault_escalate++（<0xFF）]
                     └─N→ [fault_escalate = 0]
  {stuck >= 100 || escalate >= 20?} ─Y→ [content_fault = T]
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `monitor` / `notifyRx` / `validate` | When — 执行控制 |
| `skipInit` | 首阶段是否跳过自动 Init | `false`、`true` | When — 执行控制 |
| `ticks` | `monitor` 调用次数 | `1`、`149`（超时前边界）、`150`（超时边界）、`169`（确认前）、`170`（确认边界）、`1500`（宽限边界） | When — 载荷 |
| `ecu` | 目标 ECU 索引 | `0`(CVC)、`1`(FZC)、`2`(RZC)、`3`/`255`（越界） | When — 载荷 |
| `repeats` | `notifyRx`/`validate` 重复次数 | `1`、`2`、`3`（恢复阈值边界）、`19`/`20`（escalate 阈值边界）、`99`/`100`（stuck 阈值边界） | When — 载荷 |
| `payload3` | `validate` heartbeat byte3 | `0x00`(NORMAL)、`0x02`(DEGRADED)、`0x03`(LIMP)、`0x30`(2 fault bits)、`0x10`(1 fault bit)、`0x20`(FZC 制动 bit1) | When — 载荷 |
| 阶段序列 | 多阶段路径 | init→monitor、monitor→notifyRx→monitor、init→monitor×1500→monitor、validate×N→validate | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].state.counters[ecu]` | 超时计数器 | 0..150，NotifyRx 后归 0 |
| `results[i].state.timedOut[ecu]` | 超时标志 | 0/1 |
| `results[i].state.confirmed[ecu]` | 确认锁存标志 | 0/1 |
| `results[i].state.recoveryCounts[ecu]` | 恢复去抖计数 | 0..3 |
| `results[i].state.confirmCounters[ecu]` | 确认窗口计数 | 0..20 |
| `results[i].state.stuckDegraded[ecu]` | 降级累计 | 0..100 |
| `results[i].state.faultEscalate[ecu]` | 故障升级累计 | 0..20 |
| `results[i].state.contentFault[ecu]` | 内容故障锁存 | 0/1 |
| `results[i].state.lastFaultStatus[ecu]` | 最近 FaultStatus | 0x0..0xF |
| `results[i].state.startupGrace` | 宽限计数器 | 1500→0 |
| `results[i].state.anyConfirmed` | `IsAnyConfirmed()` | 0/1 |
| `results[i].state.fzcBrakeFault` | `IsFzcBrakeFault()` | 0/1 |
| `results[i].state.leds[ecu]` | 故障 LED 状态（gioSetBit mock） | 0/1 |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。
> 为控制用例规模，超时类用例统一先 `monitor ticks=1500` 消费启动宽限期
> （`skipInit` 不用，宽限本身也是被测功能）。

### 规则: 初始化与启动宽限期 — SC_Heartbeat_Init / Monitor

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_resets_all_counters` | P0: init | counters 全 0；timedOut 全 0；startupGrace=1500 |
| `startup_grace_prevents_timeout` | P0: init; P1: monitor(1499); P2: monitor(1) | 1499 与 1500 tick 后仍无超时；startupGrace=0 |
| `grace_expired_starts_counting` | P0: init; P1: monitor(1500); P2: monitor(1) | 宽限后 1 tick：counters[CVC]=1 且未超时 |

### 规则: 超时检测 — SWR-SC-004 / SWR-SC-005

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `counter_increments_below_timeout` | P0: init; P1: monitor(1500); P2: monitor(149) | counters[CVC]=149；timedOut[CVC]=0 |
| `timeout_at_150_ticks` | P0: init; P1: monitor(1500); P2: monitor(150) | counters=150；timedOut 全 1；leds 全 1 |
| `notify_rx_resets_counter` | P0: init; P1: monitor(1500); P2: monitor(100); P3: notifyRx(CVC); P4: monitor(149) | NotifyRx 后 counter=0；150 tick 后仍不超时 |
| `notify_rx_invalid_ecu_ignored` | P0: init; P1: monitor(1500); P2: notifyRx(3); P3: notifyRx(255); P4: monitor(150) | 越界 NotifyRx 无效，仍超时 |
| `independent_ecu_timeout` | P0: init; P1: monitor(1500); P2: monitor(150, notifyA=FZC, notifyB=RZC) | 仅 CVC 超时；FZC/RZC 每 tick 收到心跳、计数器保持低位 |
| `confirmed_caps_counter_before_65535` | P0: init; P1: monitor(1500); P2: monitor(65535) | 确认锁存后 Monitor 跳过该 ECU；counter 停在 170（不递增到 0xFFFF） |

### 规则: 确认窗口与锁存 — SWR-SC-006

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `timeout_not_confirmed_until_window` | P0: init; P1: monitor(1500); P2: monitor(150); P3: monitor(19) | 超时后 19 tick：timedOut=1 但 confirmed=0 |
| `confirmed_after_20_ticks` | P0: init; P1: monitor(1500); P2: monitor(150); P3: monitor(20) | confirmCounter=20；confirmed=1；anyConfirmed=1 |
| `confirmed_latched_no_recovery` | P0: init; P1: monitor(1500); P2: monitor(170); P3: notifyRx(CVC)×6 | 确认后 NotifyRx 无效，confirmed 仍=1（锁存） |
| `recovery_requires_3_consecutive` | P0: init; P1: monitor(1500); P2: monitor(150); P3: notifyRx(CVC)×2; P4: monitor(1) | 2 个心跳不足以恢复；timedOut 仍=1 |
| `recovery_after_3_consecutive` | P0: init; P1: monitor(1500); P2: monitor(150); P3: notifyRx(CVC)×3; P4: monitor(20) | CVC 恢复：timedOut[0]=0、LED 熄灭；FZC/RZC 继续超时并经确认窗口锁存 confirmed=[0,1,1] |

### 规则: 内容校验 — SWR-SC-027 / SWR-SC-028

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `degraded_mode_accumulates_stuck` | P0: init; P1: validate(CVC, payload3=0x02)×99 | stuckDegraded=99；contentFault=0 |
| `stuck_threshold_latches_fault` | P0: init; P1: validate(CVC, payload3=0x02)×100 | stuckDegraded=100；contentFault=1 |
| `stuck_counter_saturates_255` | P0: init; P1: validate(CVC, payload3=0x02)×300 | stuckDegraded=255（<0xFF 饱和）；contentFault=1 |
| `normal_mode_resets_stuck` | P0: init; P1: validate(CVC, payload3=0x02)×10; P2: validate(CVC, payload3=0x00) | NORMAL 模式清零 stuckDegraded |
| `two_fault_bits_accumulate_escalate` | P0: init; P1: validate(FZC, payload3=0x30)×19 | faultEscalate=19；contentFault=0 |
| `escalate_threshold_latches_fault` | P0: init; P1: validate(FZC, payload3=0x30)×20 | faultEscalate=20；contentFault=1 |
| `escalate_counter_saturates_255` | P0: init; P1: validate(FZC, payload3=0x30)×300 | faultEscalate=255（<0xFF 饱和）；contentFault=1 |
| `single_fault_bit_resets_escalate` | P0: init; P1: validate(FZC, payload3=0x30)×5; P2: validate(FZC, payload3=0x10) | 单 bit 清零 faultEscalate |
| `four_fault_bits_all_set` | P0: init; P1: validate(RZC, payload3=0xF0) | lastFaultStatus[2]=0xF；faultEscalate[2]=1（4 bit 全置） |
| `limp_mode_accumulates_stuck` | P0: init; P1: validate(RZC, payload3=0x03) | stuckDegraded[2]=1（LIMP 模式计入降级） |
| `validate_invalid_ecu_or_null_ignored` | P0: init; P1: validate(3, payload3=0x30) | 越界 validate 无效，不更新状态 |
| `fzc_brake_fault_bit_detected` | P0: init; P1: validate(FZC, payload3=0x20) | lastFaultStatus[FZC]=0x2；fzcBrakeFault=1 |
| `is_content_fault_invalid_index_false` | P0: init; P1: validate(3, payload3=0x30) | IsContentFault(3)=0（越界守卫） |

## 代码路径覆盖

- `SC_Heartbeat_Init`：全槽清零 + 宽限置位路径覆盖。
- `SC_Heartbeat_NotifyRx`：越界、计数复位、已确认锁存返回、超时恢复去抖
  （<3 / ≥3 两侧）全覆盖。
- `SC_Heartbeat_Monitor`：宽限期、已确认跳过、counter 递增、LED 清/点、
  首次超时、持续超时 confirm 递增、确认锁存全覆盖。
- `SC_Heartbeat_IsTimedOut` / `IsAnyConfirmed` / `IsContentFault` /
  `IsFzcBrakeFault`：越界与正常路径全覆盖。
- `SC_Heartbeat_ValidateContent`：越界/NULL、mode 两分支、faults 两分支、
  stuck/escalate 阈值锁存全覆盖。

## 无法覆盖的代码说明

> **编译期排除**：`SC_Heartbeat_Monitor` 中 `#ifdef PLATFORM_HIL` 跳过 FZC
> 分支（L127-L132）在 harness 以生产配置（不定义 `PLATFORM_HIL`）编译时被
> 预处理器排除，不计入行统计。该 HIL 平台特性由 HIL 测试
> `test_hil_heartbeat.py` 覆盖（与 FZC CanMonitor 的 HIL 分支同理）。
>
> **防御性守卫豁免（2 个分支）**：
> 1. `SC_Heartbeat_Monitor` 中 `if (hb_counter[i] < 0xFFFFu)`（L139）的
>    false 侧（counter 饱和 0xFFFF 不递增）经公开 API 不可达：确认锁存
>    发生在 170 tick（150 超时 + 20 确认），此后 Monitor 跳过已确认 ECU
>    （L134），counter 无法继续递增至 0xFFFF。实测 65535 tick 后 counter
>    停在 170。
> 2. `SC_Heartbeat_Monitor` 中 `if ((hb_timed_out[i] == FALSE) &&
>    (hb_confirmed[i] == FALSE))`（L144）的第二条件 `hb_confirmed[i] ==
>    FALSE` 的 false 侧不可达：该行之前 L134 已 `continue` 跳过所有已确认
>    ECU，执行到 L144 时 `hb_confirmed[i]` 恒为 FALSE，第二条件为冗余
>    防御性检查。
>
> 以上两处均属防御性代码，以文档化豁免处理（与 CVC Watchdog 的
> `Wdg_CfgPtr == NULL_PTR` 守卫同理）。除此之外 **179/179 行、18/18 函数
> 全部覆盖，分支 80/82（97.6%）**，无其他无法覆盖的代码。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_heartbeat.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（179 / 179）** |
| 分支覆盖 | **97.6%（80 / 82）** |
| 函数覆盖 | **100%（18 / 18）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_heartbeat.feature` | **26 scenarios / 156 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **627 scenarios / 3791 steps passed** |

函数命中次数（`sc_heartbeat.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `SC_Heartbeat_Init` | 104 |
| `SC_Heartbeat_NotifyRx` | 628 |
| `SC_Heartbeat_Monitor` | 173228 |
| `SC_Heartbeat_IsTimedOut` | 832 |
| `SC_Heartbeat_IsAnyConfirmed` | 208 |
| `SC_Heartbeat_IsFzcBrakeFault` | 208 |
| `SC_Heartbeat_ValidateContent` | 2324 |
| `SC_Heartbeat_IsContentFault` | 832 |
| `SC_Heartbeat_TestGetCounter` | 832 |
| `SC_Heartbeat_TestGetStartupGrace` | 208 |
| `SC_Heartbeat_TestGetTimedOut` | 832 |
| `SC_Heartbeat_TestGetConfirmed` | 832 |
| `SC_Heartbeat_TestGetRecoveryCount` | 832 |
| `SC_Heartbeat_TestGetConfirmCounter` | 832 |
| `SC_Heartbeat_TestGetStuckDegradedCnt` | 832 |
| `SC_Heartbeat_TestGetFaultEscalateCnt` | 832 |
| `SC_Heartbeat_TestGetContentFault` | 832 |
| `SC_Heartbeat_TestGetLastFaultStatus` | 832 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_heartbeat.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### SC_Heartbeat_Init（L72-L87）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L73 | `{` | 104 | 全部用例（harness 启动与显式 init 阶段均调用） |
| L74 | `uint8 i;` | 104 | 全部用例 |
| L75 | `for (i = 0u; i < SC_ECU_COUNT; i++)` | 416 | 全部用例（3 槽 × 每 init） |
| L76 | `hb_counter[i] = 0u;` | 312 | 全部用例 |
| L77 | `hb_timed_out[i] = FALSE;` | 312 | 全部用例 |
| L78 | `hb_stuck_degraded_cnt[i] = 0u;` | 312 | 全部用例 |
| L79 | `hb_fault_escalate_cnt[i] = 0u;` | 312 | 全部用例 |
| L80 | `hb_content_fault[i] = FALSE;` | 312 | 全部用例 |
| L81 | `hb_last_fault_status[i] = 0u;` | 312 | 全部用例 |
| L82 | `hb_confirm_counter[i] = 0u;` | 312 | 全部用例 |
| L83 | `hb_confirmed[i] = FALSE;` | 312 | 全部用例 |
| L84 | `hb_recovery_count[i] = 0u;` | 312 | 全部用例 |
| L86 | `hb_startup_grace = SC_HB_STARTUP_GRACE_TICKS;` | 104 | 全部用例 |
| L87 | `}` | 104 | 全部用例 |

#### SC_Heartbeat_NotifyRx（L89-L114）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L90 | `{` | 628 | `notify_rx_resets_counter`、`notify_rx_invalid_ecu_ignored`、`recovery_requires_3_consecutive`、`recovery_after_3_consecutive`、`confirmed_latched_no_recovery` 等 |
| L91 | `if (ecuIndex >= SC_ECU_COUNT)` | 628 | true 侧 `notify_rx_invalid_ecu_ignored`（ecu=3/255）；false 侧其余 |
| L92 | `return;` | 4 | `notify_rx_invalid_ecu_ignored` |
| L95 | `hb_counter[ecuIndex] = 0u;` | 624 | 所有有效 NotifyRx |
| L98 | `if (hb_confirmed[ecuIndex] == TRUE)` | 624 | true 侧 `confirmed_latched_no_recovery`；false 侧其余 |
| L99 | `return;` | 12 | `confirmed_latched_no_recovery`（6 次重复 × 2 轮） |
| L102 | `if (hb_timed_out[ecuIndex] == TRUE)` | 612 | true 侧 `recovery_requires_3_consecutive`、`recovery_after_3_consecutive`；false 侧 `notify_rx_resets_counter` |
| L105 | `hb_recovery_count[ecuIndex]++;` | 10 | `recovery_requires_3_consecutive`（2 次）、`recovery_after_3_consecutive`（3 次）等 |
| L106 | `if (hb_recovery_count >= SC_HB_RECOVERY_THRESHOLD)` | 10 | true 侧 `recovery_after_3_consecutive`；false 侧 `recovery_requires_3_consecutive` |
| L107 | `hb_timed_out[ecuIndex] = FALSE;` | 2 | `recovery_after_3_consecutive` |
| L108 | `hb_confirm_counter[ecuIndex] = 0u;` | 2 | `recovery_after_3_consecutive` |
| L109 | `hb_recovery_count[ecuIndex] = 0u;` | 2 | `recovery_after_3_consecutive` |
| L110 | `gioSetBit(...0u);` | 2 | `recovery_after_3_consecutive`（LED 熄灭） |

#### SC_Heartbeat_Monitor（L116-L169）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L117 | `{` | 173228 | 全部用例（monitor 阶段） |
| L118 | `uint8 i;` | 173228 | 全部用例 |
| L121 | `if (hb_startup_grace > 0u)` | 173228 | true 侧 `startup_grace_prevents_timeout`（前 1500 tick）；false 侧其余 |
| L122 | `hb_startup_grace--;` | 39000 | `startup_grace_prevents_timeout` |
| L123 | `return;` | 39000 | `startup_grace_prevents_timeout`（宽限期内直接返回） |
| L126 | `for (i = 0u; i < SC_ECU_COUNT; i++)` | 536912 | 全部 monitor 用例 |
| L134 | `if (hb_confirmed[i] == TRUE)` | 402684 | true 侧 `confirmed_caps_counter_before_65535`、`confirmed_latched_no_recovery`（确认后跳过）；false 侧其余 |
| L135 | `continue;` | 392190 | `confirmed_caps_counter_before_65535`、`confirmed_after_20_ticks` 等确认后继续 monitor |
| L139 | `if (hb_counter[i] < 0xFFFFu)` | 10494 | true 侧所有正常 monitor；false 侧不可达（见豁免） |
| L140 | `hb_counter[i]++;` | 10494 | 所有未确认 monitor 递增 |
| L144 | `if ((hb_timed_out[i] == FALSE) && (hb_confirmed[i] == FALSE))` | 10494 | 第一条件两侧覆盖；第二条件 false 侧不可达（见豁免） |
| L145 | `gioSetBit(...0u);` | 9934 | 未超时期间的 LED 保持清除 |
| L149 | `if (hb_counter[i] >= SC_HB_TIMEOUT_TICKS)` | 10494 | true 侧 `timeout_at_150_ticks` 等；false 侧 149 tick 用例 |
| L150 | `if (hb_timed_out[i] == FALSE)` | 608 | true 侧 `timeout_at_150_ticks`（首次检测）；false 侧持续超时 |
| L152 | `hb_timed_out[i] = TRUE;` | 50 | `timeout_at_150_ticks`、`timeout_not_confirmed_until_window` 等 |
| L153 | `hb_confirm_counter[i] = 0u;` | 50 | 首次超时检测 |
| L156 | `gioSetBit(...1u);` | 50 | 首次超时点亮 LED |
| L157 | `else` | 558 | 持续超时（确认窗口期间） |
| L159 | `hb_recovery_count[i] = 0u;` | 558 | 持续超时重置恢复计数 |
| L160 | `hb_confirm_counter[i]++;` | 558 | 确认窗口递增 |
| L163 | `if (hb_confirm_counter[i] >= SC_HB_CONFIRM_TICKS)` | 558 | true 侧 `confirmed_after_20_ticks`、`confirmed_caps_counter_before_65535`；false 侧 `timeout_not_confirmed_until_window`（19 tick） |
| L164 | `hb_confirmed[i] = TRUE;` | 22 | `confirmed_after_20_ticks`、`confirmed_caps_counter_before_65535` 等 |
| L166 | `}` | 558 | 持续超时分支 |
| L167 | `}` | 608 | 超时阈值分支 |
| L168 | `}` | 10494 | 每 ECU 循环体结束 |
| L169 | `}` | 134228 | Monitor 函数结束 |

#### SC_Heartbeat_IsTimedOut / IsAnyConfirmed / IsFzcBrakeFault（L171-L195）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L172 | `SC_Heartbeat_IsTimedOut` 函数体 | 832 | 全部用例（harness 每阶段经 API 快照 + guardProbe 越界） |
| L173 | `if (ecuIndex >= SC_ECU_COUNT)` | 832 | true 侧 guardProbe（SC_ECU_COUNT）；false 侧正常索引 |
| L174 | `return FALSE;` | 208 | guardProbe 越界 |
| L176 | `return hb_timed_out[ecuIndex];` | 624 | 正常索引查询 |
| L180 | `SC_Heartbeat_IsAnyConfirmed` 函数体 | 208 | 全部用例（harness 最终输出） |
| L182 | `for (i = 0u; i < SC_ECU_COUNT; i++)` | 782 | 全部用例 |
| L183 | `if (hb_confirmed[i] == TRUE)` | 592 | true 侧 `confirmed_after_20_ticks` 等；false 侧未确认 |
| L184 | `return TRUE;` | 18 | `confirmed_after_20_ticks`、`confirmed_caps_counter_before_65535` 等 |
| L187 | `return FALSE;` | 190 | 无确认时 |
| L191 | `SC_Heartbeat_IsFzcBrakeFault` 函数体 | 208 | 全部用例（harness 最终输出） |
| L194 | `return (... != 0u) ? TRUE : FALSE;` | 208 | true 侧 `fzc_brake_fault_bit_detected`；false 侧其余 |

#### SC_Heartbeat_ValidateContent（L197-L243）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L198 | `{` | 2324 | 全部 validate 用例 |
| L199 | `uint8 mode;` | 2324 | 全部 validate 用例 |
| L200 | `uint8 faults;` | 2324 | 全部 validate 用例 |
| L201 | `uint8 bit_count;` | 2324 | 全部 validate 用例 |
| L203 | `if ((ecuIndex >= SC_ECU_COUNT) \|\| (payload == NULL_PTR))` | 2324 | 第一条件 true 侧 `validate_invalid_ecu_or_null_ignored`（ecu=3/255）；第二条件 true 侧 harness NULL probe；false 侧正常 |
| L204 | `return;` | 608 | 越界/NULL validate |
| L209 | `mode = payload[3] & 0x0Fu;` | 1716 | 正常 validate |
| L210 | `faults = (payload[3] >> 4u) & 0x0Fu;` | 1716 | 正常 validate |
| L212 | `hb_last_fault_status[ecuIndex] = faults;` | 1716 | 正常 validate |
| L215 | `if ((mode == 2u) \|\| (mode == 3u))` | 1716 | mode==2 侧 `degraded_*`；mode==3 侧 `limp_mode_accumulates_stuck`；false 侧 `normal_mode_resets_stuck` |
| L216 | `if (hb_stuck_degraded_cnt < 0xFFu)` | 1020 | true 侧 DEGRADED/LIMP 累计；false 侧 `stuck_counter_saturates_255` |
| L217 | `hb_stuck_degraded_cnt[ecuIndex]++;` | 930 | DEGRADED/LIMP 累计 |
| L220 | `hb_stuck_degraded_cnt[ecuIndex] = 0u;` | 696 | NORMAL 模式清零 |
| L224 | `bit_count = 0u;` | 1716 | 正常 validate |
| L225 | `bit_count += ((faults & 0x01u) != 0u) ? 1u : 0u;` | 1716 | bit0 两侧（`fzc_brake_fault_bit_detected` 置位、其余清零） |
| L226 | `bit_count += ((faults & 0x02u) != 0u) ? 1u : 0u;` | 1716 | bit1 两侧 |
| L227 | `bit_count += ((faults & 0x04u) != 0u) ? 1u : 0u;` | 1716 | bit2 两侧（`four_fault_bits_all_set` 置位） |
| L228 | `bit_count += ((faults & 0x08u) != 0u) ? 1u : 0u;` | 1716 | bit3 两侧（`four_fault_bits_all_set` 置位） |
| L230 | `if (bit_count >= 2u)` | 1716 | true 侧 `two_fault_bits_*`、`four_fault_bits_all_set`；false 侧 `single_fault_bit_resets_escalate` |
| L231 | `if (hb_fault_escalate_cnt < 0xFFu)` | 690 | true 侧双 bit 累计；false 侧 `escalate_counter_saturates_255` |
| L232 | `hb_fault_escalate_cnt[ecuIndex]++;` | 600 | 双 bit 累计 |
| L235 | `hb_fault_escalate_cnt[ecuIndex] = 0u;` | 1026 | 单 bit 清零 |
| L239 | `if ((stuck >= 100) \|\| (escalate >= 20))` | 1716 | 第一条件 true 侧 `stuck_threshold_latches_fault`；第二条件 true 侧 `escalate_threshold_latches_fault`；false 侧未达阈值 |
| L240 | `(hb_fault_escalate_cnt >= SC_HB_FAULT_ESCALATE_MAX)` | 1716 | escalate 阈值两侧 |
| L241 | `hb_content_fault[ecuIndex] = TRUE;` | 968 | `stuck_threshold_latches_fault`、`escalate_threshold_latches_fault`、饱和用例 |

#### SC_Heartbeat_IsContentFault（L245-L251）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L246 | 函数体 | 832 | 全部用例（harness API 快照 + guardProbe） |
| L247 | `if (ecuIndex >= SC_ECU_COUNT)` | 832 | true 侧 guardProbe（SC_ECU_COUNT）；false 侧正常 |
| L248 | `return FALSE;` | 208 | guardProbe 越界 |
| L250 | `return hb_content_fault[ecuIndex];` | 624 | 正常索引查询 |

#### UNIT_TEST getter（L258-L333，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L260-L263 | `TestGetCounter` 越界守卫 + 正常返回 | 832/208/624 | 全部用例（每阶段快照）+ guardProbe 越界 |
| L268 | `TestGetStartupGrace` | 208 | 全部用例 |
| L273-L276 | `TestGetTimedOut` | 832/208/624 | 全部用例 + guardProbe |
| L281-L284 | `TestGetConfirmed` | 832/208/624 | 全部用例 + guardProbe |
| L289-L292 | `TestGetRecoveryCount` | 832/208/624 | 全部用例 + guardProbe |
| L297-L300 | `TestGetConfirmCounter` | 832/208/624 | 全部用例 + guardProbe |
| L305-L308 | `TestGetStuckDegradedCnt` | 832/208/624 | 全部用例 + guardProbe |
| L313-L316 | `TestGetFaultEscalateCnt` | 832/208/624 | 全部用例 + guardProbe |
| L321-L324 | `TestGetContentFault` | 832/208/624 | 全部用例 + guardProbe |
| L329-L332 | `TestGetLastFaultStatus` | 832/208/624 | 全部用例 + guardProbe |

### 分支覆盖分析

- `NotifyRx`：越界（L91）、确认锁存（L98）、超时恢复（L102）、恢复阈值
  （L106）全部两侧覆盖。
- `Monitor`：宽限期（L121）、确认跳过（L134）、LED 清/点（L145/L156）、
  超时阈值（L149）、首次/持续超时（L150）、确认阈值（L163）两侧覆盖。
- `ValidateContent`：mode 或条件（L215）、stuck/escalate 饱和守卫
  （L216/L231）、bit0-bit3 四个 popcount 位（L225-L228）、bit_count 阈值
  （L230）、内容故障锁存（L239）全部两侧覆盖。
- 查询函数越界守卫（L173/L247）与 getter 越界守卫全部两侧覆盖。
- **2 个豁免分支**：L139 counter 饱和 false 侧、L144 冗余 confirmed 检查
  false 侧（详见「无法覆盖的代码说明」）。
