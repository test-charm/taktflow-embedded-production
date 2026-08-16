# CVC CAN 总线监控 (Swc_CanMonitor) E2E 测试设计

## 被测功能

**CVC ASW CAN 总线健康监控 SWC — 总线丢失检测（bus-off 立即触发、200ms 静默、
错误警告持续 500ms 三种故障路径 → SAFE_STOP）+ 总线恢复（10s 窗口内最多 3 次
恢复尝试，第 4 次失败 → SHUTDOWN 终态）**

覆盖链路：

```text
测试 API 注入（isBusOff / rxMsgCount / errorWarning / currentTimeMs）
  → Swc_CanMonitor_Init()（状态复位：OK + 静默/错误警告/恢复追踪清零）
  → Swc_CanMonitor_Check()（10ms 周期，SWR-CVC-024）：
       · 未初始化守卫 → 直接 OK
       · SHUTDOWN 终态短路 → 直接 SHUTDOWN
       · Check 1: isBusOff == TRUE → Status=BUSOFF, 返回 SAFE_STOP（立即）
       · Check 2: rxMsgCount 无新消息且静默 >= 200ms
                    → Status=SILENCE, 返回 SAFE_STOP
                 新消息到达 → 重置静默定时器
       · Check 3: 错误警告持续 >= 500ms
                    → Status=ERROR_WARNING, 返回 SAFE_STOP
                 错误警告清除 → 重置追踪
       · 健康 → Status=OK
  → Swc_CanMonitor_Recovery()（SWR-CVC-025）：
       · 未初始化守卫 → E_NOT_OK
       · SHUTDOWN 终态短路 → E_NOT_OK
       · 10s 窗口过期 → 恢复计数器清零
       · 第 4 次恢复尝试（> CANMON_MAX_RECOVERY_ATTEMPTS=3）→ SHUTDOWN + E_NOT_OK
       · 成功 → Status=OK, ErrorWarnActive=FALSE, E_OK
  → Swc_CanMonitor_GetStatus()（状态观测）
```

与既有 ASW E2E（`Swc_Heartbeat`、`Swc_CvcCom` 等）一致，通过测试专用 API 在
原生测试框架内执行真实的 `Swc_CanMonitor.c` 生产代码。总线监控的输入（bus-off
标志、RX 消息计数、错误警告标志、时间）由 harness 脚本注入，内部状态（静默
定时器、错误警告追踪、恢复计数器）经 `#ifdef UNIT_TEST` 观测 getter 断言。

> **被测代码观测**：`CanMon_Initialized`、`CanMon_LastRxCount`、
> `CanMon_LastRxTimeMs`、`CanMon_ErrorWarnStartMs`、`CanMon_ErrorWarnActive`、
> `CanMon_RecoveryAttempts`、`CanMon_RecoveryWindowStartMs` 均为模块静态状态，
> 无法从外部直接读取。为支持 E2E 断言，在 `Swc_CanMonitor.c/.h` 增加了
> **`#ifdef UNIT_TEST` 保护**的观测 getter。生产固件构建不定义 `UNIT_TEST`，
> 这些访问器不进入交付固件；仅测试 harness 编译时生效。`CanMon_Status` 已有
> 生产 API `Swc_CanMonitor_GetStatus()` 可观测。

## 被测代码流程图

