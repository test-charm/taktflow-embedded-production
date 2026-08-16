# CVC 启动自检 (Swc_SelfTest) E2E 测试设计

## 被测功能

**CVC ASW 启动自检 SWC — 7 项诊断检查的顺序执行序列（SPI 传感器回环 / CAN 控制器回环 / NVM 双区 CRC / OLED I2C ACK / MPU 区域校验 / 栈金丝雀 / RAM 模式测试），关键检查失败立即终止并上报 DTC，OLED 失败为非关键（QM）继续执行，步骤结果位掩码每次运行清零并可通过 GetResults 读取。**

覆盖链路：

```text
7 项硬件检查结果（E_OK / E_NOT_OK，由 harness 注入）
  → Swc_SelfTest_Startup()
       1. SelfTest_StepResults = 0u        （步骤结果位掩码清零）
       2. SPI 回环失败 → DTC SELF_TEST_FAIL + FAILED + return
       3. CAN 回环失败 → DTC SELF_TEST_FAIL + FAILED + return
       4. NVM 校验失败 → DTC NVM_CRC_FAIL + FAILED + return
       5. OLED ACK 失败 → DTC DISPLAY_COMM（非关键，继续）
       6. MPU 校验失败 → DTC SELF_TEST_FAIL + FAILED + return
       7. 栈金丝雀失败 → DTC SELF_TEST_FAIL + FAILED + return
       8. RAM 模式失败 → DTC SELF_TEST_FAIL + FAILED + return
       9. 全部通过 → PASSED
  → Swc_SelfTest_GetResults()            （返回步骤结果位掩码）
```

与既有 ASW E2E（`Swc_Heartbeat`、`Swc_CanMonitor`、`Swc_Watchdog`）一致，通过测试专用
API 在原生测试框架内执行真实的 `Swc_SelfTest.c` 生产代码。7 个 `SelfTest_Hw_*` 硬件
检查由 harness 以 `E_OK`/`E_NOT_OK` 替身注入，`Dem_ReportErrorStatus` 由 harness
计数替身记录每个 DTC 的上报次数。

> **被测代码观测**：`Swc_SelfTest_Startup` 的返回值和 `Swc_SelfTest_GetResults()`
> 均为公开 API，无需像 `Swc_Heartbeat`/`Swc_CanMonitor` 那样增加 `#ifdef UNIT_TEST`
> 观测 getter。DTC 上报通过 harness 中的 `Dem_ReportErrorStatus` 计数替身观测
> （记录 SELF_TEST_FAIL / NVM_CRC_FAIL / DISPLAY_COMM 三个事件各自的上报次数）。
> 生产代码零改动。

## 被测代码流程图

```
Swc_SelfTest_Startup()
│
├─ SelfTest_StepResults = 0u
├─ result = SELF_TEST_PASSED
│
├─ Step1 SPI 回环
│   └─ if (SelfTest_Hw_SpiLoopback() == E_OK) ─Y→ |StepResults |= SPI
│                                             └─N→ DTC SELF_TEST_FAIL; FAILED; return ✗
├─ Step2 CAN 回环
│   └─ if (SelfTest_Hw_CanLoopback() == E_OK) ─Y→ |StepResults |= CAN
│                                             └─N→ DTC SELF_TEST_FAIL; FAILED; return ✗
├─ Step3 NVM 完整性
│   └─ if (SelfTest_Hw_NvmCheck() == E_OK) ───Y→ |StepResults |= NVM
│                                             └─N→ DTC NVM_CRC_FAIL; FAILED; return ✗
├─ Step4 OLED I2C ACK（非关键）
│   └─ if (SelfTest_Hw_OledAck() == E_OK) ────Y→ |StepResults |= OLED
│                                             └─N→ DTC DISPLAY_COMM（继续，不失败）
├─ Step5 MPU 区域校验
│   └─ if (SelfTest_Hw_MpuVerify() == E_OK) ─Y→ |StepResults |= MPU
│                                             └─N→ DTC SELF_TEST_FAIL; FAILED; return ✗
├─ Step6 栈金丝雀
│   └─ if (SelfTest_Hw_CanaryCheck() == E_OK) Y→ |StepResults |= CANARY
│                                             └─N→ DTC SELF_TEST_FAIL; FAILED; return ✗
├─ Step7 RAM 模式测试
│   └─ if (SelfTest_Hw_RamPattern() == E_OK) ─Y→ |StepResults |= RAM
│                                             └─N→ DTC SELF_TEST_FAIL; FAILED; return ✗
│
└─ return result（PASSED / FAILED）
```

