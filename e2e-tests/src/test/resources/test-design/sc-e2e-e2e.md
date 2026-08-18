# SC E2E CRC-8 校验 (sc_e2e) E2E 测试设计

## 被测功能

**SC E2E 保护校验（SWR-SC-003 / GAP-SC-002 / SWR-SC-030，ASIL D）**

SC 平台对 6 个接收邮箱（E-Stop、CVC/FZC/RZC 心跳、VehicleState、MotorCurrent）
的 E2E 保护逐帧校验模块：

- **SWR-SC-003 — CRC-8 校验**：接收帧按 AUTOSAR E2E Profile P01（SAE-J1850，
  poly `0x1D`，init `0xFF`，XOR-out `0xFF`）重建 CRC-8，输入顺序为 payload
  字节 2..DLC-1 + DataId 最后（与 BSW `E2E_ComputePduCrc` 一致），与帧 byte1
  比对；任一不一致即判失败。CRC 计算采用 bit-by-bit 无查表实现（`sc_crc8`），
  常量时间、无数据相关提前退出。
- **SWR-SC-003 — byte0 DataId 低半字节校验**：`data[0] & 0x0F` 必须等于
  `dataId & 0x0F`，否则失败。
- **SWR-SC-003 — alive 计数器校验**：byte0 高半字节为 4-bit alive；首帧跳过
  alive 校验（`e2e_first_rx`），此后每帧要求 `alive == (last_alive + 1) & 0x0F`，
  重复/跳号拒绝、15→0 回绕接受。
- **SWR-SC-003 — 连续失败持久锁存**：每邮箱独立 `e2e_fail_count`，达
  `SC_E2E_MAX_CONSEC_FAIL`（生产=3）后置 `e2e_failed`（持久，有效帧仅重置
  计数、不清除锁存——POSIX SIL 才清除）；`SC_E2E_IsMsgFailed` 无效索引
  恒返回 TRUE（fail-closed）。
- **GAP-SC-002 — 关键邮箱 relay-kill 门控**：`SC_E2E_IsAnyCriticalFailed`
  仅 E-Stop + CVC/FZC/RZC 心跳四个关键邮箱的持久失败返回 TRUE；启动宽限期
  （生产 5 tick）内恒返回 FALSE，宽限到期瞬间复位全部失败状态（仅 post-grace
  失败可触发 relay-kill）。
- **SWR-SC-030 — SC_Status TX CRC**：`SC_E2E_ComputeCRC8` 独立计算 SC_Status
  输出帧 CRC（相同 poly/init/XOR-out），NULL → 0。

覆盖链路：

```text
测试 API 注入（op / dataId / msgIndex / dlc / alive / 损坏注入 / nullData / ticks / len）
  → SC_E2E_Init()：
       · 清零 last_alive / fail_count / failed，first_rx=TRUE
       · e2e_grace_remaining = SC_E2E_GRACE_TICKS（生产 5）
  → SC_E2E_Check(data, dlc, dataId, msgIndex)：
       · data==NULL || msgIndex>=6 || dlc<2 → FALSE（不改变状态）
       · alive = data[0] >> 4 & 0x0F
       · byte0 低半字节 != dataId 低半字节 → valid=FALSE
       · 重建 CRC（payload 2..dlc-1 + dataId，payload_len 截断至 6）→ 比对 byte1
       · 非首帧：alive 必须 == (last_alive+1) & 0x0F
       · valid → 更新 last_alive/first_rx、重置 fail_count；无效 → fail_count++，
         达 3 → e2e_failed=TRUE
  → SC_E2E_IsMsgFailed(msgIndex)：越界 → TRUE；否则返回 e2e_failed[msgIndex]
  → SC_E2E_IsAnyCriticalFailed()：
       · 宽限 >0 → 递减；到期瞬间复位全部失败状态；返回 FALSE
       · 宽限结束 → 任一关键邮箱 e2e_failed → TRUE
  → SC_E2E_ComputeCRC8(data, len)：NULL → 0；否则 bit-by-bit CRC-8
  → 观测（harness 输出）：results[] 每操作 state 快照
       · lastAlive[6] / firstRx[6] / failCount[6] / failed[6] /
         isMsgFailed[6] / isMsgFailedInvalid / grace / guardProbe
       · check: ret；drainGrace: anyCriticalFailed；crc8/compute: crc
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_e2e.c` 生产代码。由于模块内部状态全部为 `static` 文件作用域，参照
`sc_state` / `sc_heartbeat` 的既有做法，在 `sc_e2e.c/.h` 增加 **UNIT_TEST
保护的观测 getter**（仅测试编译，不影响交付固件）：

