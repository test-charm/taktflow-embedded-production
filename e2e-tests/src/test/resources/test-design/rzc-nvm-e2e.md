# RZC NVM 存储 (Swc_RzcNvm) E2E 测试设计

## 被测功能

**RZC NVM 存储 SWC — DTC 持久化（SWR-RZC-030）**

- **DTC 持久化（SWR-RZC-030）**：20-slot 循环缓冲存储 DTC 条目（DTC ID、
  状态字节、时间戳、6 字段冻结帧：电机电流/温度/转速、电池电压、扭矩命令、
  车辆状态），每条目以 CRC-16/CCITT（0x1021、初值 0xFFFF）保护。CRC 覆盖
  条目中除 crc16 字段外的全部字节（用 `offsetof` 避开结构体 padding）。
- **写索引循环缓冲**：`StoreDtc` 写入当前写索引槽，随后写索引 +1，达到 20
  时回绕为 0（覆盖最旧条目，与 CVC `Swc_Nvm` 相同，区别于 FZC 的首空槽
  模型）。`GetWriteIndex` 为公开 API 可直接观测。
- **加载 CRC 校验（fail-closed）**：`LoadDtc` 对未初始化 / NULL 输出指针 /
  越界槽位 / CRC 损坏全部返回 E_NOT_OK，绝不返回损坏数据。空槽位（Init 后
  全零、crc16=0）因计算 CRC ≠ 0 而自然被拒绝。
- **未初始化守卫**：Init 未调用时 StoreDtc / LoadDtc 返回 E_NOT_OK。

覆盖链路：

```text
测试 API 注入（op / skipInit / repeats / dtcId / status / timestamp /
  motorCurrentMa / motorTempDdc / motorSpeedRpm / batteryMv / torqueCmdPct /
  vehicleState / slot / nullFreeze / nullEntry / dataLen）
  → Swc_RzcNvm_Init()（SWR-RZC-030 前置）：
       · 清零 20 槽 DTC 条目（含 6 字段冻结帧）
       · 写索引 = 0；Initialized = TRUE
  → Swc_RzcNvm_StoreDtc(dtcId, status, timestamp, pFreeze)（SWR-RZC-030）：
       · 未初始化 → E_NOT_OK
       · pFreeze == NULL → E_NOT_OK
       · 写入当前写索引槽（6 字段冻结帧原样复制）
       · 计算并保存条目 CRC（offsetof 防 padding 偏移）
       · 写索引 +1，≥20 回绕为 0
       · E_OK
  → Swc_RzcNvm_LoadDtc(slotIndex, pEntry)（SWR-RZC-030）：
       · 未初始化 / NULL pEntry / slotIndex≥20 → E_NOT_OK
       · CRC 校验失败 → E_NOT_OK（fail-closed）
       · 校验通过 → 复制条目（含 crc16）→ E_OK
  → Swc_RzcNvm_GetWriteIndex()：
       · 返回当前写索引（0..19）
  → 观测（harness 输出）：results[] 每操作结果 + initialized / writeIndex
```

与既有 ASW E2E（`Swc_FzcNvm`、`Swc_Nvm` 等）一致，通过测试专用 API 在原生
测试框架内执行真实的 `Swc_RzcNvm.c` 生产代码。由于 `Swc_RzcNvm` 的存储槽位
与初始化标志均为 `static` 文件作用域状态，且 CRC 损坏分支无法经公开 API
直接构造，参照 `Swc_Nvm` / `Swc_FzcNvm` 的既有做法，在
`Swc_RzcNvm.c/.h` 增加 **UNIT_TEST 保护的观测/损坏注入 getter**（仅测试
编译，不影响交付固件）：

- `Swc_RzcNvm_TestGetInitialized()` — 观测静态初始化标志；
- `Swc_RzcNvm_TestCorruptDtcCrc(slot)` — 翻转已存条目 CRC，驱动 LoadDtc 的
  损坏检测分支（越界槽位请求被忽略）；
- `Swc_RzcNvm_TestCrc16(data, length)` — 暴露静态 `RzcNvm_Crc16`，验证
  已知向量（与 CVC/FZC 同款 0x89C3）与零长度返回初值 0xFFFF。