```
┌──────────────────────────────┐
│ Swc_CanMonitor_Init          │
│ Status=OK, LastRxCount=0     │
│ LastRxTimeMs=0               │
│ ErrorWarnStartMs=0           │
│ ErrorWarnActive=FALSE        │
│ RecoveryAttempts=0           │
│ RecoveryWindowStartMs=0      │
│ Initialized=TRUE             │
└─────────────┬────────────────┘
              │
              ▼
┌──────────────────────────────┐
│ Swc_CanMonitor_Check(        │
│   isBusOff, rxMsgCount,      │
│   errorWarning, currentTimeMs)│
└─────────────┬────────────────┘
              │
 Initialized != TRUE? ──Y──→ return OK
              │N
 Status == SHUTDOWN? ──Y──→ return SHUTDOWN
              │N
 isBusOff == TRUE? ──────Y──→ Status=BUSOFF, return SAFE_STOP
              │N
 rxMsgCount != LastRxCount? ──Y──→ LastRxCount=rxMsgCount, LastRxTimeMs=currentTimeMs
              │N
 silenceMs = currentTimeMs - LastRxTimeMs
 silenceMs >= 200? ──────────Y──→ Status=SILENCE, return SAFE_STOP
              │N
 errorWarning == TRUE? ──────N──→ ErrorWarnActive = FALSE
              │Y
 ErrorWarnActive == FALSE? ──Y──→ ErrorWarnActive=TRUE, ErrorWarnStartMs=currentTimeMs
              │N
 errorWarnMs = currentTimeMs - ErrorWarnStartMs
 errorWarnMs >= 500? ────────Y──→ Status=ERROR_WARNING, return SAFE_STOP
              │N
 Status = OK, return OK
```

