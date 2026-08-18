# SC 运行时状态机 (sc_state) E2E 测试设计

## 被测功能

**SC 权威运行时状态机（GAP-SC-006 / SWR-SC-025，ASIL D）**

单一 `static uint8 sc_state` 保存权威运行状态，三个公开 API：

- `SC_State_Init()`：置状态为 `SC_STATE_INIT`（0）。
- `SC_State_Get()`：返回当前状态。
- `SC_State_Transition(new_state)`：按合法边迁移，非法迁移拒绝
  （状态不变、返回 FALSE，fail-closed）。

状态与合法迁移（`Sc_Hw_Cfg.h`）：

```text
      ┌──────────────────────────────────────────────┐
      │ SC_STATE_INIT (0)                             │
      │   ──(new=MONITORING)──────────────┐           │
      └───────────────────────────────────│───────────┘
                                          │
                                          ▼
      ┌──────────────── SC_STATE_MONITORING (1) ──────┐
      │   ──(new=FAULT)──▶ FAULT (2)                  │
      │   ──(new=KILL)──▶ KILL  (3, 终态)             │
      └────────────────────────────────────────────────┘
      SC_STATE_FAULT (2)：
         ──(new=KILL)──▶ KILL (3)
      SC_STATE_KILL (3)：终态，无任何迁出边（仅断电复位）
      默认（当前状态未知/非法）：强制置 KILL，返回 FALSE（fail-closed）
```

覆盖链路：

```text
测试 API 注入（op / newState / skipInit / setRawState）
  → SC_State_Init()：
       · sc_state = SC_STATE_INIT
  → SC_State_Get()：
       · 返回 sc_state
  → SC_State_Transition(new_state)：
       · switch(sc_state)
           INIT:        new==MONITORING → 接受；否则拒绝
           MONITORING:  new==FAULT || new==KILL → 接受；否则拒绝
           FAULT:       new==KILL → 接受；否则拒绝
           KILL:        终态，恒拒绝
           default:     状态未知/非法 → 强制 KILL，返回 FALSE（fail-closed）
  → 观测（harness 输出）：results[] 每操作 ret/state + 最终 state
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_state.c` 生产代码。由于 `sc_state` 为 `static` 文件作用域变量，`default`
分支（未知状态强制 KILL）无法经公开 API 直接构造，参照
`Swc_Nvm` / `Swc_RzcNvm` 的既有做法，在 `sc_state.c/.h` 增加
**UNIT_TEST 保护的观测/注入 getter**（仅测试编译，不影响交付固件）：

- `SC_State_TestSetRaw(uint8 state)` — 直接写内部 `sc_state` 为非法值
  （如 0xFF），驱动 Transition 的 `default` fail-closed 分支。

> **被测代码观测**：生产固件（TMS570）不定义 `UNIT_TEST`，该注入 getter
> 绝不进入交付固件。其余状态变化均经公开 `SC_State_Get` / `Transition`
> 返回值验证。

## 被测代码流程图

### SC_State_Init（L25-L28）

```text
[Init]
  ═══→ [sc_state = SC_STATE_INIT]
```

### SC_State_Get（L30-L33）

```text
[Get]
  ═══→ [return sc_state]
```

### SC_State_Transition（L35-L72）

```text
[Transition(new_state)]
  ═══→ [switch(sc_state)]
         ├─ case INIT (0):
         │    {new == MONITORING?}
         │       ├─ Y → [sc_state = new] → [return TRUE]
         │       └─ N → break
         ├─ case MONITORING (1):
         │    {new == FAULT || new == KILL?}
         │       ├─ Y → [sc_state = new] → [return TRUE]
         │       └─ N → break
         ├─ case FAULT (2):
         │    {new == KILL?}
         │       ├─ Y → [sc_state = new] → [return TRUE]
         │       └─ N → break
         ├─ case KILL (3): 终态，break（恒拒绝）
         └─ default（状态未知/非法）:
              → [sc_state = SC_STATE_KILL] → break
  ═══→ [return FALSE]
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `transition` / `setRaw` | When — 执行控制 |
| `newState` | `transition` 目标状态 | `0`(INIT)、`1`(MONITORING)、`2`(FAULT)、`3`(KILL) | When — 载荷 |
| `setRawState` | `setRaw` 注入的内部状态 | `255`(0xFF，非法状态，default 分支) | When — 载荷 |
| 当前状态前置路径 | 进入迁移时 sc_state 的取值 | init 后 INIT、MONITORING、FAULT、KILL、非法(0xFF) | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].ret` | `Transition` 返回值 | `0`=拒绝(FALSE)、`1`=接受(TRUE) |
| `results[i].state` | 每操作后当前状态 | `0/1/2/3`（非法状态强制后为 3） |
| `state`（顶层） | 最终状态 | 同上 |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。

