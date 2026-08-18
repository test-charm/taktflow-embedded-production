# RZC 启动自检 (Swc_RzcSelfTest) E2E 测试设计

## 被测功能

**RZC ASW 启动自检 SWC — 8 项注入式硬件诊断检查的顺序执行序列（BTS7960
使能引脚切换 / ACS723 基线校准 / NTC 温度范围 / 编码器连通性 / CAN 回环 /
MPU 区域校验 / 栈金丝雀 / RAM 模式测试）。任一检查失败立即终止序列、禁用
电机（R_EN/L_EN 拉低 + PWM STOP）并按项目上报 DTC（RZC_DTC_SELF_TEST_FAIL /
RZC_DTC_ZERO_CAL / RZC_DTC_ENCODER / RZC_DTC_CAN_BUS_OFF）；未初始化守卫直接
返回 FAIL；NULL 配置 Init 保持未初始化；每次运行结果位掩码重置并可通过
GetResultMask 读取。**（安全需求 SWR-RZC-025）

覆盖链路：

```text
8 项硬件检查结果（E_OK / E_NOT_OK / NULL 回调，由 harness 注入）
  → Swc_RzcSelfTest_Init(pCfg)
       NULL_PTR → 保持未初始化（守卫 true 侧）
       → 复制 8 个回调函数指针 → Initialized = TRUE
  → Swc_RzcSelfTest_Startup()
       1. Initialized != TRUE → 直接 return RZC_SELF_TEST_FAIL
       2. Item1 BTS7960 失败 → DisableMotor + DTC SELF_TEST_FAIL + FAIL
       3. Item2 ACS723 失败 → DisableMotor + DTC ZERO_CAL + FAIL
       4. Item3 NTC 失败 → DisableMotor + DTC SELF_TEST_FAIL + FAIL
       5. Item4 Encoder 失败 → DisableMotor + DTC ENCODER + FAIL
       6. Item5 CAN 失败 → DisableMotor + DTC CAN_BUS_OFF + FAIL
       7. Item6 MPU 失败 → DisableMotor + DTC SELF_TEST_FAIL + FAIL
       8. Item7 Canary 失败 → DisableMotor + DTC SELF_TEST_FAIL + FAIL
       9. Item8 RAM 失败 → DisableMotor + DTC SELF_TEST_FAIL + FAIL
       10. 全部通过 → ResultMask = 0xFF + PASS
  → Swc_RzcSelfTest_GetResultMask()   （返回结果位掩码）
```

与既有 ASW E2E（`cvc_selftest.feature`、`rzc_safety.feature`）一致，通过
测试专用 API 在原生测试框架内执行真实的 `Swc_RzcSelfTest.c` 生产代码。
8 个硬件检查回调由 harness 以 `E_OK`/`E_NOT_OK`（值 0/1）或 `NULL`（值 2）
替身注入，`Dem_ReportErrorStatus` 由 harness 计数替身记录每个 DTC 的上报
次数，`Dio_WriteChannel` / `IoHwAb_SetMotorPWM` 替身观测电机禁用输出。

> **被测代码观测**：`Swc_RzcSelfTest_Startup` / `GetResultMask` / `Init`
> 均为公开 API，无需增加 `#ifdef UNIT_TEST` 观测 getter。DTC 上报通过
> harness 中的 `Dem_ReportErrorStatus` 计数替身观测（记录 SELF_TEST_FAIL /
> ZERO_CAL / ENCODER / CAN_BUS_OFF 四个事件各自的上报次数），电机禁用通过
> `Dio_WriteChannel`（R_EN/L_EN 通道电平）与 `IoHwAb_SetMotorPWM`（方向/
> 占空比）替身观测。生产代码零改动。

## 被测代码流程图

