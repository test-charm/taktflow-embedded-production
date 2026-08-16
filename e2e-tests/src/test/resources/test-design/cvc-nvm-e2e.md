# CVC NVM 存储 (Swc_Nvm) E2E 测试设计

## 被测功能

**CVC NVM 存储 SWC — DTC 持久化与校准数据存储（SWR-CVC-030、SWR-CVC-031）：

- **DTC 持久化（SWR-CVC-030）**：20-slot 循环缓冲存储 DTC 条目（DTC ID、
  状态掩码、出现计数、32 字节冻结帧），每条目以 CRC-16（多项式 0x1021、
  初值 0xFFFF）保护，加载时校验 CRC，损坏即拒绝（fail-closed）。
- **校准数据（SWR-CVC-031）**：踏板阈值/消抖/卡滞阈值与 16 项扭矩 LUT，
  CRC-16 保护；写入时重算 CRC；读取时 CRC 损坏则回退编译期默认值并返回
  E_NOT_OK。
- **未初始化守卫**：Init 未调用时全部 API（StoreDtc/LoadDtc/ReadCal/WriteCal）
  返回 E_NOT_OK。
- **CRC-16/CCITT**：`Swc_Nvm_CalcCrc16` 为公开 API，对已知数据产生确定性结果，
  NULL 指针返回 0，长度 0 返回初值 0xFFFF。**

覆盖链路：

```text
测试 API 注入（op / skipInit / repeats / dtcId / status / ffMode / slot /
  nullEntry / nullCal / pThreshold / pDebounce / stuckThreshold / stuckCycles /
  lut0 / dataLen / nullCrc）
  → Swc_Nvm_Init()（SWR-CVC-030/031 前置）：
       · 清零 20 槽 DTC 条目（含 32B 冻结帧）
       · 装载默认校准并计算 CRC
       · Nvm_Initialized = TRUE
  → Swc_Nvm_StoreDtc(dtcId, status, freezeFrame)（SWR-CVC-030）：
       · 未初始化 → E_NOT_OK
       · 写入当前写索引槽，occurrenceCount = 计数+1
       · 冻结帧 NULL → 全零；非 NULL → 原样复制
       · 计算并保存条目 CRC；写索引 +1 且 ≥20 回绕
       · 计数 <20 时 +1（封顶 20）
  → Swc_Nvm_LoadDtc(slotIndex, entry)（SWR-CVC-030）：
       · 未初始化 / NULL entry / 越界槽位 → E_NOT_OK
       · CRC 校验失败 → E_NOT_OK（fail-closed）
       · 校验通过 → E_OK
  → Swc_Nvm_ReadCal(calData)（SWR-CVC-031）：
       · 未初始化 / NULL calData → E_NOT_OK
       · CRC 校验失败 → 回退默认值 + E_NOT_OK
       · 校验通过 → E_OK
  → Swc_Nvm_WriteCal(calData)（SWR-CVC-031）：
       · 未初始化 / NULL calData → E_NOT_OK
       · 保存并重算 CRC → E_OK
  → Swc_Nvm_CalcCrc16(data, length)：
       · data == NULL → 0u
       · 长度 0 → 初值 0xFFFF
       · 常规 → CRC-16/CCITT
  → 观测（harness 输出）：results[] 每操作结果 + initialized / writeIndex /
      dtcCount
```

与既有 ASW E2E（`Swc_SelfTest`、`Swc_Scheduler` 等）一致，通过测试专用 API 在
原生测试框架内执行真实的 `Swc_Nvm.c` 生产代码。由于 `Swc_Nvm` 的 DTC 槽位与
校准块均为 `static` 文件作用域状态，且 CRC 损坏分支无法经公开 API 直接构造，
参照 `Swc_Heartbeat` / `Swc_Watchdog` / `Swc_CanMonitor` 的既有做法，在
`Swc_Nvm.c/.h` 增加了 **UNIT_TEST 保护的观测/损坏注入 getter**（仅测试编译，
不影响交付固件）：

- `Swc_Nvm_TestGetInitialized()` / `TestGetDtcWriteIndex()` / `TestGetDtcCount()`
  — 观测内部静态状态（初始化标志、循环缓冲写索引、DTC 计数）；
- `Swc_Nvm_TestCorruptDtcCrc(slot)` / `TestCorruptCalCrc()` — 翻转已存条目的
  CRC，驱动 LoadDtc 的损坏检测分支与 ReadCal 的默认值回退分支。

> **被测代码观测**：生产固件（STM32/TMS570/POSIX）不定义 `UNIT_TEST`，上述
> getter 与损坏注入钩子绝不进入交付固件。`Nvm_DtcSlots` / `Nvm_DtcCount` /
> `Nvm_DtcWriteIndex` / `Nvm_CalData` 的其余可观测面均通过公开 API
> （LoadDtc / ReadCal）验证。