### 规则: 初始化与读取 — SC_State_Init / SC_State_Get

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_state_ready` | P0: init | state=INIT(0) |
| `get_after_init` | P0: init; P1: get | state=INIT(0) |

### 规则: 合法迁移 — 迁移被接受且状态更新

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_to_monitoring_accepted` | P0: init; P1: transition(new=1) | ret=1；state=1 |
| `monitoring_to_fault_accepted` | P0: init; P1: transition(1); P2: transition(2) | ret=1；state=2 |
| `monitoring_to_kill_accepted` | P0: init; P1: transition(1); P2: transition(3) | ret=1；state=3 |
| `fault_to_kill_accepted` | P0: init; P1: transition(1); P2: transition(2); P3: transition(3) | ret=1；state=3 |

### 规则: 非法迁移 — 拒绝且状态不变（fail-closed）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_to_fault_rejected` | P0: init; P1: transition(2) | ret=0；state=0 |
| `init_to_kill_rejected` | P0: init; P1: transition(3) | ret=0；state=0 |
| `init_to_init_rejected` | P0: init; P1: transition(0) | ret=0；state=0 |
| `monitoring_to_init_rejected` | P0: init; P1: transition(1); P2: transition(0) | ret=0；state=1 |
| `monitoring_to_monitoring_rejected` | P0: init; P1: transition(1); P2: transition(1) | ret=0；state=1 |
| `fault_to_init_rejected` | P0: init; P1: transition(1); P2: transition(2); P3: transition(0) | ret=0；state=2 |
| `fault_to_monitoring_rejected` | P0: init; P1: transition(1); P2: transition(2); P3: transition(1) | ret=0；state=2 |
| `fault_to_fault_rejected` | P0: init; P1: transition(1); P2: transition(2); P3: transition(2) | ret=0；state=2 |
| `kill_terminal_rejects_all` | P0: init; P1: transition(1); P2: transition(3); P3: transition(0); P4: transition(1); P5: transition(2); P6: transition(3) | P3-P6 全部 ret=0；state=3 |

### 规则: 未知状态 fail-closed — default 分支强制 KILL

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `unknown_state_forces_kill` | P0: init; P1: setRaw(255); P2: transition(1) | ret=0；state=3（KILL） |

## 代码路径覆盖

- `SC_State_Init`：INIT 赋值路径覆盖。
- `SC_State_Get`：返回当前状态路径覆盖。
- `SC_State_Transition`：
  - `case INIT`：接受侧（new==MONITORING）与拒绝侧（new∈{0,2,3}）全覆盖；
  - `case MONITORING`：或条件两分支（new==FAULT、new==KILL）与全 false
    侧（new∈{0,1}）全覆盖；
  - `case FAULT`：接受侧（new==KILL）与拒绝侧（new∈{0,1,2}）全覆盖；
  - `case KILL`：终态恒拒绝路径覆盖；
  - `default`：非法内部状态强制 KILL（fail-closed）路径经 `setRaw` 注入覆盖。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_state.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（38 / 38）** |
| 分支覆盖 | **100%（18 / 18）** |
| 函数覆盖 | **100%（4 / 4）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_state.feature` | **15 scenarios / 90 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **601 scenarios / 3635 steps passed** |

函数命中次数（`sc_state.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `SC_State_Get` | 252 |
| `SC_State_Init` | 122 |
| `SC_State_TestSetRaw` | 4 |
| `SC_State_Transition` | 126 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_state.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### SC_State_Init（L25-L28）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L26 | `{` | 122 | 全部用例（harness 启动与显式 init 阶段均调用） |
| L27 | `sc_state = SC_STATE_INIT;` | 122 | 全部用例 |
| L28 | `}` | 122 | 全部用例 |

#### SC_State_Get（L30-L33）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L31 | `{` | 252 | 全部用例（harness 每阶段与最终输出均调用） |
| L32 | `return sc_state;` | 252 | 全部用例 |
| L33 | `}` | 252 | 全部用例 |