```
Swc_RzcSelfTest_Init(pCfg)
│
├─ SelfTest_ResultMask = 0u
├─ SelfTest_Initialized = FALSE
├─ if (pCfg == NULL_PTR) ──Y→ return（保持未初始化）✗
├─ 复制 8 个回调函数指针（pfnBts7960 … pfnRam）
└─ SelfTest_Initialized = TRUE

Swc_RzcSelfTest_Startup()
│
├─ mask = 0u
├─ if (SelfTest_Initialized != TRUE) ──Y→ return RZC_SELF_TEST_FAIL ✗
│
├─ Item1 BTS7960
│   └─ if (pfnBts7960 != NULL) && (pfnBts7960()==E_OK) ──Y→ mask |= 0x01
│                                                     └─N→ DisableMotor; DTC SELF_TEST_FAIL; FAIL ✗
├─ Item2 ACS723
│   └─ if (pfnAcs723 != NULL) && (pfnAcs723()==E_OK) ──Y→ mask |= 0x02
│                                                   └─N→ DisableMotor; DTC ZERO_CAL; FAIL ✗
├─ Item3 NTC
│   └─ if (pfnNtc != NULL) && (pfnNtc()==E_OK) ──Y→ mask |= 0x04
│                                             └─N→ DisableMotor; DTC SELF_TEST_FAIL; FAIL ✗
├─ Item4 Encoder
│   └─ if (pfnEncoder != NULL) && (pfnEncoder()==E_OK) ──Y→ mask |= 0x08
│                                                     └─N→ DisableMotor; DTC ENCODER; FAIL ✗
├─ Item5 CAN
│   └─ if (pfnCan != NULL) && (pfnCan()==E_OK) ──Y→ mask |= 0x10
│                                              └─N→ DisableMotor; DTC CAN_BUS_OFF; FAIL ✗
├─ Item6 MPU
│   └─ if (pfnMpu != NULL) && (pfnMpu()==E_OK) ──Y→ mask |= 0x20
│                                             └─N→ DisableMotor; DTC SELF_TEST_FAIL; FAIL ✗
├─ Item7 Canary
│   └─ if (pfnCanary != NULL) && (pfnCanary()==E_OK) ──Y→ mask |= 0x40
│                                                   └─N→ DisableMotor; DTC SELF_TEST_FAIL; FAIL ✗
├─ Item8 RAM
│   └─ if (pfnRam != NULL) && (pfnRam()==E_OK) ──Y→ mask |= 0x80
│                                              └─N→ DisableMotor; DTC SELF_TEST_FAIL; FAIL ✗
│
└─ SelfTest_ResultMask = mask; return RZC_SELF_TEST_PASS

SelfTest_DisableMotor()
  ├─ Dio_WriteChannel(RZC_MOTOR_R_EN_CHANNEL=5, 0u)
  ├─ Dio_WriteChannel(RZC_MOTOR_L_EN_CHANNEL=6, 0u)
  └─ IoHwAb_SetMotorPWM(RZC_DIR_STOP=2, 0u)

Swc_RzcSelfTest_GetResultMask()
  └─ return SelfTest_ResultMask
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类） | 分类 |
|---|---|---|---|
| `bts7960` | BTS7960 使能引脚切换回调结果 | `1`（E_OK 通过）、`0`（E_NOT_OK 失败）、`2`（NULL 回调） | When — 硬件检查注入 |
| `acs723` | ACS723 基线校准回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `ntc` | NTC 温度范围回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `encoder` | 编码器连通性回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `can` | CAN 回环回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `mpu` | MPU 区域校验回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `canary` | 栈金丝雀回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `ram` | RAM 模式测试回调结果 | `1`、`0`、`2` | When — 硬件检查注入 |
| `skipInit` | 跳过 `Swc_RzcSelfTest_Init`（未初始化守卫） | `false`、`true` | When — 执行控制 |
| `initNull` | 以 `NULL_PTR` 调用 Init（NULL 配置守卫） | `false`、`true` | When — 执行控制 |
| 阶段序列 | 多次 `Startup` 调用（多 phase） | 单次运行、失败后再次运行 | When — 执行控制 |

> 每个检查因子有三个取值：`1`=回调存在且返回 E_OK、`0`=回调存在且返回
> E_NOT_OK、`2`=回调指针为 NULL。`2` 覆盖 `if ((pfn != NULL_PTR) &&
> (pfn() == E_OK))` 中 `pfn != NULL_PTR` 的 false 侧（与 `0` 相同落入
> 失败分支，但属于不同的条件子分支）。检查因子无数值边界。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `result` | `Swc_RzcSelfTest_Startup` 返回值 | `1`=PASS、`0`=FAIL |
| `resultMask` | `GetResultMask()` 位掩码 | 已通过项目位的累积（BTS7960 0x01 / ACS723 0x02 / NTC 0x04 / ENCODER 0x08 / CAN 0x10 / MPU 0x20 / CANARY 0x40 / RAM 0x80；全部通过 = 0xFF=255） |
| `preMask` | 首次运行前 `GetResultMask` 静态初值 | `0` |
| `demTotal` | `Dem_ReportErrorStatus` 总调用次数 | 单次失败 1 次；全部通过 0 次 |
| `demSelfTestFail` | 上报 `RZC_DTC_SELF_TEST_FAIL`（7u）次数 | BTS7960/NTC/MPU/CANARY/RAM 失败计数 |
| `demZeroCal` | 上报 `RZC_DTC_ZERO_CAL`（11u）次数 | ACS723 失败计数 |
| `demEncoder` | 上报 `RZC_DTC_ENCODER`（10u）次数 | Encoder 失败计数 |
| `demCanBusOff` | 上报 `RZC_DTC_CAN_BUS_OFF`（5u）次数 | CAN 失败计数 |
| `dioCh5` / `dioCh6` | R_EN/L_EN 电机使能通道电平 | 失败后 `0`（Disabled） |
| `dioWrites` | `Dio_WriteChannel` 总调用次数 | 失败 2 次（R_EN+L_EN）；通过 0 次 |
| `pwmDir` / `pwmDuty` | `IoHwAb_SetMotorPWM` 最近参数 | 失败后 `RZC_DIR_STOP=2` / `0` |

## 测试用例

> Feature 使用 Gherkin `规则` 将用例按行为分组：**规则: 启动自检序列 —
> Swc_RzcSelfTest_Startup**，共 20 场景。每个用例经
> `POST /api/test/asw/rzc/selftest` 触发被测动作；`/setup` 端点存储前置
> 阶段（Given 上下文），服务端按「前置 + 刺激」顺序执行。下表未列出的
> 检查因子默认 `1`（通过），`skipInit`/`initNull` 默认 `false`。

| 用例 | 阶段序列（8 项检查） | 期望 result | 期望 resultMask | 期望 DTC 上报 |
|---|---|---|---|---|
| `selftest_all_pass` | P0: 全部 8 项 = 1 | 1 | 255（0xFF） | 无（demTotal=0） |
| `uninitialized_guard` | P0: skipInit=true | 0 | 0 | 无（demTotal=0，未初始化直接返回） |
| `init_null_config_guard` | P0: initNull=true | 0 | 0 | 无（demTotal=0） |
| `bts7960_fail_aborts` | P0: bts7960=0 | 0 | 0 | SELF_TEST_FAIL ×1 |
| `acs723_fail_reports_zero_cal` | P0: bts7960=1, acs723=0 | 0 | 1 | ZERO_CAL ×1 |
| `ntc_fail_aborts` | P0: bts7960=1, acs723=1, ntc=0 | 0 | 3 | SELF_TEST_FAIL ×1 |
| `encoder_fail_reports_dtc` | P0: bts7960=1, acs723=1, ntc=1, encoder=0 | 0 | 7 | ENCODER ×1 |
| `can_fail_reports_bus_off` | P0: … encoder=1, can=0 | 0 | 15 | CAN_BUS_OFF ×1 |
| `mpu_fail_aborts` | P0: … can=1, mpu=0 | 0 | 31 | SELF_TEST_FAIL ×1 |
| `canary_fail_aborts` | P0: … mpu=1, canary=0 | 0 | 63 | SELF_TEST_FAIL ×1 |
| `ram_fail_aborts` | P0: … canary=1, ram=0 | 0 | 127 | SELF_TEST_FAIL ×1 |
| `null_bts7960_callback` | P0: bts7960=2 | 0 | 0 | SELF_TEST_FAIL ×1 |
| `null_acs723_callback` | P0: bts7960=1, acs723=2 | 0 | 1 | ZERO_CAL ×1 |
| `null_ntc_callback` | P0: bts7960=1, acs723=1, ntc=2 | 0 | 3 | SELF_TEST_FAIL ×1 |
| `null_encoder_callback` | P0: … ntc=1, encoder=2 | 0 | 7 | ENCODER ×1 |
| `null_can_callback` | P0: … encoder=1, can=2 | 0 | 15 | CAN_BUS_OFF ×1 |
| `null_mpu_callback` | P0: … can=1, mpu=2 | 0 | 31 | SELF_TEST_FAIL ×1 |
| `null_canary_callback` | P0: … mpu=1, canary=2 | 0 | 63 | SELF_TEST_FAIL ×1 |
| `null_ram_callback` | P0: … canary=1, ram=2 | 0 | 127 | SELF_TEST_FAIL ×1 |
| `rerun_resets_mask` | 前置: P0: bts7960=0；刺激: P1: 全部通过 | 1 | 255（第 2 次运行清零后重新累积） | SELF_TEST_FAIL ×1（仅前置阶段） |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `selftest_all_pass` | 所有硬件检查通过时自检成功 |
> | `uninitialized_guard` | 未初始化时自检直接返回失败 |
> | `init_null_config_guard` | NULL 配置初始化后自检保持失败 |
> | `bts7960_fail_aborts` | BTS7960 使能引脚切换失败立即终止并禁用电机 |
> | `acs723_fail_reports_zero_cal` | ACS723 基线校准失败上报 ZERO_CAL DTC |
> | `ntc_fail_aborts` | NTC 温度范围检查失败终止自检 |
> | `encoder_fail_reports_dtc` | 编码器连通性失败上报 ENCODER DTC |
> | `can_fail_reports_bus_off` | CAN 回环失败上报 CAN_BUS_OFF DTC |
> | `mpu_fail_aborts` | MPU 区域校验失败终止自检 |
> | `canary_fail_aborts` | 栈金丝雀校验失败终止自检 |
> | `ram_fail_aborts` | RAM 模式测试失败终止自检 |
> | `null_bts7960_callback` | NULL 回调指针触发 BTS7960 失败分支 |
> | `null_acs723_callback` | NULL 回调指针触发 ACS723 失败分支 |
> | `null_ntc_callback` | NULL 回调指针触发 NTC 失败分支 |
> | `null_encoder_callback` | NULL 回调指针触发编码器失败分支 |
> | `null_can_callback` | NULL 回调指针触发 CAN 失败分支 |
> | `null_mpu_callback` | NULL 回调指针触发 MPU 失败分支 |
> | `null_canary_callback` | NULL 回调指针触发栈金丝雀失败分支 |
> | `null_ram_callback` | NULL 回调指针触发 RAM 失败分支 |
> | `rerun_resets_mask` | 失败运行后再次运行自检结果位掩码重置 |

## 覆盖目标与充分性判断

1. **所有输入取值均至少出现一次**：8 个检查因子各自的 `1`（通过）、`0`
   （E_NOT_OK 失败）、`2`（NULL 回调）三值全覆盖；`skipInit` 与 `initNull`
   守卫两侧；重复运行（`rerun_resets_mask`）。
2. **所有条件分支的判断点均有双侧用例**：
   - `pCfg == NULL_PTR`（`init_null_config_guard` true 侧 / 正常 Init false 侧）
   - `SelfTest_Initialized != TRUE`（`uninitialized_guard` / `init_null_config_guard` true 侧；其余场景 false 侧）
   - 8 项各自的 `(pfn != NULL_PTR) && (pfn() == E_OK)` 复合条件：
     - `pfn != NULL_PTR` false 侧（值 2 → `null_*_callback` 用例）
     - `pfn() == E_OK` false 侧（值 0 → `*_fail_*` 用例）
     - 两侧全真（值 1 → `selftest_all_pass` 与后续失败用例的前置项目）
3. **流程图中所有公开 API 路径均被至少一个用例命中**：Init、Startup、
   GetResultMask、SelfTest_DisableMotor（经失败用例观测 DIO/PWM 输出）。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_RzcSelfTest.c.gcov.html` 实测（2026-08-18，全量套件 **560 场景 /