> **被测代码观测**：生产固件（STM32/TMS570/POSIX）不定义 `UNIT_TEST`，上述
> getter 与损坏注入钩子绝不进入交付固件。写索引经公开 `GetWriteIndex` 观测；
> DTC 条目内容经公开 `LoadDtc` 读回（含 crc16 字段）。

## 被测代码流程图

### RzcNvm_Crc16（L53-L79）

```text
[Crc16(data, length)]
  ═══→ [crc = 0xFFFF]
  ═══→ [for i in 0..length)
           → [crc ^= data[i]<<8]
           → [for bit in 0..8)
                  {crc & 0x8000?}
                     ├─ Y → [crc = (crc<<1) ^ 0x1021]
                     └─ N → [crc = crc<<1]
  ═══→ [return crc]
```

### RzcNvm_ComputeEntryCrc（L86-L97）

```text
[ComputeEntryCrc(pEntry)]
  ═══→ [dataLen = offsetof(entry, crc16)]
  ═══→ [return Crc16(pEntry, dataLen)]
```

### Swc_RzcNvm_Init（L103-L121）

```text
[Init]
  ═══→ [for i in 0..20) 零填充槽位全部字节]
  ═══→ [WriteIndex = 0]
  ═══→ [Initialized = TRUE]
```

### Swc_RzcNvm_StoreDtc（L127-L170）

```text
[StoreDtc]
  ═══→ {Initialized?} ─N→ [return E_NOT_OK]
   ↓ Y
  {pFreeze == NULL?} ─Y→ [return E_NOT_OK]
   ↓ N
  [pSlot = &Storage[WriteIndex]]
  [写入 dtc_id/status/timestamp]
  [复制 6 字段冻结帧]
  [pSlot->crc16 = ComputeEntryCrc(pSlot)]
  [WriteIndex++]
  {WriteIndex >= 20?} ─Y→ [WriteIndex = 0]
   ↓ N（或已回绕）
  [return E_OK]
```

### Swc_RzcNvm_LoadDtc（L176-L219）

```text
[LoadDtc]
  ═══→ {Initialized?} ─N→ [return E_NOT_OK]
   ↓ Y
  {pEntry == NULL?} ─Y→ [return E_NOT_OK]
   ↓ N
  {slotIndex >= 20?} ─Y→ [return E_NOT_OK]
   ↓ N
  [expected = ComputeEntryCrc(pSlot)]
  {expected != pSlot->crc16?} ─Y→ [return E_NOT_OK]
   ↓ N（CRC 通过）
  [逐字节复制条目到输出]
  [return E_OK]
```

### Swc_RzcNvm_GetWriteIndex（L225-L228）

```text
[GetWriteIndex]
  ═══→ [return WriteIndex]
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `storeDtc` / `loadDtc` / `corruptDtcCrc` / `calcCrc` | When — 执行控制 |
| `skipInit` | 首阶段是否跳过自动 Init | `false`、`true` | When — 执行控制 |
| `repeats` | `storeDtc` 重复次数 | `1`、`20`（回绕边界） | When — 载荷 |
| `dtcId` | DTC ID | `0`、`5`、`99` | When — 载荷 |
| `status` | DTC 状态字节 | `0`、`1`、`8` | When — 载荷 |
| `timestamp` | 存储时系统 tick | `0`、`1000` | When — 载荷 |
| `motorCurrentMa` | 冻结帧电机电流 | `0`、`25000` | When — 载荷 |
| `motorTempDdc` | 冻结帧电机温度 | `-40`、`85` | When — 载荷 |
| `motorSpeedRpm` | 冻结帧电机转速 | `0`、`6000` | When — 载荷 |
| `batteryMv` | 冻结帧电池电压 | `0`、`12000` | When — 载荷 |
| `torqueCmdPct` | 冻结帧扭矩命令 | `-100`、`100` | When — 载荷 |
| `vehicleState` | 冻结帧车辆状态 | `0`、`3` | When — 载荷 |
| `slot` | `loadDtc`/`corruptDtcCrc` 槽位 | `0`、`19`、`20`（越界边界） | When — 执行控制 |
| `nullFreeze` | `storeDtc` 传 NULL_PTR | `false`、`true` | When — 守卫 |
| `nullEntry` | `loadDtc` 传 NULL_PTR | `false`、`true` | When — 守卫 |
| `dataLen` | `calcCrc` 输入长度 | `0`（边界）、`4`（常规） | When — 载荷 |
| 阶段序列 | 多阶段路径 | init→load、store→load、store→corrupt→load、store×20→store→load | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].ret` | 每步返回值 | `0`=E_OK、`1`=E_NOT_OK |
| `results[i].dtcId/status/timestamp/freeze*` | 读回 DTC 条目 | 与存储值一致或拒绝 |
| `results[i].crc16` | 读回条目 CRC | 与存储时计算值一致 |
| `results[i].crc` | `calcCrc` 返回值 | `35267 / 65535` |
| `initialized` | `TestGetInitialized()` | Init 后 1，skipInit 时 0 |
| `writeIndex` | `GetWriteIndex()` | 0..19，随 StoreDtc 推进并回绕 |

