# FZC NVM 存储 (Swc_FzcNvm) E2E 测试设计

## 被测功能

**FZC NVM 存储 SWC — DTC 持久化与舵机/制动/激光雷达校准存储（SWR-FZC-031、
SWR-FZC-032）**

- **DTC 持久化（SWR-FZC-031）**：20 个静态槽位，`StoreDtc` 总是写入首个空槽；
  `LoadDtc` 读取时校验 CRC-16/CCITT（0x1021，初值 0xFFFF），损坏即拒绝返回。
- **校准数据（SWR-FZC-032）**：转向中心偏移、转向增益、制动偏移/增益、
  激光雷达 warn/brake/emergency 阈值，写入时重算 CRC，读取时 CRC 损坏回退默认值。
- **Init/NvM 后端重载**：`Swc_FzcNvm_Init` 先清 RAM 镜像并装载默认值，再从
  `NvM_ReadBlock(0/1)` 重载后端；校准 CRC 不合法则在 Init 内部立即回退默认值。
- **未初始化守卫**：Init 未调用时 StoreDtc/LoadDtc/StoreCal 返回 E_NOT_OK；
  `LoadCal` 返回默认值并返回 E_NOT_OK。
- **公开 CRC API**：`Swc_FzcNvm_Crc16` 的 NULL/零长度/已知向量行为必须稳定。

覆盖链路：

```text
测试 API 注入（op / skipInit / repeats / dtcId / steerAngle / brakePos /
  lidarDist / slot / nullRecord / nullCal / steerCenterOffset / steerGain /
  brakePosOffset / brakeGain / lidarWarnCm / lidarBrakeCm / lidarEmergencyCm /
  dataLen / nullCrc）
  → Swc_FzcNvm_Init()
       · 清空 20 个 DTC RAM 槽位
       · 装载默认校准并计算 CRC
       · NvM_ReadBlock(0): 重载 DTC 镜像
       · NvM_ReadBlock(1): 重载校准镜像
       · 若后端校准 CRC 错误，立即回退默认值
  → Swc_FzcNvm_StoreDtc(dtcId, steerAngle, brakePos, lidarDist)
       · 未初始化 → E_NOT_OK
       · 找首个空槽；满槽 → E_NOT_OK
       · 状态强制 ACTIVE，写入冻结帧并计算 CRC
       · NvM_WriteBlock(0) 持久化整块 DTC 镜像
  → Swc_FzcNvm_LoadDtc(slot, record)
       · NULL / 越界 / 未初始化 / 空槽 → E_NOT_OK
       · CRC 错误 → E_NOT_OK（fail-closed）
       · 正常 → 复制记录并返回 E_OK
  → Swc_FzcNvm_LoadCal(cal)
       · NULL → E_NOT_OK
       · 未初始化 → 输出默认值 + E_NOT_OK
       · CRC 错误 → 输出默认值 + E_NOT_OK
       · 正常 → 返回当前校准
  → Swc_FzcNvm_StoreCal(cal)
       · NULL / 未初始化 → E_NOT_OK
       · 拷贝字段、重算 CRC、NvM_WriteBlock(1) 持久化
  → Swc_FzcNvm_Crc16(data, length)
       · data == NULL → 0
       · length == 0 → 0xFFFF
       · 常规路径 → CRC-16/CCITT
  → 观测（harness 输出）
       · results[] 每步返回值/读回数据
       · initialized（UNIT_TEST getter）
       · occupiedSlots（harness 通过公开 LoadDtc 统计）
       · backendReadCount / backendWriteCount（NvM mock 计数）
```

> 被测模块内部状态均为 `static` 文件作用域。为观测初始化标志并驱动运行期 CRC
> 损坏分支，增加 **UNIT_TEST 保护的测试钩子**：
> - `Swc_FzcNvm_TestGetInitialized()`
> - `Swc_FzcNvm_TestCorruptDtcCrc(slot)`
> - `Swc_FzcNvm_TestCorruptCalCrc()`
>
> Init 阶段的“后端校准 CRC 损坏”分支不依赖生产代码额外钩子，而由 harness 的
> `NvM_ReadBlock` mock 后端配合 `corruptBackendCalCrc` 操作驱动。