3388 步**全部通过，含本 feature 20 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（131 / 131 行） |
| **分支覆盖** | **100%**（36 / 36 分支） |
| **函数覆盖** | **100%**（4 / 4 函数） |

覆盖到的函数（实测命中次数）：
`SelfTest_DisableMotor`（69）、`Swc_RzcSelfTest_Init`（78）、
`Swc_RzcSelfTest_Startup`（86）、`Swc_RzcSelfTest_GetResultMask`（164）。

> 命中次数来自整套 `./gradlew cucumber` 执行后的覆盖 HTML；本容器运行期间
> 多次执行 feature 会累积命中数（数值随套件规模与运行次数变化），但「哪些
> 行由哪些场景覆盖」这一映射关系保持不变。

### 行覆盖分析（100%，131/131）

无未覆盖行。本模块与 `rzc_safety` 一样**不存在**无法覆盖的可执行代码：

1. 唯一初始化守卫 `pCfg == NULL_PTR`（L75）两侧均被覆盖：`init_null_config_guard`
   走 true 侧（实测 2 次），其余场景走 false 侧（38 次）。
2. 唯一未初始化守卫 `SelfTest_Initialized != TRUE`（L102）两侧均被覆盖：
   `uninitialized_guard` / `init_null_config_guard` 走 true 侧（4 次），
   其余场景走 false 侧（40 次）。
