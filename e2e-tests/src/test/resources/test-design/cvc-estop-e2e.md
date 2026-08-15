# CVC 紧急停止 (Swc_EStop) E2E 测试设计

## 被测功能

**CVC ASW 紧急停止 SWC — 按钮消抖 → 永久锁存 → CAN 0x001 广播 → DTC 上报**

覆盖链路：

```text
IoHwAb E-stop 按钮（GPIO 状态）
  → Swc_EStop_MainFunction（10ms 周期）
  → 1 周期消抖（ESTOP_DEBOUNCE_THRESHOLD=1）
  → 首次激活：永久锁存 active=TRUE（上电前不复位）
  → Dem_ReportErrorStatus(CVC_DTC_ESTOP_ACTIVATED, FAILED)  DTC 上报
  → Rte_Write(CVC_SIG_ESTOP_ACTIVE)  RTE 信号写
  → Swc_CvcCom_TransmitSchedule → Com 0x001 EStop_Broadcast（Active/Source）
```

与既有 ASW E2E（`Swc_Pedal`、`Swc_VehicleState`）一致，本测试**不**走仪表盘/系统 E2E
运行器，而是通过测试专用 API 在原生测试框架内执行真实的 `Swc_EStop.c` + `Swc_CvcCom.c`
生产代码。E-stop 失效保护：`IoHwAb_ReadEStop` 读取失败时按激活处理（fail-safe）。

## 被测代码流程图

```
                       ┌──────────────────┐
                       │ Swc_EStop_Init    │
                       │ (active=FALSE,    │
                       │  debounce=0,      │
                       │  initialized=TRUE)│
                       └────────┬─────────┘
                                │
                       ┌────────▼─────────┐
                       │  MainFunction     │
                       │  (每 10ms)        │
                       └────────┬─────────┘
                                │
         Step1: initialized==FALSE？ ──Y──→ return（未初始化空转）
                                │N
         Step2: IoHwAb_ReadEStop 读按钮
                ──读取失败(E_NOT_OK)──→ pin_state=STD_HIGH（fail-safe）
                                │
         Step3: active==FALSE（未锁存）？
                ├─ pin_state==STD_HIGH → debounce_counter++
                │    └─ counter>=1（阈值）→ active=TRUE
                │         · Dem_ReportErrorStatus(DTC ESTOP, FAILED)
                │         · Rte_Write(CVC_SIG_ESTOP_ACTIVE, TRUE)
                │    └─ counter<1（阈值=1 时不可达）
                └─ pin_state==STD_LOW → debounce_counter=0
                                │
         Step4: active==TRUE（锁存）？
                ├─ Rte_Write(CVC_SIG_ESTOP_ACTIVE, 1)（每周期刷新广播）
                └─ Dem_ReportErrorStatus(DTC, FAILED)（每周期重报，供 DEM 3 周期饱和）
                                │
         Step5: Swc_CvcCom_TransmitSchedule
                → Rte_Read(CVC_SIG_ESTOP_ACTIVE) → Com 0x001
                  EStop_Broadcast_Active=1、EStop_Broadcast_Source=1
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `pin` | E-stop 按钮 GPIO 状态 | `0`（LOW/释放）、`1`（HIGH/按下） | When — 刺激注入 |
| `readFail` | `IoHwAb_ReadEStop` 读取结果 | `false`（E_OK）、`true`（E_NOT_OK，fail-safe） | When — 故障注入 |
| `cycles` | 10ms 循环次数 | `1`（= 消抖阈值，边界）、`3`、`5` | When — 执行控制 |
| `skipInit` | 是否跳过 `Swc_EStop_Init()` | `false`（先 Init）、`true`（未初始化，防御守卫） | When — 执行控制 |

> 平台常量：`ESTOP_DEBOUNCE_THRESHOLD = 1`（1 周期消抖）。
> 由于阈值恒为 1，任何一次 HIGH 采样都会立即满足 `counter >= 1`，
> 因此「counter < 1」这一侧为结构不可达（见「无法覆盖的分支」）。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `isActive` | `Swc_EStop_IsActive()` 锁存状态 | `0`、`1` |
| `rteEstopActive` | RTE 信号 `CVC_SIG_ESTOP_ACTIVE`（=68） | `0`、`1` |
| `demEventId` | 最后上报的 DTC 事件 ID | `7`（CVC_DTC_ESTOP_ACTIVATED） |
| `demEventStatus` | 最后上报的事件状态 | `1`（DEM_EVENT_STATUS_FAILED） |
| `demReportCount` | `Dem_ReportErrorStatus` 调用次数 | 见各用例 |
| `rteWriteCount` | `CVC_SIG_ESTOP_ACTIVE` 的 Rte_Write 次数 | 见各用例 |
| `broadcastActive` | Com 信号 `CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE`（0x001） | `0`、`1` |
| `broadcastSource` | Com 信号 `CVC_COM_SIG_ESTOP_BROADCAST_SOURCE` | `1`（CVC） |

## 测试用例

> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/estop/setup` 存储）：设置前置 E-stop 状态
>   （如按钮已按下并锁存）。无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/estop` body）：触发被测动作，
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值（`pin=0`、`readFail=false`、
> `skipInit=false`）。

| 用例 | 阶段序列 | 期望 isActive | 期望 rteEstopActive | 期望 demEventId/Count | 期望 broadcastActive |
|---|---|---|---|---|---|
| uninitialized_noop | P0: skipInit=true,pin=1,cycles=3 | 0 | 0 | —/0 | 0 |
| released_stays_inactive | P0: pin=0,cycles=3 | 0 | 0 | —/0 | 0 |
| pressed_activates_latch | P0: pin=1,cycles=1 | 1 | 1 | 7/2 | 1 |
| read_failure_failsafe | P0: pin=0,readFail=true,cycles=1 | 1 | 1 | 7/2 | 1 |
| latch_holds_after_release | 前置: pin=1,cycles=1; P0: pin=0,cycles=3 | 1 | 1 | 7 | 1 |
| cyclic_rebroadcast_while_latched | P0: pin=1,cycles=5 | 1 | 1 | 7/6 | 1 |

> 计数说明：激活周期（`pressed_activates_latch`）内 MainFunction 先走「首次激活」块
> （1 次 Dem + 1 次 Rte_Write），随后同一周期进入「锁存循环」块（再 1 次 Dem + 1 次
> Rte_Write），故 `demReportCount=2`、`rteWriteCount=2`。此后每锁存周期各 +1，
> 5 周期时 `demReportCount=6`、`rteWriteCount=6`。

## 代码路径覆盖

- 3 个函数的全部可执行行：`Swc_EStop_Init`、`Swc_EStop_MainFunction`、`Swc_EStop_IsActive` ✅
- 消抖激活路径（HIGH → counter++ → `>=1` → active=TRUE）✅
- 未激活路径（LOW → 清 counter）✅
- fail-safe 路径（读取失败 → pin=STD_HIGH）✅
- 锁存循环路径（激活后每周期 Rte_Write + Dem 重报）✅
- 未初始化守卫（`initialized==FALSE` → return）✅
- CvcCom 0x001 广播桥（`estop_val != 0` → Active=1/Source=1）✅

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_EStop.c.gcov.html` 实测：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（38 / 38 行） |
| **分支覆盖** | **91.7%**（11 / 12 分支） |
| **函数覆盖** | **100%**（3 / 3） |