## 被测代码流程图

### Swc_FzcNvm_Init（L144-L175）

```text
[Init]
  ═══→ [for 20 槽清空 dtcId/status/freeze*/crc]
  ═══→ [ApplyCalDefaults]
  ═══→ [NvM_ReadBlock(0, dtc slots)]
  ═══→ [NvM_ReadBlock(1, cal)]
  ═══→ {cal.crc == FzcNvm_CalDataCrc(cal)?}
          ├─ Y → [initialized = TRUE]
          └─ N → [ApplyCalDefaults] → [initialized = TRUE]
```

### Swc_FzcNvm_StoreDtc（L181-L220）

```text
[StoreDtc]
  ═══→ {initialized?}
          ├─ N → [return E_NOT_OK]
          └─ Y → [扫描首个空槽]
                     ↓ {找到空槽?}
                         ├─ N → [return E_NOT_OK]
                         └─ Y → [填充 dtcId/status/freeze*]
                                  → [计算 record CRC]
                                  → [NvM_WriteBlock(0)]
                                  → [return E_OK]
```

### Swc_FzcNvm_LoadDtc（L226-L262）

```text
[LoadDtc]
  ═══→ {record == NULL?} ─Y→ [return E_NOT_OK]
   ↓ N
  {index 越界?} ─Y→ [return E_NOT_OK]
   ↓ N
  {initialized?} ─N→ [return E_NOT_OK]
   ↓ Y
  {slot empty?} ─Y→ [return E_NOT_OK]
   ↓ N
  {crc valid?} ─N→ [return E_NOT_OK]
   ↓ Y
  [复制记录] → [return E_OK]
```

### Swc_FzcNvm_LoadCal（L269-L300）

```text
[LoadCal]
  ═══→ {cal == NULL?} ─Y→ [return E_NOT_OK]
   ↓ N
  {initialized?} ─N→ [ApplyCalDefaults(out)] → [return E_NOT_OK]
   ↓ Y
  {ram cal crc valid?}
     ├─ N → [ApplyCalDefaults(out)] → [return E_NOT_OK]
     └─ Y → [复制当前校准] → [return E_OK]
```

### Swc_FzcNvm_StoreCal（L307-L331）

```text
[StoreCal]
  ═══→ {cal == NULL?} ─Y→ [return E_NOT_OK]
   ↓ N
  {initialized?} ─N→ [return E_NOT_OK]
   ↓ Y
  [复制 7 个字段]
    → [重算 CRC]
    → [NvM_WriteBlock(1)]
    → [return E_OK]
```

### Swc_FzcNvm_Crc16（L52-L75）