## 被测代码流程图

### Swc_Nvm_Init (L123-149)

```
┌──────────────────────────┐
│ Swc_Nvm_Init(void)       │
└─────────────┬────────────┘
              │
  Nvm_DtcWriteIndex = 0; Nvm_DtcCount = 0
              │
  for i in 0..19:  每次清 dtcId/status/occurrenceCount/crc=0
     └─ for j in 0..31: freezeFrame[j] = 0
              │
  Nvm_CalData = Nvm_DefaultCal
  Nvm_CalData.crc = Nvm_ComputeCalCrc(&Nvm_CalData)
  Nvm_Initialized = TRUE
```

### Swc_Nvm_StoreDtc (L158-204)

```
┌────────────────────────────────────────┐
│ Swc_Nvm_StoreDtc(dtcId, status, ff)    │
└──────────────────┬─────────────────────┘
                   │
  Nvm_Initialized != TRUE? ──Y──→ return E_NOT_OK
                   │N
  slot = &Nvm_DtcSlots[Nvm_DtcWriteIndex]
  slot->dtcId/status/occurrenceCount(=计数+1) 写入
                   │
  for i in 0..31:
     freezeFrame != NULL? ─┬─Y─→ slot->freezeFrame[i] = ff[i]
                           └─N─→ slot->freezeFrame[i] = 0
                   │
  slot->crc = Nvm_ComputeDtcCrc(slot)
  Nvm_DtcWriteIndex++
  WriteIndex >= 20? ──Y──→ WriteIndex = 0
                   │N (或已回绕)
  Nvm_DtcCount < 20? ──Y──→ Nvm_DtcCount++
                   │N (封顶)
  return E_OK
```

### Swc_Nvm_LoadDtc (L213-243)

```
┌────────────────────────────────────────┐
│ Swc_Nvm_LoadDtc(slotIndex, entry)      │
└──────────────────┬─────────────────────┘
                   │
  Nvm_Initialized != TRUE? ──Y──→ return E_NOT_OK
                   │N
  entry == NULL_PTR? ────────Y──→ return E_NOT_OK
                   │N
  slotIndex >= NVM_MAX_DTC_SLOTS? ──Y──→ return E_NOT_OK
                   │N
  *entry = Nvm_DtcSlots[slotIndex]
  computedCrc = Nvm_ComputeDtcCrc(entry)
                   │
  computedCrc != entry->crc? ──Y──→ return E_NOT_OK (fail-closed)
                   │N
  return E_OK
```

### Swc_Nvm_ReadCal (L252-280)

```
┌────────────────────────────────────────┐
│ Swc_Nvm_ReadCal(calData)               │
└──────────────────┬─────────────────────┘
                   │
  Nvm_Initialized != TRUE? ──Y──→ return E_NOT_OK
                   │N
  calData == NULL_PTR? ──────Y──→ return E_NOT_OK
                   │N
  *calData = Nvm_CalData
  computedCrc = Nvm_ComputeCalCrc(calData)
                   │
  computedCrc != calData->crc? ──Y──→ *calData = 默认值; 重算CRC; return E_NOT_OK
                   │N
  return E_OK
```

### Swc_Nvm_WriteCal (L289-307)

```
┌────────────────────────────────────────┐
│ Swc_Nvm_WriteCal(calData)              │
└──────────────────┬─────────────────────┘
                   │
  Nvm_Initialized != TRUE? ──Y──→ return E_NOT_OK
                   │N
  calData == NULL_PTR? ──────Y──→ return E_NOT_OK
                   │N
  Nvm_CalData = *calData
  Nvm_CalData.crc = Nvm_ComputeCalCrc(&Nvm_CalData)
  return E_OK
```

### Swc_Nvm_CalcCrc16 (L59-90)