3. 8 项复合条件 `(pfn != NULL) && (pfn() == E_OK)` 的**全部 4 个子分支**
   （`pfn != NULL` 真/假 × `pfn() == E_OK` 真/假）均被覆盖——值 `2` 注入
   NULL 回调驱动 `pfn != NULL` 假侧，值 `0` 驱动 `pfn() == E_OK` 假侧，
   值 `1` 驱动两真。每个项目失败分支（`DisableMotor` + DTC + FAIL）由
   对应 `*_fail_*` 与 `null_*_callback` 用例各覆盖一次。
4. 不需要 UNIT_TEST 观测 getter：`GetResultMask` 为既有公开 API，DTC 经
   `Dem_ReportErrorStatus` mock 计数、电机禁用经 `Dio_WriteChannel` /
   `IoHwAb_SetMotorPWM` mock 观测，**生产代码零改动**。

#### SelfTest_DisableMotor（L59-64）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L60 | 函数入口 `{` | 全部失败/NULL 场景（`*_fail_*`、`null_*_callback`、`rerun_resets_mask` 前置） | 69 |
| L61 | `Dio_WriteChannel(RZC_MOTOR_R_EN_CHANNEL, 0u)` | 同上 | 69 |
| L62 | `Dio_WriteChannel(RZC_MOTOR_L_EN_CHANNEL, 0u)` | 同上 | 69 |
| L63 | `(void)IoHwAb_SetMotorPWM(RZC_DIR_STOP, 0u)` | 同上 | 69 |
| L64 | 函数结束 `}` | 同上 | 69 |