- `SC_E2E_TestGetLastAlive(msgIndex)` / `TestGetFirstRx(msgIndex)` /
  `TestGetFailCount(msgIndex)` / `TestGetFailed(msgIndex)` —
  观测每邮箱 E2E 内部状态；
- `SC_E2E_TestGetGraceRemaining()` — 观测启动宽限计数器；
- `SC_E2E_TestCrc8(data, len)` — 直接驱动静态 `sc_crc8`，覆盖零长度
  （循环体不执行）与已知向量分支。

> **被测代码观测**：生产固件（TMS570）不定义 `UNIT_TEST`，上述 getter 绝不
> 进入交付固件。harness 以生产配置编译（不定义 `PLATFORM_POSIX` /
> `PLATFORM_HIL`），严格 3 次连续失败阈值与 5 tick 宽限生效；`IsMsgFailed`
> 经公开 API 交叉验证，越界守卫经 harness guardProbe（`SC_MB_COUNT` 索引）
> 驱动。

## 被测代码流程图

### SC_E2E_Init（L82-L92）

```text
[Init]
  ═══→ [for i in 0..6)
           last_alive=0 / first_rx=TRUE / fail_count=0 / failed=FALSE
  ═══→ [e2e_grace_remaining = 5（生产 SC_E2E_GRACE_TICKS）]
```

### SC_E2E_Check（L94-L173）

```text
[Check(data, dlc, dataId, msgIndex)]
  ═══→ {data==NULL || msgIndex>=6 || dlc<2?} ─Y→ [return FALSE]
   ↓ N
  [alive = data[0] >> 4 & 0x0F]
  {data[0] & 0x0F != dataId & 0x0F?} ─Y→ [valid = FALSE]
   ↓
  [received_crc = data[1]]
  [payload_len = (dlc>2) ? dlc-2 : 0]  ──> {>6?} ─Y→ [payload_len = 6]
   ↓
  [crc_input = payload[0..payload_len-1] + dataId]
  [expected_crc = sc_crc8(crc_input, payload_len+1)]
  {expected_crc != received_crc?} ─Y→ [valid = FALSE]
   ↓
  {valid && !first_rx[msgIndex]?} ─Y→ {alive != (last_alive+1) & 0x0F?}
                                      │       ├─ Y → [valid = FALSE]
                                      │       └─ N →（通过）
                                      └─N→（首帧/已失败，跳过）
   ↓
  {valid?} ─Y→ [last_alive=alive / first_rx=FALSE / fail_count=0]
            └─N→ [fail_count++]
                    ↓ {fail_count >= 3?} → [e2e_failed[msgIndex] = TRUE]
  [return valid]
```

### SC_E2E_IsMsgFailed（L175-L181）

```text
[IsMsgFailed(msgIndex)]
  ═══→ {msgIndex >= 6?} ─Y→ [return TRUE]（fail-closed）
   ↓ N
  [return e2e_failed[msgIndex]]
```

### SC_E2E_IsAnyCriticalFailed（L183-L221）

```text
[IsAnyCriticalFailed]
  ═══→ {grace_remaining > 0?} ─Y→ [grace--]
                                      ↓ {==0?}
                                          ├─ Y → [复位全部 fail_count/failed/
                                          │        first_rx=TRUE]
                                          └─ N →（继续）
                                      [return FALSE]
   ↓ N（宽限结束）
  {e2e_failed[ESTOP]?}   ─Y→ [return TRUE]
  {e2e_failed[CVC_HB]?}  ─Y→ [return TRUE]
  {e2e_failed[FZC_HB]?}  ─Y→ [return TRUE]
  {e2e_failed[RZC_HB]?}  ─Y→ [return TRUE]
  [return FALSE]
```