## 测试用例

> 用例按“最短路径优先”逐步导出；名称突出区别于前一用例的因子取值。

### 规则: 初始化与未初始化守卫 — Swc_RzcNvm_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_ready_defaults` | P0: init; P1: loadDtc(0) | initialized=1；writeIndex=0；空槽 loadDtc ret=1（全零槽 CRC 校验失败） |
| `uninit_store_rejected` | P0: skipInit=true, storeDtc(dtcId=5) | initialized=0；writeIndex=0；ret=1 |
| `uninit_load_rejected` | P0: skipInit=true, loadDtc(slot=0) | initialized=0；ret=1 |

### 规则: DTC 持久化 — SWR-RZC-030

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `dtc_store_load_roundtrip` | P0: storeDtc(5,1,1000,...); P1: loadDtc(0) | ret=0；dtcId=5/status=1/timestamp=1000；writeIndex=1 |
| `dtc_freeze_frame_full_roundtrip` | P0: storeDtc(3,8,555,全部 6 字段自定义); P1: loadDtc(0) | 6 字段逐一匹配 |
| `dtc_write_index_advances` | P0: storeDtc(dtcId=1); P1: storeDtc(dtcId=2); P2: loadDtc(1) | writeIndex=2；slot1 读到 dtcId=2 |
| `dtc_circular_wrap_after_20` | P0: storeDtc(dtcId=0,repeats=20); P1: storeDtc(dtcId=99,status=8); P2: loadDtc(0) | 20 次后 writeIndex 回绕 0；第 21 条覆盖 slot0，loadDtc(0)=99 |
| `dtc_null_freeze_rejected` | P0: storeDtc(dtcId=5,nullFreeze=true) | ret=1 |
| `dtc_load_out_of_range` | P0: loadDtc(slot=20) | ret=1 |
| `dtc_load_null_entry` | P0: loadDtc(slot=0,nullEntry=true) | ret=1 |
| `dtc_empty_slot_rejected` | P0: loadDtc(slot=0) | ret=1（Init 后全零槽 CRC 校验失败） |

### 规则: DTC CRC 损坏检测 — SWR-RZC-030

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `dtc_crc_corrupt_rejected` | P0: storeDtc(...); P1: corruptDtcCrc(0); P2: loadDtc(0) | ret=1 |
| `dtc_crc_intact_success` | P0: storeDtc(...); P1: loadDtc(0) | ret=0，条目字段匹配 |
| `dtc_crc_corrupt_out_of_range_ignored` | P0: storeDtc(...); P1: corruptDtcCrc(20); P2: loadDtc(0) | 越界损坏请求被忽略，slot0 仍可正常加载 |

### 规则: CRC-16 计算 — RzcNvm_Crc16

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `crc_known_vector` | P0: calcCrc(dataLen=4) | crc=35267（0x89C3，与 CVC/FZC 同款已知向量） |
| `crc_zero_length_init` | P0: calcCrc(dataLen=0) | crc=65535（0xFFFF，初值） |

## 代码路径覆盖

- `RzcNvm_Crc16`：位循环两个分支 + 已知向量/零长度路径全部纳入用例。
- `RzcNvm_ComputeEntryCrc`：由每次 StoreDtc 计算 CRC 与每次 LoadDtc 校验
  CRC 覆盖。