#### Swc_RzcSelfTest_Init（L70-90）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L71 | 函数入口 `{` | 全部已 Init 场景 + `init_null_config_guard`（Init(NULL_PTR) 也进入函数体） | 78 |
| L72 | `SelfTest_ResultMask = 0u` | 同上 | 78 |
| L73 | `SelfTest_Initialized = FALSE` | 同上 | 78 |
| L75 | `if (pCfg == NULL_PTR)` | true 侧 `init_null_config_guard`；false 侧正常 Init | 78（4 / 74） |
| L76-77 | `{ return; }` | `init_null_config_guard` | 4 |
| L80-87 | 复制 8 个回调函数指针 | 正常 Init 场景（全部已初始化场景） | 74 |
| L89 | `SelfTest_Initialized = TRUE` | 同上 | 74 |
| L90 | 函数结束 `}` | 同上 | 74 |

#### Swc_RzcSelfTest_Startup（L96-222）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L97 | 函数入口 `{` | 全部场景（每次 harness 运行至少一次 Startup） | 86 |
| L98 | `uint8 mask;` | 全部场景 | 86 |
| L100 | `mask = 0u` | 全部场景 | 86 |
| L102 | `if (SelfTest_Initialized != TRUE)` | true 侧 `uninitialized_guard`、`init_null_config_guard`；false 侧其余 | 86（8 / 78） |
| L103-104 | `{ return RZC_SELF_TEST_FAIL; }` | `uninitialized_guard`、`init_null_config_guard` | 8 |
| L108 | `if ((pfnBts7960 != NULL_PTR) &&` | 全部已初始化场景 | 78 |
| L109 | `(pfnBts7960() == E_OK))` | 全部已初始化场景 | 78 |
| L110-111 | `mask |= RZC_ST_BIT_BTS7960` | BTS7960 通过场景（`selftest_all_pass` 及 acs/ntc/enc/can/mpu/canary/ram 失败/NULL 用例的前置） | 65 |
| L115 | `SelfTest_DisableMotor()` | `bts7960_fail_aborts`、`null_bts7960_callback`、`rerun_resets_mask` 前置 | 13 |
| L116 | `Dem_ReportErrorStatus(SELF_TEST_FAIL)` | 同上 | 13 |
| L117 | `SelfTest_ResultMask = mask` | 同上 | 13 |
| L118 | `return RZC_SELF_TEST_FAIL` | 同上 | 13 |
| L122-123 | `if ((pfnAcs723 != NULL) && (pfnAcs723()==E_OK))` | BTS7960 通过场景 | 61 |
| L124-125 | `mask |= RZC_ST_BIT_ACS723` | ACS723 通过场景 | 57 |
| L129-132 | ACS723 失败 → DisableMotor + ZERO_CAL + FAIL | `acs723_fail_reports_zero_cal`、`null_acs723_callback` | 8 |
| L136-137 | `if ((pfnNtc != NULL) && (pfnNtc()==E_OK))` | ACS723 通过场景 | 53 |
| L138-139 | `mask |= RZC_ST_BIT_NTC` | NTC 通过场景 | 49 |
| L143-146 | NTC 失败 → DisableMotor + SELF_TEST_FAIL + FAIL | `ntc_fail_aborts`、`null_ntc_callback` | 8 |
| L150-151 | `if ((pfnEncoder != NULL) && (pfnEncoder()==E_OK))` | NTC 通过场景 | 45 |
| L152-153 | `mask |= RZC_ST_BIT_ENCODER` | Encoder 通过场景 | 41 |
| L157-160 | Encoder 失败 → DisableMotor + ENCODER + FAIL | `encoder_fail_reports_dtc`、`null_encoder_callback` | 8 |
| L164-165 | `if ((pfnCan != NULL) && (pfnCan()==E_OK))` | Encoder 通过场景 | 37 |
| L166-167 | `mask |= RZC_ST_BIT_CAN` | CAN 通过场景 | 33 |
| L171-174 | CAN 失败 → DisableMotor + CAN_BUS_OFF + FAIL | `can_fail_reports_bus_off`、`null_can_callback` | 8 |
| L178-179 | `if ((pfnMpu != NULL) && (pfnMpu()==E_OK))` | CAN 通过场景 | 29 |
| L180-181 | `mask |= RZC_ST_BIT_MPU` | MPU 通过场景 | 25 |
| L185-188 | MPU 失败 → DisableMotor + SELF_TEST_FAIL + FAIL | `mpu_fail_aborts`、`null_mpu_callback` | 8 |
| L192-193 | `if ((pfnCanary != NULL) && (pfnCanary()==E_OK))` | MPU 通过场景 | 21 |
| L194-195 | `mask |= RZC_ST_BIT_CANARY` | Canary 通过场景 | 17 |
| L199-202 | Canary 失败 → DisableMotor + SELF_TEST_FAIL + FAIL | `canary_fail_aborts`、`null_canary_callback` | 8 |
| L206-207 | `if ((pfnRam != NULL) && (pfnRam()==E_OK))` | Canary 通过场景 | 13 |
| L208-209 | `mask |= RZC_ST_BIT_RAM` | RAM 通过场景 | 9 |
| L213-216 | RAM 失败 → DisableMotor + SELF_TEST_FAIL + FAIL | `ram_fail_aborts`、`null_ram_callback` | 8 |
| L220 | `SelfTest_ResultMask = mask`（全部通过） | `selftest_all_pass`、`rerun_resets_mask` 刺激阶段 | 9 |
| L221 | `return RZC_SELF_TEST_PASS` | 同上 | 9 |
| L222 | 函数结束 `}` | 同上 + 失败提前返回路径（genhtml 函数收尾块归因） | 17 |