### SC_E2E_ComputeCRC8（L223-L242）

```text
[ComputeCRC8(data, len)]
  ═══→ {data == NULL?} ─Y→ [return 0]
   ↓ N
  [bit-by-bit CRC-8（poly 0x1D，init 0xFF）]
  [return crc ^ 0xFF]
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `check` / `drainGrace` / `crc8` / `compute` | When — 执行控制 |
| `skipInit` | 首阶段是否跳过自动 Init | `false`、`true` | When — 执行控制 |
| `dataId` | E2E Data ID（byte0 低半字节 + CRC 输入尾字节） | `1`(ESTOP)、`2`(CVC_HB)、`3`(FZC_HB)、`4`(RZC_HB)、`5`(VEHSTATE)、`15`(MOTORCUR) | When — 载荷 |
| `msgIndex` | 目标邮箱索引 | `0`..`5`（有效）、`6`/`255`（越界） | When — 载荷 |
| `dlc` | 数据长度码 | `0`/`1`（<2 守卫）、`2`（零 payload 边界）、`8`（典型）、`255`（payload 截断） | When — 载荷 |
| `alive` | alive 计数器 | `0`、`1`、`2`、`5`、`15`（回绕边界） | When — 载荷 |
| `crcCorrupt` | 翻转 byte1（CRC 字节） | `0`、`1` | When — 故障注入 |
| `dataIdCorrupt` | 强制 byte0 低半字节 != dataId | `0`、`1` | When — 故障注入 |
| `payloadCorrupt` | 翻转 payload 字节 data[4] | `0`、`1` | When — 故障注入 |
| `nullData` | check/crc8/compute 传 NULL_PTR | `0`、`1` | When — 故障注入 |
| `ticks` | drainGrace 调用 IsAnyCriticalFailed 次数 | `1`、`4`（宽限前）、`5`（宽限边界） | When — 执行控制 |
| `len` | crc8/compute 输入长度 | `0`（空）、`1`、`3`（已知向量） | When — 载荷 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].ret` | `SC_E2E_Check` 返回值 | 0/1 |
| `results[i].state.lastAlive[m]` | 每邮箱最近 alive | 0..15 |
| `results[i].state.firstRx[m]` | 每邮箱首帧标志 | 0/1 |
| `results[i].state.failCount[m]` | 每邮箱连续失败计数 | 0..3+ |
| `results[i].state.failed[m]` | 每邮箱持久失败锁存 | 0/1 |
| `results[i].state.isMsgFailed[m]` | 公开 `SC_E2E_IsMsgFailed(m)` | 0/1 |
| `results[i].state.isMsgFailedInvalid` | `SC_E2E_IsMsgFailed(6)`（越界） | 1 |
| `results[i].state.grace` | 启动宽限计数器 | 5→0 |
| `results[i].anyCriticalFailed` | `SC_E2E_IsAnyCriticalFailed()` 最近一次返回值 | 0/1 |
| `results[i].crc` | `sc_crc8` / `SC_E2E_ComputeCRC8` 结果 | 0x00/0x26/0xB7 |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。
> 宽限类用例统一先 `drainGrace ticks=5` 消费启动宽限期，以验证 post-grace
> 关键邮箱门控（`skipInit` 不用，宽限本身也是被测功能）。

### 规则: 初始化 — SC_E2E_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_resets_all_state` | P0: init | lastAlive/failCount/failed 全 0；firstRx 全 1；isMsgFailed 全 0；grace=5 |

### 规则: 参数校验守卫 — SC_E2E_Check

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `null_data_rejected` | P0: init; P1: check(nullData=1) | ret=0；状态不变 |
| `invalid_msg_index_rejected` | P0: init; P1: check(msgIndex=6) | ret=0；lastAlive 全 0 |
| `short_dlc_rejected` | P0: init; P1: check(dlc=1); P2: check(dlc=0) | 两次均 ret=0 |
| `valid_first_message_accepted` | P0: init; P1: check(alive=1) | ret=1；lastAlive[0]=1；firstRx[0]=0（首帧跳过 alive） |