```text
Swc_SelfTest_GetResults()
  └─ return SelfTest_StepResults
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类） | 分类 |
|---|---|---|---|
| `spi` | `SelfTest_Hw_SpiLoopback` 返回 `E_OK`/`E_NOT_OK` | `true`（通过）、`false`（失败） | When — 硬件检查注入 |
| `can` | `SelfTest_Hw_CanLoopback` 返回 | `true`、`false` | When — 硬件检查注入 |
| `nvm` | `SelfTest_Hw_NvmCheck` 返回 | `true`、`false` | When — 硬件检查注入 |
| `oled` | `SelfTest_Hw_OledAck` 返回（非关键） | `true`、`false` | When — 硬件检查注入 |
| `mpu` | `SelfTest_Hw_MpuVerify` 返回 | `true`、`false` | When — 硬件检查注入 |
| `canary` | `SelfTest_Hw_CanaryCheck` 返回 | `true`、`false` | When — 硬件检查注入 |
| `ram` | `SelfTest_Hw_RamPattern` 返回 | `true`、`false` | When — 硬件检查注入 |
| 阶段序列 | 多次 `Swc_SelfTest_Startup` 调用（多 phase） | 单次运行、失败后再次运行 | When — 执行控制 |

> 每个检查因子只有两个等价类（通过/失败），无数值边界。失败点是检查序列中第一个
> 失败的关键项（SPI→CAN→NVM→MPU→CANARY→RAM 顺序）；OLED 失败不终止序列。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `result` | `Swc_SelfTest_Startup` 返回值 | `1`=PASSED、`0`=FAILED |
| `stepResults` | `Swc_SelfTest_GetResults()` 位掩码 | 已通过步骤位的累积（`SPI 0x01 / CAN 0x02 / NVM 0x04 / OLED 0x08 / MPU 0x10 / CANARY 0x20 / RAM 0x40`；全部通过 = 0x7F=127） |
| `preResults` | 首次运行前 `GetResults` 静态初值 | `0` |
| `demTotal` | `Dem_ReportErrorStatus` 总调用次数 | 关键失败 1 次；OLED 失败 1 次；OLED+关键失败 2 次 |
| `demSelfTestFail` | 上报 `CVC_DTC_SELF_TEST_FAIL`（16u）次数 | 关键检查失败计数 |
| `demNvmCrcFail` | 上报 `CVC_DTC_NVM_CRC_FAIL`（27u）次数 | NVM 失败计数 |
| `demDisplayComm` | 上报 `CVC_DTC_DISPLAY_COMM`（19u）次数 | OLED 失败计数 |

## 测试用例

> Feature 使用 Gherkin `规则` 将用例按行为分组：**规则: 启动自检序列 —
> Swc_SelfTest_Startup**，共 10 场景。每个用例经 `POST /api/test/asw/cvc/selftest`
> 触发被测动作；`/setup` 端点存储前置阶段（Given 上下文），服务端按「前置 + 刺激」
> 顺序执行。下表未列出的检查因子默认 `true`（通过）。

| 用例 | 阶段序列（7 项检查） | 期望 result | 期望 stepResults | 期望 DTC 上报 |
|---|---|---|---|---|
| `selftest_all_pass` | P0: spi=can=nvm=oled=mpu=canary=ram=true | 1 | 127（0x7F） | 无（demTotal=0） |
| `spi_fail_aborts` | P0: spi=false（其余默认通过） | 0 | 0 | SELF_TEST_FAIL ×1 |
| `can_fail_aborts` | P0: spi=true, can=false | 0 | 1（SPI） | SELF_TEST_FAIL ×1 |
| `nvm_fail_reports_crc` | P0: nvm=false | 0 | 3（SPI\|CAN） | NVM_CRC_FAIL ×1 |
| `oled_fail_noncritical` | P0: oled=false | 1 | 119（0x77，缺 OLED） | DISPLAY_COMM ×1 |
| `mpu_fail_aborts` | P0: mpu=false | 0 | 15（SPI\|CAN\|NVM\|OLED） | SELF_TEST_FAIL ×1 |
| `canary_fail_aborts` | P0: canary=false | 0 | 31（…\|MPU） | SELF_TEST_FAIL ×1 |
| `ram_fail_aborts` | P0: ram=false | 0 | 63（…\|CANARY） | SELF_TEST_FAIL ×1 |
| `oled_and_critical_fail` | P0: oled=false, mpu=false | 0 | 7（SPI\|CAN\|NVM） | DISPLAY_COMM ×1 + SELF_TEST_FAIL ×1 |
| `rerun_resets_results` | 前置: P0: spi=false; 刺激: P1: 全部通过 | 1 | 127（第 2 次运行清零后重新累积） | SELF_TEST_FAIL ×1（仅前置阶段） |

> **用例 ↔ feature 场景对照**（feature 场景名均为中文描述）：
> | 用例 ID（本文档） | feature 场景名 |
> |---|---|
> | `selftest_all_pass` | 所有硬件检查通过时自检成功 |
> | `spi_fail_aborts` | SPI 传感器回环失败立即终止自检 |
> | `can_fail_aborts` | CAN 控制器回环失败在 SPI 通过后终止自检 |
> | `nvm_fail_reports_crc` | NVM 完整性失败上报 CRC DTC 并终止自检 |
> | `oled_fail_noncritical` | OLED 失败为非关键项不阻断自检 |
> | `mpu_fail_aborts` | MPU 区域校验失败终止自检 |
> | `canary_fail_aborts` | 栈金丝雀校验失败终止自检 |
> | `ram_fail_aborts` | RAM 模式测试失败终止自检 |
> | `oled_and_critical_fail` | OLED 与关键检查同时失败时上报两个 DTC |
> | `rerun_resets_results` | 失败运行后再次运行自检步骤结果位掩码重置 |

## 代码路径覆盖

- `Swc_SelfTest_Startup` 全部可执行行 ✅
  - `SelfTest_StepResults = 0u` 清零与 `result = SELF_TEST_PASSED` 初始化 ✅
  - 7 个 `if (SelfTest_Hw_X() == E_OK)` 判断两侧：
    - 通过侧（`|= SELFTEST_STEP_X`）✅（`selftest_all_pass` 及部分失败场景的前置步骤）
    - 失败侧（DTC 上报 + FAILED + return）✅（`spi/can/nvm/mpu/canary/ram_fail_*` 各一）
  - OLED 非关键失败继续执行 ✅（`oled_fail_noncritical`、`oled_and_critical_fail`）
  - 末尾 `return result`（全部通过/OLED 失败后到达）✅
- `Swc_SelfTest_GetResults` 全部可执行行 ✅
  - 运行前静态初值 `0` ✅（`preResults` 断言）
  - 运行后位掩码 ✅（`stepResults` 断言，每个场景）
- harness 中的 `SelfTest_Hw_*` 替身与 `Dem_ReportErrorStatus` 计数替身（仅测试编译）✅
  - 由输入注入与 DTC 断言全部命中

> 被测功能无 `#ifdef UNIT_TEST` 观测 getter 新增（`GetResults` 为既有公开 API），
> 生产代码零改动。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_SelfTest.c.gcov.html` 实测（2026-08-16 全量套件 332 场景运行后，含本
feature 10 场景）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100%**（77 / 77 行） |
| **分支覆盖** | **100%**（14 / 14 分支） |
| **函数覆盖** | **100%**（2 / 2 函数） |

覆盖到的函数：`Swc_SelfTest_Startup`、`Swc_SelfTest_GetResults`。

> 下表「实测命中」为完整套件（332 场景）运行后的累积值（本容器运行期间多次执行
> feature 的累积：21 次 harness 调用、23 次 Startup 执行）；每次运行因容器重启
> 会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（100%，77/77）

行覆盖反映**每一行是否被执行**。77 行全部覆盖，无行级缺口。

### 逐函数代码行覆盖映射

#### Swc_SelfTest_Startup（L53-146）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L54 | 函数入口 `{` | 全部场景（每次 harness 运行先启动自检） | 23 |
| L55 | `uint8 result;` | 全部场景 | 23 |
| L57 | `SelfTest_StepResults = 0u;` | 全部场景（每次运行先清零） | 23 |
| L58 | `result = SELF_TEST_PASSED;` | 全部场景 | 23 |
| L61 | `if (SelfTest_Hw_SpiLoopback() == E_OK)` | 全部场景（true 侧 18 次 / false 侧 5 次） | 23 |
| L62-64 | `SelfTest_StepResults |= SELFTEST_STEP_SPI;` | SPI 通过场景（`selftest_all_pass`、`can/nvm/oled/mpu/canary/ram_fail_*` 前置步骤、`rerun_resets_results` 第 2 次运行） | 18 |
| L65-70 | SPI 失败 → DTC SELF_TEST_FAIL + FAILED + return | `spi_fail_aborts`、`rerun_resets_results` 前置阶段 | 5 |
| L73 | `if (SelfTest_Hw_CanLoopback() == E_OK)` | SPI 通过场景 | 18 |
| L74-76 | `SelfTest_StepResults |= SELFTEST_STEP_CAN;` | CAN 通过场景 | 16 |
| L77-82 | CAN 失败 → DTC SELF_TEST_FAIL + FAILED + return | `can_fail_aborts` | 2 |
| L85 | `if (SelfTest_Hw_NvmCheck() == E_OK)` | CAN 通过场景 | 16 |
| L86-88 | `SelfTest_StepResults |= SELFTEST_STEP_NVM;` | NVM 通过场景 | 14 |
| L89-96 | NVM 失败 → DTC NVM_CRC_FAIL + FAILED + return | `nvm_fail_reports_crc` | 2 |
| L99 | `if (SelfTest_Hw_OledAck() == E_OK)` | NVM 通过场景 | 14 |
| L100-102 | `SelfTest_StepResults |= SELFTEST_STEP_OLED;` | OLED 通过场景 | 10 |
| L103-107 | OLED 失败 → DTC DISPLAY_COMM（继续，不失败） | `oled_fail_noncritical`、`oled_and_critical_fail` | 4 |
| L110 | `if (SelfTest_Hw_MpuVerify() == E_OK)` | OLED 之后场景 | 14 |
| L111-113 | `SelfTest_StepResults |= SELFTEST_STEP_MPU;` | MPU 通过场景 | 10 |
| L114-119 | MPU 失败 → DTC SELF_TEST_FAIL + FAILED + return | `mpu_fail_aborts`、`oled_and_critical_fail` | 4 |
| L122 | `if (SelfTest_Hw_CanaryCheck() == E_OK)` | MPU 通过场景 | 10 |
| L123-125 | `SelfTest_StepResults |= SELFTEST_STEP_CANARY;` | CANARY 通过场景 | 8 |
| L126-131 | CANARY 失败 → DTC SELF_TEST_FAIL + FAILED + return | `canary_fail_aborts` | 2 |
| L134 | `if (SelfTest_Hw_RamPattern() == E_OK)` | CANARY 通过场景 | 8 |
| L135-137 | `SelfTest_StepResults |= SELFTEST_STEP_RAM;` | RAM 通过场景 | 6 |
| L138-143 | RAM 失败 → DTC SELF_TEST_FAIL + FAILED + return | `ram_fail_aborts` | 2 |
| L145 | `return result;`（末尾返回，全部通过或 OLED 失败继续后到达） | `selftest_all_pass`、`oled_fail_noncritical`、`rerun_resets_results` 第 2 次运行 | 6 |
| L146 | 函数结束 `}` | 同上（genhtml 归因于函数收尾块） | 8 |

#### Swc_SelfTest_GetResults（L152-155）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L153 | 函数入口 `{` | 全部场景（harness 输出前调用 + 运行前初值快照） | 42 |
| L154 | `return SelfTest_StepResults;` | 全部场景（`preResults`/`stepResults` 断言） | 42 |
| L155 | 函数结束 `}` | 全部场景 | 42 |

> 常量/宏定义（`SELF_TEST_PASSED/FAILED`、`SELFTEST_STEP_*`，头文件）与注释行为
> 非执行行，不计入行覆盖。`SelfTest_Hw_*` 的 `extern` 声明（L32-38）为非执行行，
> 由 harness 替身满足链接。genhtml 的行统计中 L146 的命中数高于 L145 属工具对
> 函数收尾块的归因差异，不构成覆盖缺口（两行均有命中，`LF=LH=77`）。

---

## 分支覆盖分析（100%，14/14）

| 分支 | 位置 | 覆盖状态 | 说明 |
|---|---|---|---|
| `SelfTest_Hw_SpiLoopback() == E_OK` | L61 | ✅ 两侧 | true 18 次（通过）/ false 5 次（`spi_fail_aborts`、`rerun_resets_results` 前置） |
| `SelfTest_Hw_CanLoopback() == E_OK` | L73 | ✅ 两侧 | true 16 次 / false 2 次（`can_fail_aborts`） |
| `SelfTest_Hw_NvmCheck() == E_OK` | L85 | ✅ 两侧 | true 14 次 / false 2 次（`nvm_fail_reports_crc`） |
| `SelfTest_Hw_OledAck() == E_OK` | L99 | ✅ 两侧 | true 10 次 / false 4 次（`oled_fail_noncritical`、`oled_and_critical_fail`） |
| `SelfTest_Hw_MpuVerify() == E_OK` | L110 | ✅ 两侧 | true 10 次 / false 4 次（`mpu_fail_aborts`、`oled_and_critical_fail`） |
| `SelfTest_Hw_CanaryCheck() == E_OK` | L122 | ✅ 两侧 | true 8 次 / false 2 次（`canary_fail_aborts`） |
| `SelfTest_Hw_RamPattern() == E_OK` | L134 | ✅ 两侧 | true 6 次 / false 2 次（`ram_fail_aborts`） |

> 全部 7 个分支点两侧均已覆盖，无无法覆盖的分支。

---

## 无法覆盖的代码说明

`Swc_SelfTest.c` **不存在无法覆盖的可执行代码**，行/分支/函数覆盖均为 100%：

1. **全部 7 个 `if (SelfTest_Hw_X() == E_OK)` 判断**：true（通过）侧由全通过场景
   及失败场景的前置步骤覆盖，false（失败）侧由对应单项失败场景覆盖，无死分支。
2. **OLED 非关键失败路径**：通过 `oled_fail_noncritical`（仅 OLED 失败，自检仍
   PASSED）与 `oled_and_critical_fail`（OLED 失败后后续关键项失败，双 DTC）两条
   独立路径覆盖，验证「非关键失败不阻断」与「非关键 + 关键同时失败」两种行为。
3. **每次运行的位掩码清零（L57）**：由 `rerun_resets_results` 的「失败 → 再次
   全通过」多阶段序列覆盖（第 2 次运行 stepResults 重新累积为 127）。
4. 不依赖 `#ifdef UNIT_TEST` 观测 getter（与 `Swc_Heartbeat` 等不同），
   `Swc_SelfTest_GetResults` 为既有公开 API，生产代码零改动即实现 100% 覆盖。

> **生产代码注释不一致说明**：`Swc_SelfTest.c` L91 注释「NVM failure: load defaults
> and report DTC, but continue」与 L94-95 实际行为（`result = SELF_TEST_FAILED;
> return result;`，NVM 失败立即终止并返回失败）不一致。E2E 用例 `nvm_fail_reports_crc`
> 按**实际行为**断言（result=0、DTC NVM_CRC_FAIL），并在 `cvc_selftest.feature` 中
> 通过场景描述「NVM 完整性失败上报 CRC DTC 并终止自检」固化该行为。注释与代码不符
> 属文档维护问题，建议在后续重构中修正注释（不影响覆盖）。

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---|
| 行 | 100%（77/77） | 0 行 | — |
| 分支 | 100%（14/14） | 0 个 | — |
| 函数 | 100%（2/2） | 0 个 | — |