```
┌──────────────────────────────┐
│ Swc_CanMonitor_Recovery(     │
│   currentTimeMs)             │
└─────────────┬────────────────┘
              │
 Initialized != TRUE? ──Y──→ return E_NOT_OK
              │N
 Status == SHUTDOWN? ──Y──→ return E_NOT_OK
              │N
 windowElapsed = currentTimeMs - RecoveryWindowStartMs
 windowElapsed >= 10000? ──Y──→ RecoveryAttempts=0, RecoveryWindowStartMs=currentTimeMs
              │N
 RecoveryAttempts == 0? ──Y──→ RecoveryWindowStartMs = currentTimeMs
              │N
 RecoveryAttempts++
 RecoveryAttempts > 3? ──Y──→ Status=SHUTDOWN, return E_NOT_OK
              │N
 Status=OK, ErrorWarnActive=FALSE, return E_OK
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_CanMonitor_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `cycles` | Check 调用次数 | `1`（单次）、`3`（静默三拍）、`5`/`6`（错误警告 500ms 五/六拍） | When — 执行控制 |
| `isBusOff` | Check 的 bus-off 输入 | `false`（总线正常）、`true`（总线关闭） | When — 故障注入 |
| `rxMsgCount` | Check 的 RX 消息计数 | `0`（Init 后初值）、`5`（有消息基线）、`rxInc=1` 时逐拍递增 | When — 状态注入 |
| `rxInc` | 每拍是否递增 rxMsgCount | `false`（静默：计数不变）、`true`（消息持续到达） | When — 状态注入 |
| `errorWarning` | Check 的错误警告输入 | `false`（无警告）、`true`（错误警告） | When — 故障注入 |
| `timeStartMs` | 第一拍的 `currentTimeMs` | `0`、`1000`、`15000`（恢复窗口过期边界） | When — 时间注入 |
| `timeStepMs` | 每拍时间增量 | `100`（静默 200ms 边界）、`500`（错误警告边界） | When — 时间注入 |
| `recovery` | 是否调用 `Swc_CanMonitor_Recovery()` | `false`、`true` | When — 执行控制 |
| `recoveryTimeMs` | Recovery 的 `currentTimeMs` | `0`/`1000`/`2000`/`3000`（窗口内）、`15000`（窗口过期） | When — 时间注入 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `status` | `Swc_CanMonitor_GetStatus()` | OK(0)/BUSOFF(1)/SILENCE(2)/ERROR_WARNING(3)/SAFE_STOP(4)/SHUTDOWN(5) |
| `checkResult` | 最后一次 Check 返回值 | OK(0)/SAFE_STOP(4)/SHUTDOWN(5) |
| `recoveryResult` | 最后一次 Recovery 返回值 | E_OK(0)/E_NOT_OK(1)，未调用为 -1 |
| `initialized` | `CanMon_Initialized`（getter） | 0/1 |
| `lastRxCount` / `lastRxTimeMs` | 静默检测内部状态（getter） | 新消息后更新 |
| `errorWarnActive` / `errorWarnStartMs` | 错误警告追踪状态（getter） | 激活/清除 |
| `recoveryAttempts` / `recoveryWindowStartMs` | 恢复追踪状态（getter） | 窗口内计数/窗口起始 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测函数分组：
> - **规则: 总线丢失检测 — Swc_CanMonitor_Check（bus-off）**：初始化默认值 /
>   未初始化守卫 / 总线关闭立即 SAFE_STOP，共 3 场景。
> - **规则: 200ms 静默检测 — Swc_CanMonitor_Check（silence）**：未达阈值 /
>   恰达阈值 / 消息重置定时器，共 3 场景。
> - **规则: 错误警告检测 — Swc_CanMonitor_Check（error warning）**：未达 500ms /
>   恰达 500ms / 清除后重新计时，共 3 场景。
> - **规则: 总线恢复 — Swc_CanMonitor_Recovery**：未初始化守卫 / 恢复成功 /
>   第 4 次失败 SHUTDOWN / 窗口过期重置 / SHUTDOWN 终态短路，共 5 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/canmonitor/setup` 存储）：设置前置总线
>   监控状态（如 bus-off 基线）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/canmonitor` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`cycles=1`、
> `isBusOff=false`、`rxMsgCount=0`、`rxInc=false`、`errorWarning=false`、
> `timeStartMs=0`、`timeStepMs=100`、`recovery=false`、`recoveryTimeMs=0`、
> `skipInit=false`）。

### 规则: 总线丢失检测 — Swc_CanMonitor_Check（bus-off）

| 用例 | 阶段序列 | 期望 checkResult | 期望 status |
|---|---|---|---|
| init_defaults_ok | P0: cycles=1 | OK(0) | OK(0) |
| uninitialized_check_returns_ok | P0: cycles=1, skipInit=true | OK(0)（守卫返回） | OK(0)（静态初值） |
| busoff_immediate_safe_stop | P0: cycles=1, isBusOff=true | SAFE_STOP(4) | BUSOFF(1) |

### 规则: 200ms 静默检测 — Swc_CanMonitor_Check（silence）

| 用例 | 阶段序列 | 期望 checkResult | 期望 status |
|---|---|---|---|
| silence_under_200ms_ok | P0: cycles=2, rxMsgCount=5, timeStartMs=0, timeStepMs=100 | OK(0)（100ms 静默） | OK(0) |
| silence_at_200ms_triggers | P0: cycles=3, rxMsgCount=5, timeStartMs=0, timeStepMs=100 | SAFE_STOP(4)（第 3 拍 200ms） | SILENCE(2) |
| message_arrival_resets_silence | P0: cycles=5, rxMsgCount=5, rxInc=true, timeStartMs=0, timeStepMs=100 | OK(0)（每拍新消息） | OK(0) |

> 说明：`silence_under_200ms_ok` 中第一拍（t=0）计数 `5 != Init 后 LastRxCount=0`
> 视为新消息，建立 `LastRxTimeMs=0`；第二拍（t=100）计数仍为 5，静默 100ms < 200，
> 不触发。`silence_at_200ms_triggers` 增加第三拍（t=200），静默恰达 200ms 边界
> 触发 SAFE_STOP。

### 规则: 错误警告检测 — Swc_CanMonitor_Check（error warning）

| 用例 | 阶段序列 | 期望 checkResult | 期望 status |
|---|---|---|---|
| error_warning_under_500ms_ok | P0: cycles=5, rxInc=true, errorWarning=true, timeStartMs=0, timeStepMs=100 | OK(0)（400ms 持续） | OK(0) |
| error_warning_at_500ms_triggers | P0: cycles=6, rxInc=true, errorWarning=true, timeStartMs=0, timeStepMs=100 | SAFE_STOP(4)（第 6 拍 500ms） | ERROR_WARNING(3) |
| error_warning_cleared_restarts | 前置: cycles=1, rxInc=true, errorWarning=true, timeStartMs=0（激活，start=0）; P0: cycles=1, rxInc=true, errorWarning=false, timeStartMs=300（清除）; P1: cycles=1, rxInc=true, errorWarning=true, timeStartMs=400（重新激活，start=400）; P2: cycles=1, rxInc=true, errorWarning=true, timeStartMs=899 | OK(0)（899-400=499 < 500） | OK(0) |

> 说明：错误警告用例均配合 `rxInc=true`（每拍新消息到达），避免静默检测抢先
> 触发。`error_warning_cleared_restarts` 验证清除后 `ErrorWarnStartMs` 重新起算：
> 若未重置，899-0=899 早已 ≥500 触发；实际 899-400=499 不触发，证明重新计时。

### 规则: 总线恢复 — Swc_CanMonitor_Recovery

| 用例 | 阶段序列 | 期望 recoveryResult | 期望 status / attempts |
|---|---|---|---|
| recovery_uninitialized_not_ok | P0: recovery=true, recoveryTimeMs=1000, skipInit=true | E_NOT_OK(1) | OK(0) |
| recovery_success_after_busoff | 前置: cycles=1, isBusOff=true, timeStartMs=1000（BUSOFF）; P0: recovery=true, recoveryTimeMs=1000 | E_OK(0) | OK(0) |
| recovery_3_failures_shutdown | 前置: cycles=1, isBusOff=true, timeStartMs=1000; P0: recovery=true, recoveryTimeMs=1000（attempt 1 → E_OK）; P1: cycles=1, isBusOff=true, timeStartMs=2000; P2: recovery=true, recoveryTimeMs=2000（attempt 2 → E_OK）; P3: cycles=1, isBusOff=true, timeStartMs=3000; P4: recovery=true, recoveryTimeMs=3000（attempt 3 → E_OK）; P5: cycles=1, isBusOff=true, timeStartMs=4000; P6: recovery=true, recoveryTimeMs=4000（attempt 4 → E_NOT_OK） | E_NOT_OK(1) | SHUTDOWN(5), attempts=4 |
| recovery_window_expired_resets | P0: recovery=true, recoveryTimeMs=0（attempt 1 → E_OK）; P1: recovery=true, recoveryTimeMs=1000（attempt 2）; P2: recovery=true, recoveryTimeMs=2000（attempt 3）; P3: recovery=true, recoveryTimeMs=15000（窗口过期 → 重置 → attempt 1） | E_OK(0) | OK(0), attempts=1 |
| shutdown_terminal_check_and_recovery | 前置: P0-P6 同 recovery_3_failures_shutdown（进入 SHUTDOWN）; P7: cycles=1, isBusOff=false, timeStartMs=4000; P8: recovery=true, recoveryTimeMs=4000 | checkResult=SHUTDOWN(5), recoveryResult=E_NOT_OK(1) | SHUTDOWN(5) |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_defaults_ok` | 初始化后默认状态为 OK |
> | `uninitialized_check_returns_ok` | 未初始化时 Check 返回 OK 不动作 |
> | `busoff_immediate_safe_stop` | 总线关闭立即触发 SAFE_STOP |
> | `silence_under_200ms_ok` | 静默不足 200ms 不触发 |
> | `silence_at_200ms_triggers` | 静默恰达 200ms 触发 SAFE_STOP |
> | `message_arrival_resets_silence` | 消息持续到达重置静默定时器 |
> | `error_warning_under_500ms_ok` | 错误警告持续不足 500ms 不触发 |
> | `error_warning_at_500ms_triggers` | 错误警告持续 500ms 触发 SAFE_STOP |
> | `error_warning_cleared_restarts` | 错误警告清除后重新计时 |
> | `recovery_uninitialized_not_ok` | 未初始化时恢复返回 E_NOT_OK |
> | `recovery_success_after_busoff` | 总线关闭后恢复成功复位 OK |
> | `recovery_3_failures_shutdown` | 3 次恢复失败后第 4 次触发 SHUTDOWN |
> | `recovery_window_expired_resets` | 恢复窗口过期后计数器重置 |
> | `shutdown_terminal_check_and_recovery` | SHUTDOWN 终态下 Check/Recovery 短路 |