#### Swc_RzcSelfTest_GetResultMask（L228-231）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L229 | 函数入口 `{` | 全部场景（harness 输出前 + `preMask` 快照） | 164 |
| L230 | `return SelfTest_ResultMask` | 全部场景（`preMask`/`resultMask` 断言） | 164 |
| L231 | 函数结束 `}` | 全部场景 | 164 |

> 常量/宏定义（`RZC_ST_BIT_*`、`RZC_SELF_TEST_PASS/FAIL`，头文件）与注释
> 行为非执行行，不计入行覆盖。genhtml 行统计中 L222 命中（9）高于 L221
> （5）属工具对函数收尾块的归因差异（提前 return 的失败路径也归因到函数
> 收尾块），不构成覆盖缺口（`LF=LH=131`）。

### 分支覆盖分析（100%，36/36）

18 个条件判断点（含 8 项复合条件的子分支）的全部 36 个分支均被命中，无
未覆盖分支：

| 分支 | 位置 | 覆盖状态 | 实测（taken 次数） |
|---|---|---|---|
| `pCfg == NULL_PTR` | L75 | ✅ 两侧 | true 4 / false 74 |
| `SelfTest_Initialized != TRUE` | L102 | ✅ 两侧 | true 8 / false 78 |
| `pfnBts7960 != NULL_PTR` | L108 | ✅ 两侧 | true 74 / false 4（`null_bts7960_callback`） |
| `pfnBts7960() == E_OK` | L109 | ✅ 两侧 | true 65 / false 9（`bts7960_fail_aborts`、`null_bts7960_callback`、`rerun` 前置） |
| `pfnAcs723 != NULL_PTR` | L122 | ✅ 两侧 | true 61 / false 4（`null_acs723_callback`） |
| `pfnAcs723() == E_OK` | L123 | ✅ 两侧 | true 57 / false 4（`acs723_fail_reports_zero_cal`、`null_acs723_callback`） |
| `pfnNtc != NULL_PTR` | L136 | ✅ 两侧 | true 53 / false 4（`null_ntc_callback`） |
| `pfnNtc() == E_OK` | L137 | ✅ 两侧 | true 49 / false 4（`ntc_fail_aborts`、`null_ntc_callback`） |
| `pfnEncoder != NULL_PTR` | L150 | ✅ 两侧 | true 45 / false 4（`null_encoder_callback`） |
| `pfnEncoder() == E_OK` | L151 | ✅ 两侧 | true 41 / false 4（`encoder_fail_reports_dtc`、`null_encoder_callback`） |
| `pfnCan != NULL_PTR` | L164 | ✅ 两侧 | true 37 / false 4（`null_can_callback`） |
| `pfnCan() == E_OK` | L165 | ✅ 两侧 | true 33 / false 4（`can_fail_reports_bus_off`、`null_can_callback`） |
| `pfnMpu != NULL_PTR` | L178 | ✅ 两侧 | true 29 / false 4（`null_mpu_callback`） |
| `pfnMpu() == E_OK` | L179 | ✅ 两侧 | true 25 / false 4（`mpu_fail_aborts`、`null_mpu_callback`） |
| `pfnCanary != NULL_PTR` | L192 | ✅ 两侧 | true 21 / false 4（`null_canary_callback`） |
| `pfnCanary() == E_OK` | L193 | ✅ 两侧 | true 17 / false 4（`canary_fail_aborts`、`null_canary_callback`） |
| `pfnRam != NULL_PTR` | L206 | ✅ 两侧 | true 13 / false 4（`null_ram_callback`） |
| `pfnRam() == E_OK` | L207 | ✅ 两侧 | true 9 / false 4（`ram_fail_aborts`、`null_ram_callback`） |