覆盖到的函数：`Swc_EStop_Init`、`Swc_EStop_MainFunction`、`Swc_EStop_IsActive`。

> 下表「实测命中」为完整套件（6 个 E-stop 场景 + 既有踏板/状态机场景）运行后的累积值，
> 供参考；每次运行因容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（100%，38/38）

行覆盖反映**每一行是否被执行**。38 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_EStop_Init（L53-58）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L54 | `{`（函数体） | 全部场景（每个场景 harness 运行先 Init） | 11 |
| L55 | `active = FALSE` | 全部场景 | 11 |
| L56 | `debounce_counter = 0u` | 全部场景 | 11 |
| L57 | `initialized = TRUE` | 全部场景 | 11 |
| L58 | `}` | 全部场景 | 11 |

#### Swc_EStop_MainFunction（L72-120）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L73-75 | 局部变量（`pin_state=STD_LOW`、`ret`） | 全部场景（每周期进入） | 41 |
| L77 | `if (initialized == FALSE)`（防御守卫） | `uninitialized_noop`（true 侧，9 次）+ 其余场景（false 侧，32 次） | 41 |
| L78-79 | `return`（未初始化空转） | `uninitialized_noop`（skipInit=true，3 周期） | 9 |
| L82 | `ret = IoHwAb_ReadEStop(&pin_state)` | 全部已初始化场景 | 32 |
| L84 | `if (ret != E_OK)`（fail-safe 分支） | `read_failure_failsafe`（true 侧，2 次）+ 其余场景（false 侧，30 次） | 32 |
| L86-87 | `pin_state = STD_HIGH`（读取失败按激活处理） | `read_failure_failsafe` | 2 |
| L90 | `if (active == FALSE)`（未锁存分支） | `released_stays_inactive`、`pressed_activates_latch`、`read_failure_failsafe`、`latch_holds_after_release` 前置段（true 侧，15 次）+ 锁存后续周期（false 侧，17 次） | 32 |
| L91 | `if (pin_state == STD_HIGH)`（按钮按下判定） | `pressed_activates_latch`、`read_failure_failsafe`（true 侧，9 次）+ `released_stays_inactive`（false 侧，6 次） | 15 |
| L92 | `debounce_counter++` | `pressed_activates_latch`、`read_failure_failsafe` | 9 |
| L94 | `if (debounce_counter >= ESTOP_DEBOUNCE_THRESHOLD)`（消抖阈值） | `pressed_activates_latch`、`read_failure_failsafe`（true 侧，9 次） | 9 |
| L95 | `active = TRUE`（永久锁存） | `pressed_activates_latch`、`read_failure_failsafe` | 9 |
| L98-99 | `Dem_ReportErrorStatus(CVC_DTC_ESTOP_ACTIVATED, FAILED)`（DTC 上报） | `pressed_activates_latch`、`read_failure_failsafe`（首次激活） | 9 |
| L102 | `Rte_Write(CVC_SIG_ESTOP_ACTIVE, TRUE)`（RTE 信号写） | `pressed_activates_latch`、`read_failure_failsafe`（首次激活） | 9 |
| L103 | `}`（阈值达成块结束） | 同上 | 9 |
| L104 | `} else {`（按钮 LOW 且未锁存） | `released_stays_inactive`、`latch_holds_after_release` 未锁存周期 | 9 |
| L106 | `debounce_counter = 0u`（清消抖计数） | `released_stays_inactive`（pin=0 保持未激活） | 6 |
| L107-108 | `}`（消抖/未锁存块结束） | 同上 | 15 |
| L115 | `if (active == TRUE)`（锁存循环分支） | `pressed_activates_latch`、`read_failure_failsafe`、`latch_holds_after_release`、`cyclic_rebroadcast_while_latched`（true 侧，26 次）+ 未激活场景（false 侧，6 次） | 32 |
| L116 | `Rte_Write(CVC_SIG_ESTOP_ACTIVE, 1u)`（每周期刷新广播） | 上述 4 个激活场景（含激活当周期） | 26 |
| L117-118 | `Dem_ReportErrorStatus(CVC_DTC_ESTOP_ACTIVATED, FAILED)`（每周期重报） | 上述 4 个激活场景 | 26 |
| L119-120 | `}`（锁存循环块 / MainFunction 结束） | 全部场景 | 32 |