## 代码路径覆盖

- `Swc_CanMonitor_Init` 全部可执行行 ✅
- `Swc_CanMonitor_Check` 全部可执行行 ✅
  - 未初始化守卫（`CanMon_Initialized != TRUE` → return OK）✅
  - SHUTDOWN 终态短路 ✅
  - bus-off 立即 SAFE_STOP ✅
  - 新消息重置静默定时器 / 静默 ≥200ms 触发 ✅（两侧）
  - 错误警告激活追踪 / 持续 ≥500ms 触发 / 清除重置 ✅（三侧）
  - 健康返回 OK ✅
- `Swc_CanMonitor_Recovery` 全部可执行行 ✅
  - 未初始化守卫 ✅
  - SHUTDOWN 终态短路 ✅
  - 窗口过期重置计数器 ✅
  - 窗口内计数 / 首拍设窗口 ✅
  - 第 4 次尝试 SHUTDOWN ✅
  - 成功复位 OK + ErrorWarnActive=FALSE ✅
- `Swc_CanMonitor_GetStatus` ✅（每场景 harness 输出读取）
- UNIT_TEST 观测 getters（仅测试编译）✅ 由 harness 输出读取，全部命中

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_CanMonitor.c.gcov.html` 实测（2026-08-16 全量套件 312 场景运行后，
含本 feature 14 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（118 / 118 行） |
| **分支覆盖** | **100%**（26 / 26 分支） |
| **函数覆盖** | **100%**（11 / 11 函数） |

覆盖到的函数：`Swc_CanMonitor_Init`、`Swc_CanMonitor_Check`、
`Swc_CanMonitor_Recovery`、`Swc_CanMonitor_GetStatus`（生产 API），以及 7 个
`#ifdef UNIT_TEST` 观测 getter（`GetInitialized`、`GetLastRxCount`、
`GetLastRxTimeMs`、`GetErrorWarnStartMs`、`GetErrorWarnActive`、
`GetRecoveryAttempts`、`GetRecoveryWindowStartMs`）。