```text
[Crc16]
  ═══→ {data == NULL?}
          ├─ Y → [return 0]
          └─ N → [crc=0xFFFF]
                   → [按字节循环]
                     ↓ [按 bit 循环]
                     ↓ {crc & 0x8000}
                         ├─ Y → [左移并异或 0x1021]
                         └─ N → [仅左移]
                   → [return crc]
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `storeDtc` / `loadDtc` / `readCal` / `writeCal` / `corruptDtcCrc` / `corruptCalCrc` / `corruptBackendCalCrc` / `calcCrc` | When — 执行控制 |
| `skipInit` | 首阶段是否跳过自动 Init | `false`、`true` | When — 执行控制 |
| `repeats` | `storeDtc` 重复次数 | `1`、`20`（满槽边界） | When — 载荷 |
| `dtcId` | DTC ID | `0`、`5`、`99` | When — 载荷 |
| `steerAngle` | 冻结帧转向角 | `-20`、`0`、`20` | When — 载荷 |
| `brakePos` | 冻结帧制动位置 | `0`、`30`、`50` | When — 载荷 |
| `lidarDist` | 冻结帧激光距离 | `0`、`80`、`120`、`200` | When — 载荷 |
| `slot` | `loadDtc`/`corruptDtcCrc` 槽位 | `0`、`19`、`20`（越界边界） | When — 执行控制 |
| `nullRecord` | `loadDtc` 传 NULL_PTR | `false`、`true` | When — 守卫 |
| `nullCal` | `readCal`/`writeCal` 传 NULL_PTR | `false`、`true` | When — 守卫 |
| 校准 7 字段 | 自定义校准输入 | 默认值、非默认自定义值 | When — 载荷 |
| `dataLen` | CRC 输入长度 | `0`（边界）、`4`（常规） | When — 载荷 |
| `nullCrc` | CRC 输入指针是否为 NULL | `false`、`true` | When — 守卫 |
| 阶段序列 | 多阶段路径 | init→read、write→init→read、store→corrupt→load、write→corrupt→read | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].ret` | 每步返回值 | `0`=E_OK、`1`=E_NOT_OK |
| `results[i].dtcId/status/freeze*` | 读回 DTC 记录 | 与存储值一致或拒绝 |
| `results[i].steerCenterOffset...` | 读回校准 | 默认值或自定义值 |
| `results[i].crc` | `calcCrc` 返回值 | `35267 / 0 / 65535` |
| `initialized` | `TestGetInitialized()` | Init 后 1，skipInit 时 0 |
| `occupiedSlots` | 已占用且 CRC 有效的槽位数 | `0 / 1 / 20` |
| `backendReadCount` | `NvM_ReadBlock` 调用次数 | 每次 Init 增加 2 |
| `backendWriteCount` | `NvM_WriteBlock` 调用次数 | store/writeCal 成功时增加 |

## 测试用例

### 规则: 初始化与未初始化守卫 — Swc_FzcNvm_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_ready_defaults` | P0: readCal | initialized=1；occupiedSlots=0；默认校准 0/100/0/100/100/50/20 |
| `init_reload_custom_cal` | P0: writeCal(custom); P1: init; P2: readCal | 重新 Init 后仍读回 custom 校准 |
| `init_corrupt_backend_fallback` | P0: writeCal(custom); P1: corruptBackendCalCrc; P2: init; P3: readCal | Init 检测后端 CRC 损坏并回退默认值 |
| `uninit_store_rejected` | P0: skipInit=true, storeDtc | ret=1 |
| `uninit_load_rejected` | P0: skipInit=true, loadDtc(slot=0) | ret=1 |
| `uninit_read_defaults_not_ok` | P0: skipInit=true, readCal | ret=1 且输出默认值 |
| `uninit_write_rejected` | P0: skipInit=true, writeCal | ret=1 |

### 规则: DTC 持久化 — SWR-FZC-031

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `dtc_store_reload_roundtrip` | P0: storeDtc(5,-20,50,120); P1: init; P2: loadDtc(0) | 重新 Init 后仍读回 DTC 与冻结帧 |
| `dtc_empty_slot_rejected` | P0: loadDtc(0) | ret=1 |
| `dtc_full_rejects_21st` | P0: storeDtc(dtcId=0,repeats=20); P1: storeDtc(99,...); P2: loadDtc(19) | 第 21 条 ret=1；occupiedSlots=20 |
| `dtc_null_record_rejected` | P0: loadDtc(0,nullRecord=true) | ret=1 |
| `dtc_index_out_of_range` | P0: loadDtc(20) | ret=1 |

### 规则: DTC CRC 损坏检测 — SWR-FZC-031

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `dtc_crc_corrupt_rejected` | P0: storeDtc(...); P1: corruptDtcCrc(0); P2: loadDtc(0) | ret=1 |
| `dtc_crc_intact_success` | P0: storeDtc(...); P1: loadDtc(0) | ret=0，记录值正确 |
| `dtc_crc_corrupt_out_of_range_ignored` | P0: storeDtc(...); P1: corruptDtcCrc(20); P2: loadDtc(0) | 越界损坏请求被忽略，slot0 仍可正常加载 |
| `dtc_crc_corrupt_empty_slot_ignored` | P0: storeDtc(...); P1: corruptDtcCrc(1); P2: loadDtc(0) | 空槽损坏请求被忽略，slot0 仍可正常加载 |