```
┌────────────────────────────────────────┐
│ Swc_Nvm_CalcCrc16(data, length)        │
└──────────────────┬─────────────────────┘
                   │
  data == NULL_PTR? ──Y──→ return 0u
                   │N
  crc = NVM_CRC16_INIT (0xFFFF)
  for i in 0..length-1:
     crc ^= data[i] << 8
     for j in 0..7:
        crc & 0x8000? ─┬─Y─→ crc = (crc<<1) ^ 0x1021
                       └─N─→ crc = crc << 1
  return crc
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行的操作 | `init` / `storeDtc` / `loadDtc` / `readCal` / `writeCal` / `corruptDtcCrc` / `corruptCalCrc` / `calcCrc` | When — 执行控制 |
| `skipInit` | 是否跳过 `Swc_Nvm_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `repeats` | `storeDtc` 存储次数 | `1`、`20`（循环缓冲填满）、`21`（回绕边界） | When — 执行控制 |
| `dtcId` | 存储的 DTC 事件 ID | `0`、`5`、`99`（>20 区分回绕） | When — 载荷 |
| `status` | 存储的 DTC 状态掩码 | `0`、`1`、`8` | When — 载荷 |
| `ffMode` | 冻结帧模式 | `0`=NULL（全零）、`1`=0xA0+i 模式 | When — 载荷 |
| `slot` | `loadDtc`/`corruptDtcCrc` 槽位 | `0`（首个）、`19`（最后有效）、`20`（越界边界） | When — 执行控制 |
| `nullEntry` | `loadDtc` 传 NULL entry | `false`、`true`（NULL 守卫） | When — 守卫 |
| `nullCal` | `readCal`/`writeCal` 传 NULL | `false`、`true`（NULL 守卫） | When — 守卫 |
| `pThreshold` 等 | `writeCal` 自定义校准值 | `0`（默认）、`500`（非默认） | When — 载荷 |
| `dataLen` | `calcCrc` 数据长度 | `0`（初值边界）、`4`（常规） | When — 载荷 |
| `nullCrc` | `calcCrc` 传 NULL 数据 | `false`、`true`（NULL 守卫） | When — 守卫 |
| 阶段序列 | 多阶段调用顺序 | 单次、store→load、store→corrupt→load、writeCal→readCal、writeCal→corrupt→readCal | When — 执行控制 |