> 下表「实测命中」为完整套件（312 场景）运行后的累积值（本容器运行期间多次
> 执行 feature 的累积：29 次 harness 调用，其中 4 次因 `skipInit` 跳过 Init）；
> 每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。生产固件
> 编译不定义 `UNIT_TEST`，getter 相关行不计入交付固件的有效代码。

---

## 行覆盖分析（100%，118/118）

行覆盖反映**每一行是否被执行**。118 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_CanMonitor_Init（L48-58）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L48 | 函数入口 `{` | 全部已初始化场景（每 harness 运行先 Init） | 25 |
| L50 | `CanMon_Status = CANMON_STATUS_OK` | 全部已初始化场景（默认 OK 由 `init_defaults_ok` 断言） | 25 |
| L51-52 | `CanMon_LastRxCount = 0`、`CanMon_LastRxTimeMs = 0` | 全部已初始化场景（静默基线） | 25 |
| L53-54 | `CanMon_ErrorWarnStartMs = 0`、`CanMon_ErrorWarnActive = FALSE` | 全部已初始化场景（错误警告基线） | 25 |
| L55-56 | `CanMon_RecoveryAttempts = 0`、`CanMon_RecoveryWindowStartMs = 0` | 全部已初始化场景（恢复基线） | 25 |
| L57 | `CanMon_Initialized = TRUE` | 全部已初始化场景（`initialized=1` 断言） | 25 |
| L58 | 函数结束 `}` | 全部已初始化场景 | 25 |