### 规则: CRC / DataId 校验 — SWR-SC-003

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `crc_corrupt_rejected` | P0: init; P1: check(crcCorrupt=1) | ret=0；failCount[0]=1 |
| `dataid_mismatch_rejected` | P0: init; P1: check(dataIdCorrupt=1) | ret=0；failCount[0]=1 |
| `payload_corrupt_rejected` | P0: init; P1: check(payloadCorrupt=1) | ret=0；failCount[0]=1 |
| `zero_payload_dlc2_accepted` | P0: init; P1: check(dlc=2, alive=1) | ret=1；CRC 仅覆盖 DataId；lastAlive[0]=1 |
| `oversized_dlc_capped` | P0: init; P1: check(dlc=255, alive=1) | ret=1；payload_len 截断至 6 |
| `crc8_known_vector` | P0: init; P1: crc8(len=3) | crc=0xB7（{0x01,0xAA,0x55}） |
| `crc8_empty_null` | P0: init; P1: crc8(len=0, nullData=1) | crc=0x00（循环体不执行） |

### 规则: alive 计数器校验 — SWR-SC-003

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `sequential_alive_accepted` | P0: init; P1: check(alive=1); P2: check(alive=2) | 均 ret=1；lastAlive[0]=2 |
| `repeated_alive_rejected` | P0: init; P1: check(alive=5); P2: check(alive=5) | P2 ret=0；lastAlive[0] 仍 5 |
| `alive_skip_rejected` | P0: init; P1: check(alive=1); P2: check(alive=3) | P2 ret=0（跳号） |
| `alive_wrap_15_to_0` | P0: init; P1: check(alive=15); P2: check(alive=0) | 均 ret=1；lastAlive[0]=0 |

### 规则: 连续失败持久锁存 — SC_E2E_MAX_CONSEC_FAIL=3

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `two_failures_not_persistent` | P0: init; P1-P2: check(crcCorrupt)×2 | failCount[0]=2；failed[0]=0；isMsgFailed[0]=0 |
| `three_failures_latch` | P0: init; P1-P3: check(crcCorrupt)×3 | failCount[0]=3；failed[0]=1；isMsgFailed[0]=1 |
| `valid_frame_resets_failure_count` | P0: init; P1-P2: check(crcCorrupt)×2; P3: check(alive=1) | P3 ret=1；failCount[0]=0；failed[0]=0 |
| `invalid_index_fail_closed` | P0: init | isMsgFailedInvalid=1（越界恒 TRUE） |

### 规则: 启动宽限期 — SC_E2E_IsAnyCriticalFailed

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `grace_prevents_relay_kill` | P0: init; P1: drainGrace(4) | anyCriticalFailed=0；grace=1 |
| `grace_expiry_resets_latch` | P0: init; P1-P3: check(crcCorrupt)×3; P4: drainGrace(5) | anyCriticalFailed=0；grace=0；failed 全 0；failCount 全 0；firstRx 全 1（到期复位） |
| `grace_expired_enforces_critical` | P0: init; P1: drainGrace(5); P2-P4: check(crcCorrupt)×3; P5: drainGrace(1) | P5 anyCriticalFailed=1；failed[0]=1 |

### 规则: 关键邮箱判定 — GAP-SC-002

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `estop_failure_triggers_relay_kill` | P0: init; P1: drainGrace(5); P2-P4: check(dataId=1,msgIndex=0,crcCorrupt)×3; P5: drainGrace(1) | anyCriticalFailed=1；failed[0]=1 |
| `cvc_hb_failure_triggers_relay_kill` | 同上，dataId=2,msgIndex=1 | anyCriticalFailed=1；failed[1]=1 |
| `fzc_hb_failure_triggers_relay_kill` | 同上，dataId=3,msgIndex=2 | anyCriticalFailed=1；failed[2]=1 |
| `rzc_hb_failure_triggers_relay_kill` | 同上，dataId=4,msgIndex=3 | anyCriticalFailed=1；failed[3]=1 |
| `non_critical_mailbox_no_relay_kill` | P0: init; P1: drainGrace(5); P2-P4: check(dataId=5,msgIndex=4,crcCorrupt)×3; P5-P7: check(dataId=15,msgIndex=5,crcCorrupt)×3; P8: drainGrace(1) | anyCriticalFailed=0；failed=[0,0,0,0,1,1] |