> `skipInit`/`nullEntry`/`nullCal`/`nullCrc` 均只有两个等价类（触发/不触发）。
> `repeats` 的 `20`/`21` 为循环缓冲回绕边界；`slot=20` 为 `NVM_MAX_DTC_SLOTS`
> 越界边界；`dataLen=0` 为 CRC 初值边界。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].op` | 第 i 阶段操作名 | 与输入 op 一致（harness 回显） |
| `results[i].ret` | 第 i 阶段返回值 | `0`=E_OK、`1`=E_NOT_OK |
| `results[i].slot` | `storeDtc` 实际写入槽位 | 写入时的写索引 |
| `results[i].dtcId/status/occurrenceCount` | `loadDtc` 读回条目 | 与存储时一致 |
| `results[i].ffHex` | `loadDtc` 读回冻结帧（hex） | 模式/全零 |
| `results[i].plausThreshold` 等 | `readCal` 读回校准 | 默认 819/2/10/100 或自定义 |
| `results[i].crc` | `calcCrc` 计算值 | 已知参考（0x89C3 等） |
| `initialized` | `TestGetInitialized()` | Init 后 1；skipInit 为 0 |
| `writeIndex` | `TestGetDtcWriteIndex()` | 0..19 循环 |
| `dtcCount` | `TestGetDtcCount()` | 0..20 封顶 |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化 — Swc_Nvm_Init**：就绪/默认校准 + 未初始化守卫（store/load/
>   readCal/writeCal 拒绝），共 4 场景。
> - **规则: DTC 持久化（SWR-CVC-030）**：store→load 读回、冻结帧模式/全零、
>   循环缓冲 20+1 回绕、越界槽位、NULL entry，共 6 场景。
> - **规则: DTC CRC 损坏检测（SWR-CVC-030）**：损坏后 load 拒绝 + 未损坏成功 +
>   越界损坏请求忽略，共 3 场景。
> - **规则: 校准数据读写（SWR-CVC-031）**：写读回自定义值、NULL 指针读/写拒绝，
>   共 3 场景。
> - **规则: 校准 CRC 损坏回退默认值（SWR-CVC-031）**：损坏回退默认 + 未损坏成功，
>   共 2 场景。
> - **规则: CRC-16 计算**：已知数据参考值、NULL 返回 0、零长度返回初值，
>   共 3 场景。
>
> 每个用例由两个阶段组构成：
> - **Given 前置阶段**（经 `存在:` → `/nvm/setup` 存储）：设置前置 NVM 状态。
>   无前置状态时存空 `phases: []`。
> - **When 刺激阶段**（`POST /api/test/asw/cvc/nvm` body）：触发被测动作。
>   服务端按「前置 + 刺激」顺序执行。
> 下表 P0..Pn 表示**刺激阶段**序列；未列出的因子取默认值。

### 规则: 初始化 — Swc_Nvm_Init

| 用例 | 阶段序列 | 期望 initialized | 期望 writeIndex/dtcCount | 关键断言 |
|---|---|---|---|---|
| `init_ready_defaults` | P0: op=readCal | 1 | 0 / 0 | readCal ret=0，校准=默认 819/2/10/100/0/1000 |
| `uninit_store_rejected` | P0: skipInit=true, op=storeDtc, dtcId=5 | 0 | 0 / 0 | storeDtc ret=1 |
| `uninit_load_rejected` | P0: skipInit=true, op=loadDtc, slot=0 | 0 | — | loadDtc ret=1 |
| `uninit_cal_rw_rejected` | P0: skipInit=true, op=readCal; P1: skipInit=true, op=writeCal | 0 | — | readCal ret=1；writeCal ret=1 |

### 规则: DTC 持久化 — SWR-CVC-030

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `dtc_store_load_roundtrip` | P0: storeDtc(dtcId=5, status=1); P1: loadDtc(slot=0) | storeDtc ret=0 slot=0；loadDtc ret=0 dtcId=5 status=1 occurrenceCount=1；writeIndex=1 dtcCount=1 |
| `dtc_freezeframe_pattern` | P0: storeDtc(dtcId=3, status=1, ffMode=1); P1: loadDtc(slot=0) | loadDtc ffHex = a0a1…bf |
| `dtc_freezeframe_null_zero` | P0: storeDtc(dtcId=7, status=1); P1: loadDtc(slot=0) | loadDtc ffHex = 32×00 |
| `dtc_circular_wrap` | P0: storeDtc(dtcId=0, repeats=20); P1: loadDtc(slot=0); P2: storeDtc(dtcId=99, status=8); P3: loadDtc(slot=0) | 首 loadDtc dtcId=0 occ=1；末 loadDtc dtcId=99 status=8 occ=21；writeIndex=1 dtcCount=20 |
| `dtc_out_of_range_slot` | P0: loadDtc(slot=20) | loadDtc ret=1 |
| `dtc_null_entry` | P0: loadDtc(slot=0, nullEntry=true) | loadDtc ret=1 |

### 规则: DTC CRC 损坏检测 — SWR-CVC-030

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `dtc_crc_corrupt_rejected` | P0: storeDtc(dtcId=5, status=1); P1: corruptDtcCrc(slot=0); P2: loadDtc(slot=0) | loadDtc ret=1（fail-closed） |
| `dtc_crc_intact_success` | P0: storeDtc(dtcId=5, status=1); P1: loadDtc(slot=0) | loadDtc ret=0 dtcId=5 |
| `dtc_corrupt_out_of_range_ignored` | P0: storeDtc(dtcId=5, status=1); P1: corruptDtcCrc(slot=20); P2: loadDtc(slot=0) | corrupt 越界被忽略，loadDtc ret=0 dtcId=5 |

### 规则: 校准数据读写 — SWR-CVC-031

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `cal_write_read_custom` | P0: writeCal(pThreshold=500, pDebounce=5, stuckThreshold=20, stuckCycles=200, lut0=10); P1: readCal | writeCal ret=0；readCal ret=0 且 500/5/20/200/10 |
| `cal_read_null_rejected` | P0: readCal(nullCal=true) | readCal ret=1 |
| `cal_write_null_rejected` | P0: writeCal(nullCal=true) | writeCal ret=1 |

### 规则: 校准 CRC 损坏回退默认值 — SWR-CVC-031

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `cal_crc_corrupt_fallback` | P0: writeCal(pThreshold=500); P1: corruptCalCrc; P2: readCal | readCal ret=1 且输出=默认 819/2/10/100/1000（fail-closed 回退） |
| `cal_crc_intact_success` | P0: writeCal(pThreshold=500); P1: readCal | readCal ret=0 pThreshold=500 |

### 规则: CRC-16 计算 — Swc_Nvm_CalcCrc16

| 用例 | 阶段序列 | 期望断言 |
|---|---|---|
| `crc_known_vector` | P0: calcCrc(dataLen=4) | crc = 0x89C3（35267） |
| `crc_null_returns_zero` | P0: calcCrc(nullCrc=true) | crc = 0 |
| `crc_zero_length_init` | P0: calcCrc(dataLen=0) | crc = 0xFFFF（65535） |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `init_ready_defaults` | 初始化后内部状态就绪且校准为默认值 |
> | `uninit_store_rejected` | 未初始化时存储 DTC 被拒绝 |
> | `uninit_load_rejected` | 未初始化时加载 DTC 被拒绝 |
> | `uninit_cal_rw_rejected` | 未初始化时读写校准被拒绝 |
> | `dtc_store_load_roundtrip` | 存储后加载读回条目且计数递增 |
> | `dtc_freezeframe_pattern` | 冻结帧按 0xA0+i 模式原样存储 |
> | `dtc_freezeframe_null_zero` | 冻结帧为 NULL 时存储全零 |
> | `dtc_circular_wrap` | 存储 20 条后循环缓冲回绕覆盖最旧条目 |
> | `dtc_out_of_range_slot` | 越界槽位加载被拒绝 |
> | `dtc_null_entry` | 空条目指针加载被拒绝 |
> | `dtc_crc_corrupt_rejected` | 存储条目 CRC 损坏后加载被拒绝 |
> | `dtc_crc_intact_success` | 未损坏条目加载始终成功 |
> | `dtc_corrupt_out_of_range_ignored` | 越界槽位 CRC 损坏请求被忽略且条目仍可加载 |
> | `cal_write_read_custom` | 写入校准后读回自定义值 |
> | `cal_read_null_rejected` | 空指针读取校准被拒绝 |
> | `cal_write_null_rejected` | 空指针写入校准被拒绝 |
> | `cal_crc_corrupt_fallback` | 校准 CRC 损坏后读取回退默认值 |
> | `cal_crc_intact_success` | 未损坏校准读取始终成功 |
> | `crc_known_vector` | 已知数据 CRC 与参考一致 |
> | `crc_null_returns_zero` | NULL 数据指针 CRC 返回 0 |
> | `crc_zero_length_init` | 零长度数据 CRC 返回初值 0xFFFF |

## 代码路径覆盖

- `Swc_Nvm_Init` 全部可执行行 ✅（L123-149）
  - 清零 20 槽 + 32B 冻结帧（dtcCount/writeIndex=0 断言 + LoadDtc 读回零条目）✅
  - 装载默认校准并计算 CRC（readCal 默认值断言）✅
  - `Nvm_Initialized = TRUE`（initialized=1 断言）✅
- `Swc_Nvm_StoreDtc` 全部可执行行 ✅（L158-204）
  - 未初始化 → E_NOT_OK（`uninit_store_rejected`）✅
  - 冻结帧 NULL → 全零（`dtc_freezeframe_null_zero`）✅
  - 冻结帧非 NULL → 原样复制（`dtc_freezeframe_pattern`）✅
  - 写索引回绕（`dtc_circular_wrap`，≥20 → 0）✅
  - 计数封顶 20（`dtc_circular_wrap` 21 条后 dtcCount=20）✅
- `Swc_Nvm_LoadDtc` 全部可执行行 ✅（L213-243）
  - 未初始化 / NULL entry / 越界槽位守卫 ✅
  - CRC 校验失败 → E_NOT_OK（`dtc_crc_corrupt_rejected`）✅
  - 校验通过 → E_OK（读回断言）✅
- `Swc_Nvm_ReadCal` 全部可执行行 ✅（L252-280）
  - 未初始化 / NULL calData 守卫 ✅
  - CRC 损坏 → 回退默认值 + E_NOT_OK（`cal_crc_corrupt_fallback`）✅
  - 校验通过 → E_OK ✅
- `Swc_Nvm_WriteCal` 全部可执行行 ✅（L289-307）
  - 未初始化 / NULL calData 守卫 ✅
  - 保存并重算 CRC → E_OK ✅
- `Swc_Nvm_CalcCrc16` 全部可执行行 ✅（L59-90）
  - NULL → 0（`crc_null_returns_zero`）✅
  - 长度 0 → 初值 0xFFFF（`crc_zero_length_init`）✅
  - 常规数据 → 0x89C3 参考（`crc_known_vector`）✅
  - 内层循环 `crc & 0x8000` 两侧 ✅
- 测试专用观测/损坏注入钩子（`#ifdef UNIT_TEST`，仅测试编译，不影响交付固件）✅
  - `TestGetInitialized` / `TestGetDtcWriteIndex` / `TestGetDtcCount` 全部被 harness 调用 ✅
  - `TestCorruptDtcCrc` 槽位内损坏（slot=0）与越界忽略（slot=20）两侧 ✅
  - `TestCorruptCalCrc` 被 `cal_crc_corrupt_fallback` 调用 ✅