#### Swc_CanMonitor_Check（L67-138）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L67-70 | 函数入口签名 | 全部场景（每 Check 调用进入） | 110 |
| L71-73 | 局部变量声明 | 全部场景 | 110 |
| L75-78 | `if (CanMon_Initialized != TRUE) return OK` | true 侧：`uninitialized_check_returns_ok`；false 侧：全部已初始化场景 | 110 |
| L81-84 | `if (Status == SHUTDOWN) return SHUTDOWN` | true 侧：`shutdown_terminal_check_and_recovery`；false 侧：其余场景 | 106 |
| L87-91 | `if (isBusOff == TRUE) { Status=BUSOFF; return SAFE_STOP }` | `busoff_immediate_safe_stop` + `recovery_*` Given 中的 bus-off 前置（status=BUSOFF 断言） | 102 |
| L94-99 | `if (rxMsgCount != LastRxCount)` 新消息 → 重置静默定时器 | true 侧：`silence_under_200ms_ok` 首拍、`message_arrival_resets_silence`、`error_warning_*`（rxInc 逐拍新消息） | 82 |
| L100-104 | `else` 静默分支：`silenceMs = currentTimeMs - LastRxTimeMs; if (>= 200)` | false（未触发）：`init_defaults_ok`、`silence_under_200ms_ok` 第二拍、`error_warning_*` 首拍；true（触发）：`silence_at_200ms_triggers` | 41 |
| L105-108 | `Status=SILENCE; return SAFE_STOP` | `silence_at_200ms_triggers`（status=SILENCE 断言） | 2 |
| L109 | `}` 静默分支结束 | 静默未触发的全部场景 | 41 |
| L112 | `if (errorWarning == TRUE)` | true 侧：`error_warning_*` 三场景；false 侧：其余场景 | 80 |
| L113-119 | `if (ErrorWarnActive == FALSE)` 开始追踪（active=TRUE, start=currentTimeMs） | `error_warning_under_500ms_ok` 首拍、`error_warning_at_500ms_triggers` 首拍、`error_warning_cleared_restarts` 重新激活 | 31 |
| L120-123 | `else`：`errorWarnMs = currentTimeMs - ErrorWarnStartMs; if (>= 500)` | true（触发）：`error_warning_at_500ms_triggers` 第 6 拍；false：`error_warning_under_500ms_ok`、`error_warning_cleared_restarts` 末拍 | 21 |
| L124-127 | `Status=ERROR_WARNING; return SAFE_STOP` | `error_warning_at_500ms_triggers`（status=ERROR_WARNING 断言） | 2 |
| L128 | `}` 错误警告已激活分支结束 | 错误警告持续但未达 500ms 的场景 | 21 |
| L129 | `}` `errorWarning==TRUE` 分支结束 | 错误警告相关场景 | 31 |
| L130-134 | `else`：`ErrorWarnActive = FALSE`（清除追踪） | `error_warning_cleared_restarts`（300ms 清除）、`init_defaults_ok`、静默/总线场景 | 49 |
| L136-137 | `CanMon_Status = OK; return OK` | 健康/未触发场景（status=OK、checkResult=OK 断言） | 78 |
| L138 | 函数结束 `}` | 全部未提前返回的场景 | 80 |

#### Swc_CanMonitor_Recovery（L147-190）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L147-150 | 函数入口 + 局部变量 | `recovery_*` 全部场景 | 30 |
| L151-154 | `if (CanMon_Initialized != TRUE) return E_NOT_OK` | `recovery_uninitialized_not_ok`（recoveryResult=E_NOT_OK 断言） | 30 |
| L157-160 | `if (Status == SHUTDOWN) return E_NOT_OK` | `shutdown_terminal_check_and_recovery`（终态恢复短路） | 28 |
| L163-168 | `windowElapsed = currentTimeMs - WindowStart; if (>= 10000)` 窗口过期 → 计数器清零 | `recovery_window_expired_resets` 第 4 次（t=15000，窗口重置，attempts 复位 1） | 26 |
| L171-174 | `if (RecoveryAttempts == 0)` 首拍建立窗口 | `recovery_success_after_busoff`、`recovery_3_failures_shutdown` 第 1 次、`recovery_window_expired_resets` 第 1 次及重置后 | 26 |
| L176 | `CanMon_RecoveryAttempts++` | 全部 Recovery 调用 | 26 |
| L178-183 | `if (RecoveryAttempts > 3)` → `Status=SHUTDOWN; return E_NOT_OK` | `recovery_3_failures_shutdown` 第 4 次 + `shutdown_terminal_check_and_recovery`（status=SHUTDOWN、recoveryResult=E_NOT_OK 断言） | 26 |
| L186-187 | `Status = OK; ErrorWarnActive = FALSE` | `recovery_success_after_busoff`、`recovery_3_failures_shutdown` 第 1-3 次 | 22 |
| L189 | `return E_OK` | 恢复成功的全部调用（recoveryResult=E_OK 断言） | 22 |
| L190 | 函数结束 `}` | 全部 Recovery 调用 | 26 |