- `Swc_RzcNvm_Init`：默认初始化、清槽、写索引复位、初始化标志置位全部覆盖。
- `Swc_RzcNvm_StoreDtc`：未初始化、NULL 冻结帧、正常写入、写索引推进、
  20 槽回绕全部覆盖。
- `Swc_RzcNvm_LoadDtc`：未初始化 / NULL 输出 / 越界 / CRC 损坏 / 成功复制
  五条路径全部覆盖。
- `Swc_RzcNvm_GetWriteIndex`：所有用例经 harness 输出观测。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`Swc_RzcNvm.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（117 / 117）** |
| 分支覆盖 | **100%（28 / 28）** |
| 函数覆盖 | **100%（9 / 9）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/rzc_nvm.feature` | **16 scenarios / 96 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **586 scenarios / 3545 steps passed** |

函数命中次数（`Swc_RzcNvm.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `RzcNvm_Crc16` | 80 |
| `RzcNvm_ComputeEntryCrc` | 76 |
| `Swc_RzcNvm_Init` | 32 |
| `Swc_RzcNvm_StoreDtc` | 61 |
| `Swc_RzcNvm_LoadDtc` | 25 |
| `Swc_RzcNvm_GetWriteIndex` | 117 |
| `Swc_RzcNvm_TestGetInitialized` | 33 |
| `Swc_RzcNvm_TestCorruptDtcCrc` | 4 |
| `Swc_RzcNvm_TestCrc16` | 4 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/rzc/src/Swc_RzcNvm.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### RzcNvm_Crc16（L53-L79）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L54 | `{` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L55 | `uint16 crc;` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L56 | `uint16 i;` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L57 | `uint8  bit;` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L59 | `crc = RZC_NVM_CRC16_INIT;` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L61 | `for (i = 0u; i < length; i++)` | 1608 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L63 | `crc ^= (uint16)((uint16)data[i] << 8u);` | 1528 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L65 | `for (bit = 0u; bit < 8u; bit++)` | 13752 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L67 | `if ((crc & 0x8000u) != 0u)` | 12224 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L69 | `crc = (uint16)((uint16)(crc << 1u) ^ RZC_NVM_CRC16_POLY);` | 6150 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L73 | `crc = (uint16)(crc << 1u);` | 6074 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |
| L78 | `return crc;` | 80 | `crc_known_vector`、`crc_zero_length_init`；且被 StoreDtc/LoadDtc 计算与校验 CRC 间接复用 |

#### RzcNvm_ComputeEntryCrc（L86-L97）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L87 | `{` | 76 | `dtc_store_load_roundtrip`、`dtc_freeze_frame_full_roundtrip`、`dtc_write_index_advances`、`dtc_circular_wrap_after_20`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored` |
| L92 | `uint16 dataLen;` | 76 | 同上 |
| L94 | `dataLen = (uint16)offsetof(...);` | 76 | 同上 |
| L96 | `return RzcNvm_Crc16(...);` | 76 | 同上 |

#### Swc_RzcNvm_Init（L103-L121）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L104 | `{` | 32 | `init_ready_defaults`、`dtc_store_load_roundtrip`、`dtc_freeze_frame_full_roundtrip`、`dtc_write_index_advances`、`dtc_circular_wrap_after_20`、`dtc_crc_corrupt_rejected` 等所有含 Init 的用例 |
| L105 | `uint8 i;` | 32 | 同上 |
| L106 | `uint8 j;` | 32 | 同上 |
| L107 | `uint8 *ptr;` | 32 | 同上 |
| L109 | `for (i = 0u; i < RZC_NVM_DTC_MAX_SLOTS; i++)` | 672 | 同上 |
| L112 | `ptr = (uint8 *)&RzcNvm_Storage[i];` | 640 | 同上 |
| L113 | `for (j = 0u; j < (uint8)sizeof(...); j++)` | 16000 | 同上 |
| L115 | `ptr[j] = 0u;` | 15360 | 同上 |
| L119 | `RzcNvm_WriteIndex = 0u;` | 32 | 同上 |
| L120 | `RzcNvm_Initialized = TRUE;` | 32 | 同上 |

#### Swc_RzcNvm_StoreDtc（L127-L170）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L131 | `{` | 61 | `uninit_store_rejected`、`dtc_store_load_roundtrip`、`dtc_freeze_frame_full_roundtrip`、`dtc_write_index_advances`、`dtc_circular_wrap_after_20`、`dtc_null_freeze_rejected`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored` |
| L132 | `Swc_RzcNvm_DtcEntryType *pSlot;` | 61 | 同上 |
| L134 | `if (RzcNvm_Initialized != TRUE)` | 61 | true 侧 `uninit_store_rejected`；false 侧所有含 Init 的正常存储 |
| L136 | `return E_NOT_OK;` | 2 | `uninit_store_rejected` |
| L139 | `if (pFreeze == NULL_PTR)` | 59 | true 侧 `dtc_null_freeze_rejected`；false 侧所有正常存储 |
| L141 | `return E_NOT_OK;` | 2 | `dtc_null_freeze_rejected` |
| L145 | `pSlot = &RzcNvm_Storage[RzcNvm_WriteIndex];` | 57 | 所有正常存储用例 |
| L147 | `pSlot->dtc_id = dtcId;` | 57 | 所有正常存储用例 |
| L148 | `pSlot->status = status;` | 57 | 所有正常存储用例 |
| L149 | `pSlot->timestamp = timestamp;` | 57 | 所有正常存储用例 |
| L152 | `pSlot->freeze_frame.motor_current_ma = ...;` | 57 | `dtc_freeze_frame_full_roundtrip`（自定义值）+ 其余正常存储 |
| L153 | `pSlot->freeze_frame.motor_temp_ddc = ...;` | 57 | 同上 |
| L154 | `pSlot->freeze_frame.motor_speed_rpm = ...;` | 57 | 同上 |
| L155 | `pSlot->freeze_frame.battery_mv = ...;` | 57 | 同上 |
| L156 | `pSlot->freeze_frame.torque_cmd_pct = ...;` | 57 | 同上 |
| L157 | `pSlot->freeze_frame.vehicle_state = ...;` | 57 | 同上 |
| L160 | `pSlot->crc16 = RzcNvm_ComputeEntryCrc(pSlot);` | 57 | 所有正常存储用例 |
| L163 | `RzcNvm_WriteIndex++;` | 57 | 所有正常存储用例 |
| L164 | `if (RzcNvm_WriteIndex >= RZC_NVM_DTC_MAX_SLOTS)` | 57 | true 侧 `dtc_circular_wrap_after_20`（第 20 次存储触发回绕）；false 侧其余存储 |
| L166 | `RzcNvm_WriteIndex = 0u;` | 2 | `dtc_circular_wrap_after_20` |
| L169 | `return E_OK;` | 57 | 所有正常存储用例 |

#### Swc_RzcNvm_LoadDtc（L176-L219）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L178 | `{` | 25 | `init_ready_defaults`、`uninit_load_rejected`、`dtc_store_load_roundtrip`、`dtc_freeze_frame_full_roundtrip`、`dtc_write_index_advances`、`dtc_circular_wrap_after_20`、`dtc_load_out_of_range`、`dtc_load_null_entry`、`dtc_empty_slot_rejected`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored` |
| L179 | `const Swc_RzcNvm_DtcEntryType *pSlot;` | 25 | 同上 |
| L180 | `uint16 expected_crc;` | 25 | 同上 |
| L181 | `uint8 j;` | 25 | 同上 |
| L182 | `const uint8 *src;` | 25 | 同上 |
| L183 | `uint8 *dst;` | 25 | 同上 |
| L185 | `if (RzcNvm_Initialized != TRUE)` | 25 | true 侧 `uninit_load_rejected`；false 侧其余加载 |
| L187 | `return E_NOT_OK;` | 2 | `uninit_load_rejected` |
| L190 | `if (pEntry == NULL_PTR)` | 23 | true 侧 `dtc_load_null_entry`；false 侧其余加载 |
| L192 | `return E_NOT_OK;` | 2 | `dtc_load_null_entry` |
| L195 | `if (slotIndex >= RZC_NVM_DTC_MAX_SLOTS)` | 21 | true 侧 `dtc_load_out_of_range`；false 侧其余加载 |
| L197 | `return E_NOT_OK;` | 2 | `dtc_load_out_of_range` |
| L200 | `pSlot = &RzcNvm_Storage[slotIndex];` | 19 | 其余加载用例 |
| L203 | `expected_crc = RzcNvm_ComputeEntryCrc(pSlot);` | 19 | 其余加载用例 |
| L205 | `if (expected_crc != pSlot->crc16)` | 19 | true 侧 `init_ready_defaults`（空槽全零 CRC 校验失败）、`dtc_empty_slot_rejected`、`dtc_crc_corrupt_rejected`；false 侧所有 CRC 完好加载 |
| L207 | `return E_NOT_OK;` | 6 | `init_ready_defaults`、`dtc_empty_slot_rejected`、`dtc_crc_corrupt_rejected` |
| L211 | `src = (const uint8 *)pSlot;` | 13 | CRC 校验通过的成功加载用例 |
| L212 | `dst = (uint8 *)pEntry;` | 13 | 同上 |
| L213 | `for (j = 0u; j < (uint8)sizeof(...); j++)` | 325 | `dtc_store_load_roundtrip`、`dtc_freeze_frame_full_roundtrip`、`dtc_write_index_advances`、`dtc_circular_wrap_after_20`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored` |
| L215 | `dst[j] = src[j];` | 312 | 同上 |
| L218 | `return E_OK;` | 13 | 同上 |

#### Swc_RzcNvm_GetWriteIndex（L225-L228）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L226 | `{` | 117 | 全部用例（harness 每阶段与最终输出均调用） |
| L227 | `return RzcNvm_WriteIndex;` | 117 | 全部用例 |

#### UNIT_TEST 钩子（L235-L251，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L236 | `Swc_RzcNvm_TestGetInitialized` 函数体 | 33 | 全部用例（harness 最终输出观测 initialized） |
| L241 | `Swc_RzcNvm_TestCorruptDtcCrc` 函数体 | 4 | true 侧 `dtc_crc_corrupt_rejected`（slot 0）；false 侧 `dtc_crc_corrupt_out_of_range_ignored`（slot 20） |
| L244 | `RzcNvm_Storage[slotIndex].crc16 ^= 0xFFFFu;` | 2 | `dtc_crc_corrupt_rejected` |
| L249 | `Swc_RzcNvm_TestCrc16` 函数体 | 4 | `crc_known_vector`、`crc_zero_length_init` |

### 分支覆盖分析

- `RzcNvm_Crc16`：`(crc & 0x8000u) != 0u` true/false 均覆盖（L67，6150/6074）；
  内外层循环进入/退出两侧均覆盖（L61/L65）。
- `Swc_RzcNvm_StoreDtc`：未初始化守卫（L134 true 侧 2 次 / false 侧 59 次）、
  NULL 冻结帧守卫（L139 true 侧 2 次 / false 侧 57 次）、写索引回绕
  （L164 true 侧 2 次 / false 侧 55 次）全部两侧覆盖。
- `Swc_RzcNvm_LoadDtc`：未初始化（L185）、NULL 输出（L190）、越界（L195）、
  CRC 校验（L205）四个守卫全部两侧覆盖。
- `Swc_RzcNvm_TestCorruptDtcCrc`：`slotIndex < RZC_NVM_DTC_MAX_SLOTS`
  越界防护两侧均覆盖（L242）。

## 无法覆盖的代码说明

> 本模块不存在编译期排除分支（无 `#ifdef PLATFORM_HIL` / `SIL_DIAG`）。
> **117/117 行、28/28 分支、9/9 函数全部被端到端测试覆盖，无无法覆盖的
> 可执行代码**。
>
> 唯一设计性说明：`RzcNvm_Crc16` 是 `static` 内部函数，无 NULL 参数守卫
> （仅在 `RzcNvm_ComputeEntryCrc` 内部以非空指针调用），因此不设计 NULL
> 输入用例（区别于公开 CRC API 的 CVC `Swc_Nvm_CalcCrc16` / FZC
> `Swc_FzcNvm_Crc16` 的 NULL→0 行为）；其已知向量与零长度行为经
> `Swc_RzcNvm_TestCrc16` 测试钩子验证，其余全部行经真实 StoreDtc/LoadDtc
> 调用链覆盖。