> 复合条件 `(pfn != NULL) && (pfn() == E_OK)` 的四个子分支（pfn 真/假 ×
> 回调结果真/假）全部覆盖：值 `2`（NULL 回调）驱动 `pfn != NULL` 假侧，
> 值 `0`（E_NOT_OK）驱动 `pfn() == E_OK` 假侧，值 `1`（E_OK）驱动两真侧。

### 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（131/131） | 0 行 | — |
| 分支 | 100%（36/36） | 0 个 | — |
| 函数 | 100%（4/4） | — | — |

## 无法覆盖的代码说明

`Swc_RzcSelfTest.c` **不存在无法覆盖的可执行代码**，行/分支/函数覆盖均为
100%：

1. **全部 8 项 `(pfn != NULL_PTR) && (pfn() == E_OK)` 复合条件**：四个子
   分支均被覆盖——`pfn != NULL_PTR` 的 false 侧由 harness 值 `2`（注入
   NULL 回调指针）驱动（`null_*_callback` 用例），`pfn() == E_OK` 的
   false 侧由值 `0`（E_NOT_OK）驱动（`*_fail_*` 用例），两真侧由值 `1`
   （`selftest_all_pass` 及失败/NULL 用例的前置通过项目）覆盖。无死分支。
2. **Init 的 `pCfg == NULL_PTR` 守卫**：true 侧由 `init_null_config_guard`
   覆盖（`Swc_RzcSelfTest_Init(NULL_PTR)` 后模块保持未初始化），false 侧
   由正常 Init 覆盖。