> 被测功能新增 5 个 `#ifdef UNIT_TEST` 观测/损坏注入钩子（与 `Swc_Heartbeat` /
> `Swc_Watchdog` / `Swc_CanMonitor` 的观测 getter 同模式），生产固件不含。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Nvm.c.gcov.html` 实测（2026-08-16 全量套件运行后，含本 feature 21 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（163 / 163 行） |
| **分支覆盖** | **100%**（42 / 42 分支） |
| **函数覆盖** | **100%**（13 / 13 函数） |

覆盖到的函数：`Swc_Nvm_CalcCrc16`、`Nvm_ComputeDtcCrc`、`Nvm_ComputeCalCrc`、
`Swc_Nvm_Init`、`Swc_Nvm_StoreDtc`、`Swc_Nvm_LoadDtc`、`Swc_Nvm_ReadCal`、
`Swc_Nvm_WriteCal`、以及 5 个 `#ifdef UNIT_TEST` 观测/损坏注入钩子。

> 下表「实测命中」为完整套件（365 场景）运行后的累积值（本容器运行期间多次执行
> feature 的累积：21 次 harness 调用）；每次运行因容器重启会重新累积，具体数字
> 可能不同，但覆盖关系不变。

---

## 行覆盖分析（100%，163/163）

行覆盖反映**每一行是否被执行**。163 行全部覆盖，无缺口。