### 规则: SC_Status TX CRC — SWR-SC-030

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `compute_crc8_null_returns_zero` | P0: init; P1: compute(len=3, nullData=1) | crc=0 |
| `compute_crc8_known_vector` | P0: init; P1: compute(len=3) | crc=0xB7（{0x01,0xAA,0x55}） |
| `compute_crc8_empty` | P0: init; P1: compute(len=0) | crc=0x00（init ^ XOR-out） |
| `compute_crc8_single_byte` | P0: init; P1: compute(len=1) | crc=0x26（{0x01}） |

## 代码路径覆盖

- `SC_E2E_Init`：全槽清零 + firstRx 置位 + 宽限置位路径覆盖。
- `SC_E2E_Check`：三条件守卫（data==NULL / msgIndex>=6 / dlc<2 各 true 侧 +
  全 false 侧）、DataId 半字节校验、CRC 重建（payload_len 0/2/6/截断）、
  alive 校验（首帧跳过 / 重复 / 跳号 / 回绕）、失败计数递增与阈值锁存、
  有效帧重置全覆盖。
- `SC_E2E_IsMsgFailed`：越界守卫与正常路径全覆盖。
- `SC_E2E_IsAnyCriticalFailed`：宽限递减（含到期复位分支）、四个关键邮箱
  各 true 侧 + 全 false 侧全覆盖。
- `SC_E2E_ComputeCRC8`：NULL、零长度、已知向量、单字节全覆盖。
- `sc_crc8`（内部）：经 `SC_E2E_Check` 每帧调用（len 1..7）覆盖循环体两
  侧；零长度循环不执行经 `TestCrc8` 钩子覆盖。
- UNIT_TEST getter：越界守卫经 guardProbe、正常返回经每阶段快照全覆盖。

## 无法覆盖的代码说明