#### Swc_CanMonitor_GetStatus（L196-199）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L196-199 | 返回 `CanMon_Status` | 全部场景（harness 输出 JSON 的 status 字段逐次调用） | 29 |

#### UNIT_TEST 观测 getters（L209-242，仅测试编译）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L209-242 | 7 个 getter 返回静态 CAN 监控状态 | 全部场景（harness 输出 JSON 逐次调用） | 29（各 getter） |

> 常量/静态声明（L28-42 静态变量）为非执行行，不计入行覆盖。genhtml 的行统计
> 另含 13 个「带分支计数的条件行」：`L75`（`Initialized != TRUE`）、`L81`
> （`Status == SHUTDOWN`）、`L87`（`isBusOff == TRUE`）、`L94`
> （`rxMsgCount != LastRxCount`）、`L104`（`silenceMs >= 200`）、`L112`
> （`errorWarning == TRUE`）、`L114`（`ErrorWarnActive == FALSE`）、`L123`
> （`errorWarnMs >= 500`）、`L151`（`Initialized != TRUE`）、`L157`
> （`Status == SHUTDOWN`）、`L164`（`windowElapsed >= 10000`）、`L171`
> （`RecoveryAttempts == 0`）、`L178`（`RecoveryAttempts > 3`）。全部由上述
> 场景命中两侧，故 118/118 行全部覆盖。

---

## 分支覆盖分析（100%，26/26）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `CanMon_Initialized != TRUE` | L75 | ✅ 两侧 | `uninitialized_check_returns_ok`（true）/ 全部已初始化场景（false） |
| `CanMon_Status == SHUTDOWN` | L81 | ✅ 两侧 | `shutdown_terminal_check_and_recovery`（true）/ 其余场景（false） |
| `isBusOff == TRUE` | L87 | ✅ 两侧 | `busoff_immediate_safe_stop` + recovery 前置（true）/ 其余场景（false） |
| `rxMsgCount != LastRxCount` | L94 | ✅ 两侧 | 新消息场景（true）/ 静默场景（false） |
| `silenceMs >= 200` | L104 | ✅ 两侧 | `silence_at_200ms_triggers`（true）/ `silence_under_200ms_ok`、`init_defaults_ok`（false） |
| `errorWarning == TRUE` | L112 | ✅ 两侧 | `error_warning_*`（true）/ 其余场景（false） |
| `ErrorWarnActive == FALSE` | L114 | ✅ 两侧 | 首拍激活/清除后重激活（true）/ 持续激活（false） |
| `errorWarnMs >= 500` | L123 | ✅ 两侧 | `error_warning_at_500ms_triggers`（true）/ 未达阈值（false） |
| `CanMon_Initialized != TRUE`（Recovery） | L151 | ✅ 两侧 | `recovery_uninitialized_not_ok`（true）/ 其余（false） |
| `CanMon_Status == SHUTDOWN`（Recovery） | L157 | ✅ 两侧 | `shutdown_terminal_check_and_recovery`（true）/ 其余（false） |
| `windowElapsed >= 10000` | L164 | ✅ 两侧 | `recovery_window_expired_resets`（true）/ 窗口内（false） |
| `RecoveryAttempts == 0` | L171 | ✅ 两侧 | 每次窗口首拍（true）/ 后续拍（false） |
| `RecoveryAttempts > 3` | L178 | ✅ 两侧 | 第 4 次（true）/ 第 1-3 次（false） |

> 全部 13 个分支点两侧均已覆盖，无无法覆盖的分支。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（118/118） | 0 行 | — |
| 分支 | 100%（26/26） | 0 个 | — |
| 函数 | 100%（11/11） | — | — |