### 逐函数代码行覆盖映射

#### Swc_Nvm_CalcCrc16（L59-90）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L60 | 函数入口 `{` | 全部场景（每个 readCal/storeDtc/calcCrc 等均间接调用） | 286 |
| L65 | `if (data == NULL_PTR)` | true 侧：`crc_null_returns_zero`（dataLen 任意）；false 侧：其余全部 | 286 |
| L66-68 | NULL 分支：`return 0u` | `crc_null_returns_zero`（crc=0 断言） | 4 |
| L70 | `crc = NVM_CRC16_INIT` | 非 NULL 全部场景 | 282 |
| L72 | `for (i = 0u; i < length; i++)` | true 侧：`crc_known_vector` 等非零长度；false 侧：`crc_zero_length_init`（dataLen=0） | 11258 |
| L74 | `crc ^= ((uint16)data[i] << 8u)` | 非零长度全部场景 | 10976 |
| L76 | `for (j = 0u; j < 8u; j++)` | 非零长度全部场景（内层 8 次） | 98784 |
| L78 | `if ((crc & 0x8000u) != 0u)` | 两侧均覆盖（真实数据中两类位模式交替出现） | 87808 |
| L80 | `crc = (crc<<1) ^ NVM_CRC16_POLY` | true 侧 | 45712 |
| L84 | `crc = (crc << 1u)` | false 侧 | 42096 |
| L89 | `return crc` | 非 NULL 全部场景（参考值断言） | 282 |

#### Nvm_ComputeDtcCrc / Nvm_ComputeCalCrc（L96-117）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L96-104 | DTC 条目 CRC（`offsetof` 计算，padding-safe） | 每个 storeDtc/loadDtc 间接调用 | 163 |
| L110-117 | 校准块 CRC | 每个 init/writeCal/readCal 间接调用 | 111 |

#### Swc_Nvm_Init（L123-149）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L124 | 函数入口 `{` | 全部非 skipInit 场景（harness 启动自动 Init） | 75 |
| L128-129 | 清零 writeIndex/count | 全部 | 75 |
| L131 | `for (i = 0u; i < NVM_MAX_DTC_SLOTS; i++)` | 全部 Init（20 槽循环） | 1575 |
| L133-136 | 槽位清 dtcId/status/occurrenceCount/crc | 全部 Init | 1500 |
| L138 | `for (j = 0u; j < NVM_FREEZE_FRAME_SIZE; j++)` | 全部 Init（32B 冻结帧循环） | 49500 |
| L140 | `freezeFrame[j] = 0u` | 全部 Init | 48000 |
| L145-146 | 装载默认校准 + 计算 CRC | 全部 Init（readCal 默认值断言） | 75 |
| L148 | `Nvm_Initialized = TRUE` | 全部（initialized=1 断言） | 75 |

#### Swc_Nvm_StoreDtc（L158-204）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L160 | 函数入口 `{` | 全部 storeDtc 场景 | 134 |
| L164 | `if (Nvm_Initialized != TRUE)` | true 侧：`uninit_store_rejected`；false 侧：其余 | 134 |
| L166 | `return E_NOT_OK` | `uninit_store_rejected`（ret=1 断言） | 5 |
| L169 | `slot = &Nvm_DtcSlots[Nvm_DtcWriteIndex]` | 有效 store 全部 | 129 |
| L171-173 | 写 dtcId/status/occurrenceCount | 有效 store 全部（loadDtc 读回断言） | 129 |
| L176 | `for (i = 0u; i < NVM_FREEZE_FRAME_SIZE; i++)` | 有效 store 全部 | 4257 |
| L178 | `if (freezeFrame != NULL_PTR)` | true 侧：`dtc_freezeframe_pattern`；false 侧：`dtc_freezeframe_null_zero` 等 | 4128 |
| L180 | `slot->freezeFrame[i] = freezeFrame[i]` | true 侧 | 128 |
| L184 | `slot->freezeFrame[i] = 0u` | false 侧 | 4000 |
| L189 | `slot->crc = Nvm_ComputeDtcCrc(slot)` | 有效 store 全部（损坏/正常断言） | 129 |
| L192 | `Nvm_DtcWriteIndex++` | 有效 store 全部 | 129 |
| L193 | `if (Nvm_DtcWriteIndex >= NVM_MAX_DTC_SLOTS)` | true 侧：`dtc_circular_wrap` 第 21 条；false 侧：其余 | 129 |
| L195 | `Nvm_DtcWriteIndex = 0u` | 回绕 true 侧 | 5 |
| L198 | `if (Nvm_DtcCount < NVM_MAX_DTC_SLOTS)` | true 侧：前 20 条；false 侧：第 21 条起 | 129 |
| L200 | `Nvm_DtcCount++` | true 侧（dtcCount 递增断言） | 124 |
| L203 | `return E_OK` | 有效 store 全部（ret=0 断言） | 129 |