#### SC_State_Transition（L35-L72）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L36 | `{` | 126 | 全部用例（每个 transition 阶段） |
| L38 | `switch (sc_state)` | 126 | 全部用例 |
| L39 | `case SC_STATE_INIT:` | 53 | `init_to_monitoring_accepted`、`init_to_fault_rejected`、`init_to_kill_rejected`、`init_to_init_rejected` |
| L40 | `if (new_state == SC_STATE_MONITORING)` | 53 | true 侧 `init_to_monitoring_accepted`；false 侧 `init_to_fault_rejected`、`init_to_kill_rejected`、`init_to_init_rejected` |
| L41 | `sc_state = new_state;` | 41 | `init_to_monitoring_accepted`（及所有从 INIT 出发的首次迁移） |
| L42 | `return TRUE;` | 41 | `init_to_monitoring_accepted`、`monitoring_to_fault_accepted`、`monitoring_to_kill_accepted`、`fault_to_kill_accepted` |
| L44 | `break;` | 12 | INIT 状态下的拒绝分支 |
| L46 | `case SC_STATE_MONITORING:` | 37 | `monitoring_to_fault_accepted`、`monitoring_to_kill_accepted`、`monitoring_to_init_rejected`、`monitoring_to_monitoring_rejected` |
| L47 | `if ((new_state == SC_STATE_FAULT) \|\|` | 37 | `monitoring_to_fault_accepted`（FAULT 侧） |
| L48 | `(new_state == SC_STATE_KILL))` | 37 | `monitoring_to_kill_accepted`（KILL 侧，当 FAULT 侧为 false 时求值） |
| L49 | `sc_state = new_state;` | 29 | `monitoring_to_fault_accepted`、`monitoring_to_kill_accepted` |
| L50 | `return TRUE;` | 29 | `monitoring_to_fault_accepted`、`monitoring_to_kill_accepted`、`fault_to_kill_accepted` |
| L52 | `break;` | 8 | MONITORING 状态下的拒绝分支（`monitoring_to_init_rejected`、`monitoring_to_monitoring_rejected`） |
| L54 | `case SC_STATE_FAULT:` | 16 | `fault_to_kill_accepted`、`fault_to_init_rejected`、`fault_to_monitoring_rejected`、`fault_to_fault_rejected` |
| L55 | `if (new_state == SC_STATE_KILL)` | 16 | true 侧 `fault_to_kill_accepted`；false 侧 `fault_to_init_rejected`、`fault_to_monitoring_rejected`、`fault_to_fault_rejected` |
| L56 | `sc_state = new_state;` | 4 | `fault_to_kill_accepted` |
| L57 | `return TRUE;` | 4 | `fault_to_kill_accepted` |
| L59 | `break;` | 12 | FAULT 状态下的拒绝分支 |
| L61 | `case SC_STATE_KILL:` | 16 | `monitoring_to_kill_accepted`、`kill_terminal_rejects_all` 全部迁出请求 |
| L63 | `break;` | 16 | `kill_terminal_rejects_all` |
| L65 | `default:` | 4 | `unknown_state_forces_kill`（setRaw(255) 后） |
| L67 | `sc_state = SC_STATE_KILL;` | 4 | `unknown_state_forces_kill` |
| L68 | `break;` | 4 | `unknown_state_forces_kill` |
| L69 | `}` | 126 | 全部用例 |
| L71 | `return FALSE;` | 52 | 所有拒绝/终态/default 路径 |
| L72 | `}` | 126 | 全部用例 |

#### SC_State_TestSetRaw（L79-L82，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L80 | `{` | 4 | `unknown_state_forces_kill` |
| L81 | `sc_state = state;` | 4 | `unknown_state_forces_kill` |
| L82 | `}` | 4 | `unknown_state_forces_kill` |

### 分支覆盖分析

- `case INIT`（L40）：`new_state == SC_STATE_MONITORING` true（接受）/ false
  （拒绝）两侧覆盖。
- `case MONITORING`（L47-L48）：或条件两真侧（new==FAULT、new==KILL）与
  全 false 侧（new∈{0,1}）三态全覆盖。
- `case FAULT`（L55）：`new_state == SC_STATE_KILL` true / false 两侧覆盖。
- `switch` 五路 case（INIT/MONITORING/FAULT/KILL/default）全部命中。

## 无法覆盖的代码说明

> 本模块无编译期排除分支。`default` 分支（L65-L68）需非法内部状态（0xFF），
> 该状态在交付固件中仅由内存损坏引入、无法经公开 API 构造，故通过
> UNIT_TEST 注入 getter `SC_State_TestSetRaw` 驱动（仅测试编译，不影响
> 交付固件），已纳入用例 `unknown_state_forces_kill`。
> **38/38 行、18/18 分支、4/4 函数全部被端到端测试覆盖，无无法覆盖的
> 可执行代码**。