#### Swc_EStop_IsActive（L126-129）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L127 | `{`（函数体） | 全部场景（harness 输出 `isActive` 时读取） | 14 |
| L128 | `return active` | 全部场景 | 14 |
| L129 | `}` | 全部场景 | 14 |

### 相关链路补充：Swc_CvcCom 0x001 广播桥

`Swc_EStop` 自身不直接发 CAN（Phase 2 分层），广播由 `Swc_CvcCom_TransmitSchedule`
桥接。E-stop 场景使该链路从「仅广播 0（未激活）」扩展为「同时覆盖广播 1（激活）」：

| 位置 | 代码 | 覆盖状态 |
|---|---|---|
| L175 | `Rte_Read(CVC_SIG_ESTOP_ACTIVE, &estop_val)` | ✅ |
| L176 | `estop_active = (estop_val != 0u) ? 1u : 0u` | ✅ 两侧分支（`estop_val!=0` true 侧 9 次 — 由 E-stop 场景覆盖；false 侧 1683 次） |
| L177 | `Com_SendSignal(CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE, ...)` | ✅ |
| L178 | `Com_SendSignal(CVC_COM_SIG_ESTOP_BROADCAST_SOURCE, ...)` | ✅ |

---

## 分支覆盖分析（91.7%，11/12）

行覆盖率不能反映 `&&`/`||` 短路、多值枚举等**同一行内多个判断侧**的覆盖情况。
`Swc_EStop.c.gcov.html` 实测 12 个分支、11 个覆盖，唯一未覆盖分支为结构不可达
（见下）。

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `initialized == FALSE` 两侧 | L77 | ✅ +/+, 9/32 | `uninitialized_noop` 覆盖 true 侧，其余场景覆盖 false 侧 |
| `ret != E_OK` 两侧 | L84 | ✅ +/+, 2/30 | `read_failure_failsafe` 覆盖 true 侧，其余覆盖 false 侧 |
| `active == FALSE` 两侧 | L90 | ✅ +/+, 15/17 | 未锁存/锁存周期均有覆盖 |
| `pin_state == STD_HIGH` 两侧 | L91 | ✅ +/+, 9/6 | 按下/释放均有覆盖 |
| `debounce_counter >= 1` 两侧 | L94 | ⚠️ +/-, 9/0 | **false 侧不可达**（见下） |
| `active == TRUE` 两侧 | L115 | ✅ +/+, 26/6 | 激活/未激活均有覆盖 |

### 无法覆盖的分支（1 个，结构不可达）

| 分支 | 位置 | 无法覆盖的理由 |
|---|---|---|
| `debounce_counter >= ESTOP_DEBOUNCE_THRESHOLD` 的 false 侧 | L94 | 消抖阈值恒为 1（`#define ESTOP_DEBOUNCE_THRESHOLD 1u`），且比较前先 `debounce_counter++`。任何一次 HIGH 采样都会使计数器从 0 变为 1，`1 >= 1` 恒为真，因此「计数器 < 阈值」这一侧在逻辑上不可达。只有当配置阈值 > 1（需要多周期消抖）时该侧才可能出现，当前单周期消抖设计下为**结构不可达**，非测试缺口。 |

> 该分支侧已由单元测试 `test_Swc_EStop_asilb.c` 中的
> `test_EStop_Debounce_exact_threshold_boundary` 从设计层面确认阈值边界行为。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（38/38） | 0 行 | — |
| 分支 | 91.7%（11/12） | 1 个 | L94 `counter>=1` false 侧：阈值=1 结构不可达 |
| 函数 | 100%（3/3） | — | — |