#### Swc_Nvm_LoadDtc（L213-243）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L214 | 函数入口 `{` | 全部 loadDtc 场景 | 46 |
| L217 | `if (Nvm_Initialized != TRUE)` | true 侧：`uninit_load_rejected`；false 侧：其余 | 46 |
| L219 | `return E_NOT_OK` | `uninit_load_rejected`（ret=1） | 4 |
| L222 | `if (entry == NULL_PTR)` | true 侧：`dtc_null_entry`；false 侧：其余 | 42 |
| L224 | `return E_NOT_OK` | `dtc_null_entry`（ret=1） | 4 |
| L227 | `if (slotIndex >= NVM_MAX_DTC_SLOTS)` | true 侧：`dtc_out_of_range_slot`（slot=20）；false 侧：其余 | 38 |
| L229 | `return E_NOT_OK` | `dtc_out_of_range_slot`（ret=1） | 4 |
| L232 | `*entry = Nvm_DtcSlots[slotIndex]` | 有效 load 全部 | 34 |
| L235 | `computedCrc = Nvm_ComputeDtcCrc(entry)` | 有效 load 全部 | 34 |
| L237 | `if (computedCrc != entry->crc)` | true 侧：`dtc_crc_corrupt_rejected`；false 侧：其余 | 34 |
| L239 | `return E_NOT_OK` | `dtc_crc_corrupt_rejected`（ret=1） | 5 |
| L242 | `return E_OK` | 有效 load 全部（读回断言） | 29 |

#### Swc_Nvm_ReadCal（L252-280）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L253 | 函数入口 `{` | 全部 readCal 场景 | 26 |
| L256 | `if (Nvm_Initialized != TRUE)` | true 侧：`uninit_cal_rw_rejected` P0；false 侧：其余 | 26 |
| L258 | `return E_NOT_OK` | `uninit_cal_rw_rejected` P0（ret=1） | 4 |
| L261 | `if (calData == NULL_PTR)` | true 侧：`cal_read_null_rejected`；false 侧：其余 | 22 |
| L263 | `return E_NOT_OK` | `cal_read_null_rejected`（ret=1） | 4 |
| L266 | `*calData = Nvm_CalData` | 有效 readCal 全部 | 18 |
| L269 | `computedCrc = Nvm_ComputeCalCrc(calData)` | 有效 readCal 全部 | 18 |
| L271 | `if (computedCrc != calData->crc)` | true 侧：`cal_crc_corrupt_fallback`；false 侧：其余 | 18 |
| L274-275 | 回退默认值 + 重算 CRC | `cal_crc_corrupt_fallback`（默认值断言） | 5 |
| L276 | `return E_NOT_OK` | `cal_crc_corrupt_fallback`（ret=1） | 5 |
| L279 | `return E_OK` | 有效 readCal 全部 | 13 |

#### Swc_Nvm_WriteCal（L289-307）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L290 | 函数入口 `{` | 全部 writeCal 场景 | 21 |
| L291 | `if (Nvm_Initialized != TRUE)` | true 侧：`uninit_cal_rw_rejected` P1；false 侧：其余 | 21 |
| L293 | `return E_NOT_OK` | `uninit_cal_rw_rejected` P1（ret=1） | 4 |
| L296 | `if (calData == NULL_PTR)` | true 侧：`cal_write_null_rejected`；false 侧：其余 | 17 |
| L298 | `return E_NOT_OK` | `cal_write_null_rejected`（ret=1） | 4 |
| L301 | `Nvm_CalData = *calData` | 有效 writeCal 全部 | 13 |
| L304 | `Nvm_CalData.crc = Nvm_ComputeCalCrc(&Nvm_CalData)` | 有效 writeCal 全部（readCal 读回断言） | 13 |
| L306 | `return E_OK` | 有效 writeCal 全部（ret=0） | 13 |