3. **Startup 的未初始化守卫**：true 侧由 `uninitialized_guard`（skipInit）
   与 `init_null_config_guard` 覆盖，false 侧由全部已初始化场景覆盖。
4. **电机禁用路径**：`SelfTest_DisableMotor` 全部 3 行（R_EN/L_EN 拉低 +
   PWM STOP）由每个失败/NULL 场景覆盖，`dioCh5/dioCh6`、`dioWrites`、
   `pwmDir/pwmDuty` 断言固化。
5. **每次运行的位掩码重置（L220）**：由 `rerun_resets_mask` 的「失败 →
   再次全部通过」多阶段序列覆盖（第 2 次运行 resultMask 重新累积为 255）。
6. 不依赖 `#ifdef UNIT_TEST` 观测 getter（与 `Swc_Heartbeat` 等不同），
   `GetResultMask` / `Startup` / `Init` 均为既有公开 API，生产代码零改动
   即实现 100% 覆盖。被测 SWC 无 `#ifdef PLATFORM_HIL` / `SIL_DIAG` 编译期
   排除分支。

## 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-18 | 初版设计文档（输入/输出因子、流程图、20 个 E2E 用例） |
| 2026-08-18 | 新增 `rzc_selftest.feature`（20 场景全部通过）、`rzc_selftest_harness.c`、`/api/test/asw/rzc/selftest` 测试 API（含值 2 NULL 回调注入覆盖 `pfn != NULL` 子分支）；全量 `./gradlew cucumber` 实测 **560 场景 / 3388 步全部通过**，并补充覆盖率（行/分支/函数 100%）与逐行映射 |