> **编译期排除**（不计入行统计，故 149/149 已覆盖全部可执行行）：
> 1. `SC_E2E_Check` 中 `#ifdef PLATFORM_POSIX` 的「有效帧清除 e2e_failed
>    锁存」块（L160-164）——harness 以生产配置编译（不定义 `PLATFORM_POSIX`），
>    该 SIL 平台特性（Docker 抖动容忍）由 POSIX SIL 系统级测试覆盖。
> 2. `SC_E2E_IsAnyCriticalFailed` 中 `#ifdef PLATFORM_POSIX` 的「无条件返回
>    FALSE（禁用 E2E 强制）」块（L185-192）——同理，生产逻辑为下方完整
>    宽限 + 关键邮箱门控。
> 3. `SC_E2E_GRACE_TICKS` 的 `#ifdef PLATFORM_POSIX`（1000）/ `#elif
>    PLATFORM_HIL`（1000）分支——harness 编译生产 `#else` 5 tick 分支，
>    宽限 5/1000 两侧均非被测输入。
> 4. `#ifndef PLATFORM_HIL` 的 `expected_alive` 声明与 alive 校验块
>    （L146-153）——HIL 编译排除；harness 以生产配置编译，该块**包含**
>    且被 alive 用例全覆盖（L146-L153 在报告中有命中）。HIL 平台跳过
>    alive 校验的特性由 HIL 测试 `test_hil_e2e.py` 覆盖。
>
> **不可达分支**：**无**。本模块不存在经公开 API 无法构造输入的分支——
> 与 `Swc_RzcScheduler`（编译期只读表防御分支）不同，`sc_crc8` 零长度
> 分支经 `TestCrc8` 钩子覆盖，宽限到期复位循环经 `grace_expiry_resets_latch`
> 覆盖，四关键邮箱各 true/false 侧均经独立用例覆盖。66/66 分支全部两侧
> 命中，**无需任何豁免**。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_e2e.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（149 / 149）** |
| 分支覆盖 | **100%（66 / 66）** |
| 函数覆盖 | **100%（12 / 12）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_e2e.feature` | **31 scenarios / 186 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **658 scenarios / 3977 steps passed** |

函数命中次数（`sc_e2e.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `SC_E2E_Init` | 126 |
| `SC_E2E_Check` | 94 |
| `SC_E2E_IsMsgFailed` | 2048 |
| `SC_E2E_IsAnyCriticalFailed` | 78 |
| `SC_E2E_ComputeCRC8` | 8 |
| `sc_crc8`（内部静态） | 90 |
| `SC_E2E_TestGetLastAlive` | 1792 |
| `SC_E2E_TestGetFirstRx` | 1792 |
| `SC_E2E_TestGetFailCount` | 1792 |
| `SC_E2E_TestGetFailed` | 1792 |
| `SC_E2E_TestGetGraceRemaining` | 256 |
| `SC_E2E_TestCrc8` | 4 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_e2e.c.gcov.html`
> 的逐行 hit count 回填。所有 149 个可执行行均至少被 1 个端到端场景命中，
> 全部 66 个分支两侧均被覆盖。

#### sc_crc8（L58-L76，内部静态）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L59-L62 | 函数入口 + 声明 | 90 | 全部 `check` 用例（`SC_E2E_Check` 每帧经 L135 调用） |
| L64 | `for (i = 0u; i < len; i++)` | 686 | true 侧 `sequential_alive_accepted`/`crc_corrupt_rejected` 等（payload_len≥1）；false 侧 `crc8_empty_null`（len=0 循环不执行） |
| L65 | `crc ^= data[i];` | 596 | 全部非空 CRC 计算 |
| L66 | `for (j = 0u; j < 8u; j++)` | 5364 | true/false 两侧（bit 循环退出） |
| L67 | `if ((crc & 0x80u) != 0u)` | 4768 | 两侧：`crc8_known_vector`/`compute_crc8_known_vector` 等（poly 异或分支 2334 次、左移分支 2434 次） |
| L68 | `crc = (crc << 1u) ^ POLY` | 2334 | 高位为 1 时 |
| L69-L71 | else `crc <<= 1u` | 2434 | 高位为 0 时 |
| L72-L73 | 内/外层循环收尾 | 4768 / 596 | 全部 CRC 计算 |
| L75 | `return crc ^ 0xFFu;` | 90 | 全部 `check`/`crc8` 用例 |

#### SC_E2E_Init（L82-L92）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L83-L84 | 函数入口 + 声明 | 126 | 全部用例（harness 启动自动 init + 显式 `init` 阶段） |
| L85 | `for (i = 0u; i < SC_MB_COUNT; i++)` | 882 | true/false 两侧（6 邮箱 × 每 init） |
| L86-L90 | 清零 last_alive/first_rx/fail_count/failed | 756 | 全部用例 |
| L91 | `e2e_grace_remaining = SC_E2E_GRACE_TICKS;` | 126 | 全部用例（生产 5 tick） |
| L92 | `}` | 126 | 全部用例 |

#### SC_E2E_Check（L94-L173）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L96-L106 | 函数入口 + 声明 | 94 | 全部 `check` 用例 |
| L108 | `if ((data == NULL_PTR) \|\| (msgIndex >= SC_MB_COUNT) \|\| (dlc < 2u))` | 94 | 三子条件 true 侧：`null_data_rejected`（data==NULL）、`invalid_msg_index_rejected`（msgIndex=6）、`short_dlc_rejected`（dlc=1/0）；false 侧其余 |
| L109-L110 | `return FALSE;` | 8 | 三守卫 true 侧用例 |
| L113 | `alive = data[0] >> 4 & 0x0F;` | 86 | 全部通过守卫的 `check` |
| L116 | `if ((data[0] & 0x0Fu) != (dataId & 0x0Fu))` | 86 | true 侧 `dataid_mismatch_rejected`（2 次）；false 侧其余 |
| L117-L118 | `valid = FALSE;` | 2 | `dataid_mismatch_rejected` |
| L121 | `received_crc = data[1];` | 86 | 全部通过守卫的 `check` |
| L125 | `payload_len = (dlc > 2u) ? (dlc-2u) : 0u;` | 86 | true 侧 dlc=4/8/255；false 侧 `zero_payload_dlc2_accepted`（dlc=2） |
| L126 | `if (payload_len > 6u)` | 86 | true 侧 `oversized_dlc_capped`（dlc=255，payload_len 253→6）；false 侧 dlc≤8 |
| L127-L128 | `payload_len = 6u;` | 2 | `oversized_dlc_capped` |
| L129 | `for (i = 0u; i < payload_len; i++)` | 590 | true 侧 payload 拷贝；false 侧 `zero_payload_dlc2_accepted`（payload_len=0 不进入） |
| L130-L131 | `crc_input[i] = data[2u + i];` | 504 | payload 拷贝 |
| L132 | `crc_input[payload_len] = dataId;` | 86 | 全部通过守卫的 `check`（CRC 输入尾字节） |
| L135 | `expected_crc = sc_crc8(...)` | 86 | 全部通过守卫的 `check` |
| L138 | `if (expected_crc != received_crc)` | 86 | true 侧 `crc_corrupt_rejected`/`payload_corrupt_rejected`（60 次）；false 侧有效帧 |
| L139-L140 | `valid = FALSE;` | 60 | CRC/DataId/payload 损坏用例 |
| L147 | `if ((valid == TRUE) && (e2e_first_rx[msgIndex] == FALSE))` | 86 | 第一条件 false 侧（损坏帧）；第二条件 false 侧 `valid_first_message_accepted` 等首帧；两侧均 true 的后续有效帧 |
| L148 | `expected_alive = (last_alive + 1u) & 0x0Fu;` | 8 | 非首帧有效 `check` |
| L149 | `if (alive != expected_alive)` | 8 | true 侧 `repeated_alive_rejected`/`alive_skip_rejected`（4 次）；false 侧 `sequential_alive_accepted`/`alive_wrap_15_to_0`（4 次） |
| L150-L151 | `valid = FALSE;` | 4 | 重复/跳号 alive 用例 |
| L156 | `if (valid == TRUE)` | 86 | true 侧有效帧（20 次）；false 侧损坏帧（66 次） |
| L157-L159 | 更新 last_alive / first_rx / 重置 fail_count | 20 | 全部有效 `check` |
| L165-L166 | `e2e_fail_count[msgIndex]++;` | 66 | 全部失败帧 |
| L167 | `if (e2e_fail_count >= SC_E2E_MAX_CONSEC_FAIL)` | 66 | true 侧第 3 次连续失败（16 次）；false 侧第 1/2 次 |
| L168-L169 | `e2e_failed[msgIndex] = TRUE;` | 16 | `three_failures_latch` 及全部关键/非关键邮箱锁存用例 |
| L170 | `}` | 66 | 失败分支收尾 |
| L172 | `return valid;` | 86 | 全部通过守卫的 `check` |
| L173 | `}` | 94 | 全部 `check` |

#### SC_E2E_IsMsgFailed（L175-L181）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L176 | 函数体 | 2048 | 全部用例（harness 每阶段快照 6 邮箱 + guardProbe） |
| L177 | `if (msgIndex >= SC_MB_COUNT)` | 2048 | true 侧 guardProbe（512 次）；false 侧正常索引（1536 次） |
| L178-L179 | `return TRUE;` | 512 | guardProbe 越界 |
| L180 | `return e2e_failed[msgIndex];` | 1536 | 正常索引查询（`three_failures_latch` 后=1） |

#### SC_E2E_IsAnyCriticalFailed（L183-L221）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L184 | 函数体 | 78 | 全部 `drainGrace` 用例 |
| L196 | `if (e2e_grace_remaining > 0u)` | 78 | true 侧宽限期内（68 次）；false 侧 `grace_expired_enforces_critical` 等宽限结束后（10 次） |
| L197 | `e2e_grace_remaining--;` | 68 | 宽限期内 drainGrace |
| L198 | `if (e2e_grace_remaining == 0u)` | 68 | true 侧宽限到期瞬间（12 次）；false 侧 4→1 递减（56 次） |
| L203-L208 | 复位 fail_count/failed/first_rx 循环 | 12 | `grace_expiry_resets_latch`（宽限到期复位） |
| L210 | `return FALSE;` | 68 | 宽限期内 |
| L216 | `if (e2e_failed[ESTOP] == TRUE)` | 10 | true 侧 `estop_failure_triggers_relay_kill`（2 次）；false 侧其余 |
| L217 | `if (e2e_failed[CVC_HB] == TRUE)` | 8 | true 侧 `cvc_hb_failure_triggers_relay_kill`（2 次）；false 侧其余 |
| L218 | `if (e2e_failed[FZC_HB] == TRUE)` | 6 | true 侧 `fzc_hb_failure_triggers_relay_kill`（2 次）；false 侧其余 |
| L219 | `if (e2e_failed[RZC_HB] == TRUE)` | 4 | true 侧 `rzc_hb_failure_triggers_relay_kill`（2 次）；false 侧 `non_critical_mailbox_no_relay_kill`（2 次） |
| L220 | `return FALSE;` | 2 | `non_critical_mailbox_no_relay_kill`（仅非关键邮箱失败） |
| L221 | `}` | 4 | 宽限结束分支收尾 |

#### SC_E2E_ComputeCRC8（L223-L242）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L224-L229 | 函数入口 + 声明 | 8 | 全部 `compute` 用例 |
| L231 | `if (data == NULL_PTR)` | 8 | true 侧 `compute_crc8_null_returns_zero`（2 次）；false 侧其余 |
| L232-L233 | `return 0u;` | 2 | `compute_crc8_null_returns_zero` |
| L234 | `for (i = 0u; i < len; i++)` | 14 | true 侧 len≥1；false 侧 `compute_crc8_empty`（len=0） |
| L235 | `crc ^= data[i];` | 8 | len=1/3 用例 |
| L236 | `for (j = 0u; j < 8u; j++)` | 72 | true/false 两侧 |
| L237 | `if ((crc & 0x80u) != 0u)` | 64 | 两侧（38/26） |
| L238-L240 | poly 异或 / 左移 | 64 | 两侧 |
| L241 | `return crc ^ 0xFFu;` | 6 | len=0/1/3 用例 |
| L242 | `}` | 8 | 全部 `compute` |

#### UNIT_TEST getter（L250-L291，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|---|
| L252-L257 | `TestGetLastAlive` 越界守卫 + 正常返回 | 1792 / 256 / 1536 | 全部用例（每阶段快照）+ guardProbe 越界 |
| L260-L265 | `TestGetFirstRx` | 1792 / 256 / 1536 | 全部用例 + guardProbe |
| L268-L273 | `TestGetFailCount` | 1792 / 256 / 1536 | 全部用例 + guardProbe |
| L276-L281 | `TestGetFailed` | 1792 / 256 / 1536 | 全部用例 + guardProbe |
| L284-L286 | `TestGetGraceRemaining` | 256 | 全部用例 |
| L289-L291 | `TestCrc8` | 4 | `crc8_known_vector`、`crc8_empty_null` |

### 分支覆盖分析

- `sc_crc8`：外层循环入口（L64）、bit 循环（L66）、poly 异或判断（L67）
  全部两侧覆盖（L67 两侧实测 2334/2434）。
- `SC_E2E_Check`：三条件守卫（L108，3 对子分支全部两侧）、DataId 校验
  （L116）、payload_len 三元/截断（L125/L126）、payload 拷贝循环（L129）、
  CRC 比对（L138）、alive 校验条件（L147 四子分支）与阈值（L149）、失败
  计数阈值（L167）全部两侧覆盖。
- `SC_E2E_IsMsgFailed`：越界守卫（L177）两侧覆盖。
- `SC_E2E_IsAnyCriticalFailed`：宽限递减（L196）、到期复位（L198）、复位
  循环（L204）、四个关键邮箱（L216-L219）全部两侧覆盖。
- `SC_E2E_ComputeCRC8`：NULL 守卫（L231）、外层循环（L234）、bit 循环
  （L236）、poly 判断（L237）全部两侧覆盖。
- 全部 66 个分支 **无一豁免**（0 个不可达分支），与 `Swc_RzcScheduler`
  （2 个编译期只读表防御分支豁免）不同——本模块每条分支均可经公开 API
  或 UNIT_TEST 钩子构造输入覆盖。