#### 测试专用钩子（L323-349，`#ifdef UNIT_TEST`）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L323-326 | `TestGetInitialized` | 全部场景（harness JSON `initialized`） | 88 |
| L328-331 | `TestGetDtcWriteIndex` | 全部场景（harness JSON `writeIndex`） | 261 |
| L333-336 | `TestGetDtcCount` | 全部场景（harness JSON `dtcCount`） | 127 |
| L338-344 | `TestCorruptDtcCrc(slot)` | true 侧：`dtc_crc_corrupt_rejected`（slot=0）；false 侧：`dtc_corrupt_out_of_range_ignored`（slot=20） | 7 |
| L346-349 | `TestCorruptCalCrc` | `cal_crc_corrupt_fallback` | 5 |

---

## 分支覆盖分析（100%，42/42）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `data == NULL_PTR` | CalcCrc16 L65 | ✅ 两侧 | `crc_null_returns_zero`（true）/ 其余（false） |
| `i < length` | CalcCrc16 L72 | ✅ 两侧 | 非零长度（true）/ `crc_zero_length_init` dataLen=0（false） |
| `crc & 0x8000u` | CalcCrc16 L78 | ✅ 两侧 | 真实数据两种位模式交替 |
| `i < NVM_MAX_DTC_SLOTS` | Init L131 | ✅ 两侧 | 20 槽循环终止 |
| `j < NVM_FREEZE_FRAME_SIZE` | Init L138 | ✅ 两侧 | 32B 循环终止 |
| `Nvm_Initialized != TRUE` | StoreDtc L164 | ✅ 两侧 | `uninit_store_rejected`（true）/ 其余（false） |
| `i < NVM_FREEZE_FRAME_SIZE` | StoreDtc L176 | ✅ 两侧 | 32B 复制循环终止 |
| `freezeFrame != NULL_PTR` | StoreDtc L178 | ✅ 两侧 | `dtc_freezeframe_pattern`（true）/ `dtc_freezeframe_null_zero`（false） |
| `WriteIndex >= NVM_MAX_DTC_SLOTS` | StoreDtc L193 | ✅ 两侧 | `dtc_circular_wrap` 第 21 条（true）/ 其余（false） |
| `Nvm_DtcCount < NVM_MAX_DTC_SLOTS` | StoreDtc L198 | ✅ 两侧 | 前 20 条（true）/ 第 21 条起封顶（false） |
| `Nvm_Initialized != TRUE` | LoadDtc L217 | ✅ 两侧 | `uninit_load_rejected`（true）/ 其余（false） |
| `entry == NULL_PTR` | LoadDtc L222 | ✅ 两侧 | `dtc_null_entry`（true）/ 其余（false） |
| `slotIndex >= NVM_MAX_DTC_SLOTS` | LoadDtc L227 | ✅ 两侧 | `dtc_out_of_range_slot`（true）/ 其余（false） |
| `computedCrc != entry->crc` | LoadDtc L237 | ✅ 两侧 | `dtc_crc_corrupt_rejected`（true）/ 其余（false） |
| `Nvm_Initialized != TRUE` | ReadCal L256 | ✅ 两侧 | `uninit_cal_rw_rejected` P0（true）/ 其余（false） |
| `calData == NULL_PTR` | ReadCal L261 | ✅ 两侧 | `cal_read_null_rejected`（true）/ 其余（false） |
| `computedCrc != calData->crc` | ReadCal L271 | ✅ 两侧 | `cal_crc_corrupt_fallback`（true）/ 其余（false） |
| `Nvm_Initialized != TRUE` | WriteCal L291 | ✅ 两侧 | `uninit_cal_rw_rejected` P1（true）/ 其余（false） |
| `calData == NULL_PTR` | WriteCal L296 | ✅ 两侧 | `cal_write_null_rejected`（true）/ 其余（false） |
| `slotIndex < NVM_MAX_DTC_SLOTS` | TestCorruptDtcCrc L340 | ✅ 两侧 | `dtc_crc_corrupt_rejected` slot=0（true）/ `dtc_corrupt_out_of_range_ignored` slot=20（false） |

---

## 无法覆盖的代码说明

**无。** 行覆盖 163/163、分支覆盖 42/42、函数覆盖 13/13 全部 100%。

> 与 `Swc_Watchdog`（93.5% 行）和 `Swc_Scheduler`（92.5% 行）不同，本模块
> 不存在「经公开 API 不可达的防御性守卫」：所有守卫（未初始化 / NULL 指针 /
> 越界 / CRC 损坏）均可经公开 API 或测试专用损坏注入钩子直接构造并断言。
> 新增的 5 个 `#ifdef UNIT_TEST` 钩子仅存在于测试编译产物中，交付固件零改动。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（163/163） | 0 | — |
| 分支 | 100%（42/42） | 0 | — |
| 函数 | 100%（13/13） | 0 | — |