### 规则: 校准读写与 CRC 损坏回退 — SWR-FZC-032

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `cal_write_read_custom` | P0: writeCal(custom); P1: readCal | ret=0，字段全量匹配 |
| `cal_read_null_rejected` | P0: readCal(nullCal=true) | ret=1 |
| `cal_write_null_rejected` | P0: writeCal(nullCal=true) | ret=1 |
| `cal_crc_corrupt_fallback` | P0: writeCal(custom); P1: corruptCalCrc; P2: readCal | ret=1 且输出默认值 |
| `cal_crc_intact_success` | P0: writeCal(steerGain=111); P1: readCal | ret=0 且 steerGain=111 |

### 规则: CRC-16 计算 — Swc_FzcNvm_Crc16

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `crc_known_vector` | P0: calcCrc(dataLen=4) | crc=35267（0x89C3） |
| `crc_null_returns_zero` | P0: calcCrc(nullCrc=true) | crc=0 |
| `crc_zero_length_init` | P0: calcCrc(dataLen=0) | crc=65535（0xFFFF） |

## 代码路径覆盖

- `Swc_FzcNvm_Init`：默认初始化、有效后端重载、后端 CRC 损坏回退三条路径全部纳入用例。
- `Swc_FzcNvm_StoreDtc`：未初始化、正常写首空槽、满槽失败全部纳入用例。
- `Swc_FzcNvm_LoadDtc`：NULL/越界/未初始化/空槽/CRC 损坏/成功六条路径全部纳入用例。
- `Swc_FzcNvm_LoadCal`：NULL/未初始化默认返回/CRC 损坏默认返回/成功四条路径全部纳入用例。
- `Swc_FzcNvm_StoreCal`：NULL/未初始化/成功三条路径全部纳入用例。
- `Swc_FzcNvm_Crc16`：NULL、零长度、常规数据以及位循环两个分支全部纳入用例。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-17）后，`Swc_FzcNvm.c` 的覆盖率报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **100%（185 / 185）** |
| 分支覆盖 | **100%（44 / 44）** |
| 函数覆盖 | **100%（12 / 12）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/fzc_nvm.feature` | **24 scenarios / 144 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **464 scenarios / 2809 steps passed** |

函数命中次数（`Swc_FzcNvm.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `Swc_FzcNvm_Crc16` | 716 |
| `FzcNvm_DtcRecordCrc` | 388 |
| `FzcNvm_CalDataCrc` | 316 |
| `FzcNvm_ApplyCalDefaults` | 182 |
| `Swc_FzcNvm_Init` | 89 |
| `Swc_FzcNvm_StoreDtc` | 104 |
| `Swc_FzcNvm_LoadDtc` | 2456 |
| `Swc_FzcNvm_LoadCal` | 33 |
| `Swc_FzcNvm_StoreCal` | 28 |
| `Swc_FzcNvm_TestGetInitialized` | 93 |
| `Swc_FzcNvm_TestCorruptDtcCrc` | 8 |
| `Swc_FzcNvm_TestCorruptCalCrc` | 4 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/fzc/src/Swc_FzcNvm.c.gcov.html`
> 的逐行 hit count 回填。所有可执行行均至少被 1 个端到端场景命中。

#### Swc_FzcNvm_Crc16（L52-L76）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L53 | `{` | 716 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L54 | `uint16 crc;` | 716 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L55 | `uint16 i;` | 716 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L56 | `uint8  bit;` | 716 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L58 | `if (data == NULL_PTR) {` | 716 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L59 | `return 0u;` | 4 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L60 | `}` | 4 | `crc_null_returns_zero`；L53-L56 亦由所有 CRC 调用路径间接命中 |
| L62 | `crc = FZC_NVM_CRC16_INIT;` | 712 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L64 | `for (i = 0u; i < length; i++) {` | 7868 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L65 | `crc ^= (uint16)((uint16)data[i] << 8u);` | 7156 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L66 | `for (bit = 0u; bit < 8u; bit++) {` | 64404 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L67 | `if ((crc & 0x8000u) != 0u) {` | 57248 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L68 | `crc = (uint16)((uint16)(crc << 1u) ^ FZC_NVM_CRC16_POLY);` | 29795 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L69 | `} else {` | 29795 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L70 | `crc = (uint16)(crc << 1u);` | 27453 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L71 | `}` | 27453 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L72 | `}` | 57248 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L73 | `}` | 7156 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |
| L75 | `return crc;` | 712 | `crc_known_vector`、`crc_zero_length_init`；且被 Init / DTC / Cal CRC 计算间接复用 |

#### FzcNvm_DtcRecordCrc（L82-L96）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L83 | `{` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L85 | `uint8 buf[7];` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L87 | `buf[0] = rec->dtcId;` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L88 | `buf[1] = rec->status;` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L89 | `buf[2] = (uint8)((uint16)rec->freezeSteer & 0xFFu);` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L90 | `buf[3] = (uint8)(((uint16)rec->freezeSteer >> 8u) & 0xFFu);` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L91 | `buf[4] = rec->freezeBrake;` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L92 | `buf[5] = (uint8)(rec->freezeLidar & 0xFFu);` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L93 | `buf[6] = (uint8)((rec->freezeLidar >> 8u) & 0xFFu);` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L95 | `return Swc_FzcNvm_Crc16(buf, 7u);` | 388 | `dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |

#### FzcNvm_CalDataCrc（L102-L122）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L103 | `{` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L104 | `uint8 buf[14];` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L106 | `buf[0]  = (uint8)((uint16)cal->steerCenterOffset & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L107 | `buf[1]  = (uint8)(((uint16)cal->steerCenterOffset >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L108 | `buf[2]  = (uint8)(cal->steerGain & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L109 | `buf[3]  = (uint8)((cal->steerGain >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L110 | `buf[4]  = (uint8)((uint16)cal->brakePosOffset & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L111 | `buf[5]  = (uint8)(((uint16)cal->brakePosOffset >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L112 | `buf[6]  = (uint8)(cal->brakeGain & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L113 | `buf[7]  = (uint8)((cal->brakeGain >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L114 | `buf[8]  = (uint8)(cal->lidarWarnCm & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L115 | `buf[9]  = (uint8)((cal->lidarWarnCm >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L116 | `buf[10] = (uint8)(cal->lidarBrakeCm & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L117 | `buf[11] = (uint8)((cal->lidarBrakeCm >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L118 | `buf[12] = (uint8)(cal->lidarEmergencyCm & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L119 | `buf[13] = (uint8)((cal->lidarEmergencyCm >> 8u) & 0xFFu);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L121 | `return Swc_FzcNvm_Crc16(buf, 14u);` | 316 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`uninit_read_defaults_not_ok`、`cal_write_read_custom`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |

#### FzcNvm_ApplyCalDefaults（L128-L138）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L129 | `{` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L130 | `cal->steerCenterOffset = FZC_NVM_CAL_STEER_OFFSET_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L131 | `cal->steerGain         = FZC_NVM_CAL_STEER_GAIN_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L132 | `cal->brakePosOffset    = FZC_NVM_CAL_BRAKE_OFFSET_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L133 | `cal->brakeGain         = FZC_NVM_CAL_BRAKE_GAIN_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L134 | `cal->lidarWarnCm       = FZC_NVM_CAL_LIDAR_WARN_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L135 | `cal->lidarBrakeCm      = FZC_NVM_CAL_LIDAR_BRAKE_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L136 | `cal->lidarEmergencyCm  = FZC_NVM_CAL_LIDAR_EMERG_DEFAULT;` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L137 | `cal->crc               = FzcNvm_CalDataCrc(cal);` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |
| L138 | `}` | 182 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_corrupt_backend_fallback`、`cal_crc_corrupt_fallback` |

#### Swc_FzcNvm_Init（L144-L175）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L145 | `{` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L146 | `uint8 i;` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L149 | `for (i = 0u; i < FZC_NVM_DTC_MAX_SLOTS; i++) {` | 1869 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L150 | `FzcNvm_DtcSlots[i].dtcId       = 0u;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L151 | `FzcNvm_DtcSlots[i].status      = FZC_NVM_DTC_EMPTY;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L152 | `FzcNvm_DtcSlots[i].freezeSteer = 0;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L153 | `FzcNvm_DtcSlots[i].freezeBrake = 0u;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L154 | `FzcNvm_DtcSlots[i].freezeLidar = 0u;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L155 | `FzcNvm_DtcSlots[i].crc         = 0u;` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L156 | `}` | 1780 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L159 | `FzcNvm_ApplyCalDefaults(&FzcNvm_CalData);` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L162 | `(void)NvM_ReadBlock(0u, &FzcNvm_DtcSlots[0]);` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L163 | `(void)NvM_ReadBlock(1u, &FzcNvm_CalData);` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L166 | `{` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L167 | `uint16 expected_crc = FzcNvm_CalDataCrc(&FzcNvm_CalData);` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L168 | `if (FzcNvm_CalData.crc != expected_crc) {` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L170 | `FzcNvm_ApplyCalDefaults(&FzcNvm_CalData);` | 85 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L171 | `}` | 85 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L172 | `}` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L174 | `FzcNvm_Initialized = TRUE;` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |
| L175 | `}` | 89 | `init_ready_defaults`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`dtc_store_reload_roundtrip` |

#### Swc_FzcNvm_StoreDtc（L186-L220）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L186 | `{` | 104 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L187 | `uint8 i;` | 104 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L188 | `uint8 slot;` | 104 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L190 | `if (FzcNvm_Initialized != TRUE) {` | 104 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L191 | `return E_NOT_OK;` | 4 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L192 | `}` | 4 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L195 | `slot = 0xFFu;` | 100 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L196 | `for (i = 0u; i < FZC_NVM_DTC_MAX_SLOTS; i++) {` | 940 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L197 | `if (FzcNvm_DtcSlots[i].status == FZC_NVM_DTC_EMPTY) {` | 936 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L198 | `slot = i;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L199 | `break;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L200 | `}` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L201 | `}` | 936 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L203 | `if (slot == 0xFFu) {` | 100 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L205 | `return E_NOT_OK;` | 4 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L206 | `}` | 4 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L209 | `FzcNvm_DtcSlots[slot].dtcId       = dtcId;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L210 | `FzcNvm_DtcSlots[slot].status      = FZC_NVM_DTC_ACTIVE;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L211 | `FzcNvm_DtcSlots[slot].freezeSteer = steerAngle;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L212 | `FzcNvm_DtcSlots[slot].freezeBrake = brakePos;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L213 | `FzcNvm_DtcSlots[slot].freezeLidar = lidarDist;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L214 | `FzcNvm_DtcSlots[slot].crc         = FzcNvm_DtcRecordCrc(&FzcNvm_DtcSlots[slot]);` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L217 | `(void)NvM_WriteBlock(0u, &FzcNvm_DtcSlots[0]);` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L219 | `return E_OK;` | 96 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L220 | `}` | 100 | `uninit_store_rejected`、`dtc_store_reload_roundtrip`、`dtc_full_rejects_21st`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |

#### Swc_FzcNvm_LoadDtc（L227-L263）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L227 | `{` | 2456 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L228 | `uint16 expected_crc;` | 2456 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L230 | `if (record == NULL_PTR) {` | 2456 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L231 | `return E_NOT_OK;` | 4 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L232 | `}` | 4 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L234 | `if (index >= FZC_NVM_DTC_MAX_SLOTS) {` | 2452 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L235 | `return E_NOT_OK;` | 4 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L236 | `}` | 4 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L238 | `if (FzcNvm_Initialized != TRUE) {` | 2448 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L239 | `return E_NOT_OK;` | 404 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L240 | `}` | 404 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L243 | `if (FzcNvm_DtcSlots[index].status == FZC_NVM_DTC_EMPTY) {` | 2044 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L244 | `return E_NOT_OK;` | 1752 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L245 | `}` | 1752 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L248 | `expected_crc = FzcNvm_DtcRecordCrc(&FzcNvm_DtcSlots[index]);` | 292 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L249 | `if (FzcNvm_DtcSlots[index].crc != expected_crc) {` | 292 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L251 | `return E_NOT_OK;` | 8 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L252 | `}` | 8 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L255 | `record->dtcId       = FzcNvm_DtcSlots[index].dtcId;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L256 | `record->status      = FzcNvm_DtcSlots[index].status;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L257 | `record->freezeSteer = FzcNvm_DtcSlots[index].freezeSteer;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L258 | `record->freezeBrake = FzcNvm_DtcSlots[index].freezeBrake;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L259 | `record->freezeLidar = FzcNvm_DtcSlots[index].freezeLidar;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L260 | `record->crc         = FzcNvm_DtcSlots[index].crc;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L262 | `return E_OK;` | 284 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |
| L263 | `}` | 292 | `uninit_load_rejected`、`dtc_empty_slot_rejected`、`dtc_null_record_rejected`、`dtc_index_out_of_range`、`dtc_store_reload_roundtrip`、`dtc_crc_corrupt_rejected`、`dtc_crc_intact_success`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored`；以及 harness `occupiedSlots` 统计 |

#### Swc_FzcNvm_LoadCal（L270-L301）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L270 | `{` | 33 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L271 | `uint16 expected_crc;` | 33 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L273 | `if (cal == NULL_PTR) {` | 33 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L274 | `return E_NOT_OK;` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L275 | `}` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L277 | `if (FzcNvm_Initialized != TRUE) {` | 29 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L278 | `FzcNvm_ApplyCalDefaults(cal);` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L279 | `return E_NOT_OK;` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L280 | `}` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L283 | `expected_crc = FzcNvm_CalDataCrc(&FzcNvm_CalData);` | 25 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L284 | `if (FzcNvm_CalData.crc != expected_crc) {` | 25 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L286 | `FzcNvm_ApplyCalDefaults(cal);` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L287 | `return E_NOT_OK;` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L288 | `}` | 4 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L291 | `cal->steerCenterOffset = FzcNvm_CalData.steerCenterOffset;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L292 | `cal->steerGain         = FzcNvm_CalData.steerGain;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L293 | `cal->brakePosOffset    = FzcNvm_CalData.brakePosOffset;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L294 | `cal->brakeGain         = FzcNvm_CalData.brakeGain;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L295 | `cal->lidarWarnCm       = FzcNvm_CalData.lidarWarnCm;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L296 | `cal->lidarBrakeCm      = FzcNvm_CalData.lidarBrakeCm;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L297 | `cal->lidarEmergencyCm  = FzcNvm_CalData.lidarEmergencyCm;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L298 | `cal->crc               = FzcNvm_CalData.crc;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L300 | `return E_OK;` | 21 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L301 | `}` | 25 | `init_ready_defaults`、`uninit_read_defaults_not_ok`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_read_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |

#### Swc_FzcNvm_StoreCal（L308-L333）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L308 | `{` | 28 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L309 | `if (cal == NULL_PTR) {` | 28 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L310 | `return E_NOT_OK;` | 4 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L311 | `}` | 4 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L313 | `if (FzcNvm_Initialized != TRUE) {` | 24 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L314 | `return E_NOT_OK;` | 4 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L315 | `}` | 4 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L318 | `FzcNvm_CalData.steerCenterOffset = cal->steerCenterOffset;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L319 | `FzcNvm_CalData.steerGain         = cal->steerGain;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L320 | `FzcNvm_CalData.brakePosOffset    = cal->brakePosOffset;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L321 | `FzcNvm_CalData.brakeGain         = cal->brakeGain;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L322 | `FzcNvm_CalData.lidarWarnCm       = cal->lidarWarnCm;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L323 | `FzcNvm_CalData.lidarBrakeCm      = cal->lidarBrakeCm;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L324 | `FzcNvm_CalData.lidarEmergencyCm  = cal->lidarEmergencyCm;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L327 | `FzcNvm_CalData.crc = FzcNvm_CalDataCrc(&FzcNvm_CalData);` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L330 | `(void)NvM_WriteBlock(1u, &FzcNvm_CalData);` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L332 | `return E_OK;` | 20 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |
| L333 | `}` | 24 | `uninit_write_rejected`、`init_reload_custom_cal`、`init_corrupt_backend_fallback`、`cal_write_read_custom`、`cal_write_null_rejected`、`cal_crc_corrupt_fallback`、`cal_crc_intact_success` |

#### UNIT_TEST 钩子（L336-L352）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---:|---|
| L337 | `{` | 93 | 全部 24 个 FZC NVM 场景（response `initialized` 字段） |
| L338 | `return FzcNvm_Initialized;` | 93 | 全部 24 个 FZC NVM 场景（response `initialized` 字段） |
| L339 | `}` | 93 | 全部 24 个 FZC NVM 场景（response `initialized` 字段） |
| L342 | `{` | 8 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L343 | `if ((index < FZC_NVM_DTC_MAX_SLOTS) &&` | 8 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L344 | `(FzcNvm_DtcSlots[index].status != FZC_NVM_DTC_EMPTY)) {` | 8 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L345 | `FzcNvm_DtcSlots[index].crc ^= 0xFFFFu;` | 4 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L346 | `}` | 4 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L347 | `}` | 8 | `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、`dtc_crc_corrupt_empty_slot_ignored` |
| L350 | `{` | 4 | `cal_crc_corrupt_fallback` |
| L351 | `FzcNvm_CalData.crc ^= 0xFFFFu;` | 4 | `cal_crc_corrupt_fallback` |
| L352 | `}` | 4 | `cal_crc_corrupt_fallback` |

### 分支覆盖分析

`Swc_FzcNvm.c` 的 44 个分支全部命中，无逻辑点遗漏：

- `Swc_FzcNvm_Crc16`：`data == NULL_PTR` true/false、位循环中
  `(crc & 0x8000u) != 0u` true/false 均覆盖；
- `Swc_FzcNvm_Init`：后端校准 CRC 校验 true/false 两侧分别由
  `init_reload_custom_cal` 与 `init_corrupt_backend_fallback` / 默认初始化路径覆盖；
- `Swc_FzcNvm_StoreDtc`：未初始化守卫、空槽搜索、满槽失败三组条件均两侧覆盖；
- `Swc_FzcNvm_LoadDtc`：NULL / 越界 / 未初始化 / 空槽 / CRC 损坏守卫全部两侧覆盖；
- `Swc_FzcNvm_LoadCal` / `Swc_FzcNvm_StoreCal`：NULL / 未初始化 / CRC 校验全部两侧覆盖；
- `Swc_FzcNvm_TestCorruptDtcCrc`：有效槽位、越界槽位、空槽位三种条件组合均由
  `dtc_crc_corrupt_rejected`、`dtc_crc_corrupt_out_of_range_ignored`、
  `dtc_crc_corrupt_empty_slot_ignored` 覆盖。

### 无法覆盖的代码说明

**无豁免项。** 当前 `Swc_FzcNvm.c` 在测试编译配置下的可执行行、分支、函数均已
由端到端测试覆盖；不存在需要单独说明的不可达防御代码或编译期排除路径。
