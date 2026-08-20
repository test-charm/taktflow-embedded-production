# BSW 现有测试分析与端到端测试扩展评估

## 范围

本文档仿照 `docs/asw-test-analysis.md`，总结本仓库围绕 AUTOSAR 风格 BSW 层的当前测试
全景，并评估将 `e2e-tests/src/test/resources/features/` 下 ASW 端到端（BDD）测试方式
扩展到 BSW 层代码的可行性与优先级。重点回答：

1. BSW 层有哪些模块、各模块的公开 API 与安全需求（SWR-BSW-xxx），
2. 每个模块现有的单元 / 集成 / SIL / HIL 覆盖情况，
3. 现有 ASW E2E 框架已间接覆盖了哪些 BSW 行为（哪些 feature 已经在驱动真实 BSW 代码），
4. 哪些 BSW 模块适合直接编写 `bsw_*.feature` 端到端测试，需要配套哪些 harness 与 mock，
5. 优先级排序与建议扩展顺序。

分析基于以下目录的当前仓库结构：

- `firmware/bsw/mcal/*`（MCAL：Can、Spi、Adc、Pwm、Dio、Gpt、Uart）
- `firmware/bsw/ecual/*`（ECUAL：CanIf、PduR、IoHwAb）
- `firmware/bsw/services/*`（Services：Com、Dcm、Dem、E2E、E2E_Sm、WdgM、BswM、
  NvM、SchM、Det、CanTp、CanSM、FiM、Xcp、Sil）
- `firmware/bsw/rte/*`（RTE：Rte）
- `firmware/bsw/os/*`（OS：bootstrap 调度器 / 任务 / 告警）
- `firmware/bsw/test/`、`test/unit/bsw/`（BSW 单元测试）
- `test/framework/test_int_*.c`（BSW 集成测试）
- `test/sil/`、`test/hil/`、`test/pil/`（xIL 场景）
- `e2e-tests/src/test/resources/features/`、`gateway/fault_inject/native/*_harness.c`
  （ASW E2E 框架）

---

## 执行摘要

BSW 层（约 10.4k 行 C，跨 MCAL / ECUAL / Services / RTE / OS 五层）已有**较全面的单元
测试**与**若干集成测试**：

- **单元测试**：`firmware/bsw/test/`（25 个手写测试）+ `test/unit/bsw/`（41 个文件，
  含生成的负向 / 全路径用例），覆盖几乎所有 BSW 模块（Com、E2E、WdgM、BswM、Dem、
  Dcm、CanIf、PduR、CanTp、Rte、Det、SchM、CanSM、FiM、IoHwAb、Xcp、MCAL 驱动）。
- **集成测试**：`test/framework/test_int_*.c`（如 `test_int_e2e_chain_asild.c` 覆盖
  E2E → Com → PduR → CanIf 真实模块链）。
- **SIL/HIL**：通过多 ECU 进程与真实台架验证总线可见的 BSW 行为（E2E CRC、bus-off
  恢复、RX 超时、DTC 广播、诊断栈等）。

**曾缺失的**是「ASW 式的可读 BDD 端到端测试」对 BSW 服务层的直接覆盖：现有 37 个
`.feature` 全部针对 **SWC（ASW）层**，它们通过 harness 的 mock 替换了 BSW 依赖
（Com / E2E / Dem / WdgM / Dio / Rte 等），因此**并没有直接驱动 `firmware/bsw/` 下的
真实 BSW 模块**——除 `sc_e2e.feature`（SC 平台自身的 E2E 实现）等少量边缘外，BSW
服务层本身没有专属 feature。

也就是说：

- ASW E2E 框架（native harness + BDD feature + 覆盖率回收）**天然可复用**到 BSW 服务层，
- 框架需要新增的是：每个 BSW 被测模块的 `*_harness.c`（链接真实 BSW 源文件 + mock
  其底层依赖）、`bsw_<module>.feature`、`test-design/bsw-<module>-e2e.md`、Java
  DTO/spec/Factory、`app.py` 端点与 `Dockerfile` 编译段，
- 最优先的候选是 **E2E、WdgM、BswM、Com、Dem、CanSM** 等安全关键且有状态机的
  服务模块；**不建议**对 MCAL 驱动、SchM、Det、OS bootstrap 做 E2E。

**首条 BSW E2E 链已落地**（2026-08-20）：`bsw_comcfg_cvc.feature`（30 场景 / 180 步）
直接读回 arxmlgen 生成的 `Com_Cfg_Cvc.c` 数据表（TX/RX PDU、信号位定义、结构不变量），
验证 DBC 一致性。全量 `./gradlew cucumber` 实测 **788 场景 / 4757 步全部通过**（无回归）。

**第二条 BSW E2E 链已落地**（2026-08-20）：`bsw_rtetaskbodies_cvc.feature`（6 场景 /
36 步）驱动 arxmlgen 生成的 `Rte_TaskBodies_Cvc.c` 六个 OSEK 任务体，验证 S-OS-11
runnable 分派顺序、WdgM checkpoint 位置与 TerminateTask 语义。**该生成文件是首个
进入覆盖率报告并带真实逐行数据的生成配置**（行/函数/区域覆盖 100%）。全量
`./gradlew cucumber` 实测 **794 场景 / 4793 步全部通过**（无回归）。

---

## 当前 BSW 测试清单

| 层级 | 位置 | 数量 | 主要目的 |
|---|---|---:|---|
| BSW 单元测试（手写） | `firmware/bsw/test/` | 25 个文件 | Unity + mock 验证单个 BSW 模块 |
| BSW 单元测试（含生成） | `test/unit/bsw/` | 41 个文件 | 手写 + 生成的负向/全路径/消息向量用例 |
| BSW 集成测试 | `test/framework/test_int_*.c` | 11 个文件 | 真实模块链（E2E→Com→PduR→CanIf、Dem→Dcm、WdgM、safe-state） |
| POSIX 多 ECU 集成 | `test/integration/`（`bsw/` 等子目录） | 若干 | vcan0 上多 ECU 进程，验证 BSW 驱动的总线行为 |
| SIL 场景 | `test/sil/scenarios/*.yaml` | 16 个 | E2E 拒绝、bus-off、看门狗超时、DTC 广播等系统级 BSW 行为 |
| HIL 场景 | `test/hil/scenarios/*.yaml`、`test/hil/test_*.py` | 37 + 11 | 真实 CAN 上的 E2E 时序、UDS 诊断栈、调度器 |
| BSW E2E（BDD） | `e2e-tests/src/test/resources/features/` | **2 个 feature / 36 场景**（`bsw_comcfg_cvc`、`bsw_rtetaskbodies_cvc`） | 生成配置读回（DBC 一致性）+ 生成任务体分派（S-OS-11） |

> 与 ASW 的对比：ASW 侧已有 37 条 E2E 链 / 758 场景；BSW 服务层这一「中间层」
> 已有两条 BSW E2E 链（`Com_Cfg` 配置一致性、`Rte_TaskBodies` 任务体分派）
> 落地，其余模块仍依赖单元测试 + SIL/HIL 黑盒测试，`panda/e2e-tests` 式的
> 可读 BSW E2E 层正在逐步补齐。

---

## 与 ASW E2E 框架的关系

现有 ASW E2E 框架已证明「原生 harness 链接真实生产代码 + BDD feature + llvm 覆盖率」
这一模式有效，且**已经在间接执行部分 BSW 逻辑**：

| 现有 feature | 驱动的真实代码 | 对 BSW 的间接覆盖 |
|---|---|---|
| `sc_e2e.feature` | `sc_e2e.c`（SC 自身 E2E） | 与 BSW `E2E.c` 逻辑同构（CRC-8/0x1D、alive、DataId）但**是不同文件** |
| `cvc_cvccom.feature` / `fzc_fzccom.feature` / `rzc_rzccom.feature` | SWC Com 层 | 通过 harness 的 mock PduR/CanIf/E2E，**未**链接真实 `Com.c`/`E2E.c` |
| `cvc_watchdog.feature` / `sc_watchdog.feature` | SWC 喂狗门控 | 是 SWC 门控逻辑，**非** BSW `WdgM.c` 的 SE 状态机 |
| `cvc_scheduler.feature` / `fzc_scheduler.feature` / `rzc_scheduler.feature` | SWC 调度表 | 是 SWC 静态表，**非** BSW `Rte.c` 的 `Rte_MainFunction` 分派 |
| `cvc_nvm.feature` / `fzc_nvm.feature` / `rzc_nvm.feature` | SWC NVM 持久化 | 通过 mock `NvM_ReadBlock/WriteBlock` 后端，**非**真实 `NvM.c` |

**结论**：ASW E2E 把 BSW 当 mock，因此 BSW 服务层的真实代码（尤其是 `Com.c` 的信号
打包/解包与 E2E 集成、`WdgM.c` 的 SE 状态机、`Dem.c` 的去抖/广播、`BswM.c` 的模式机、
`CanSM.c` 的 bus-off 恢复机）**未被任何 feature 直接覆盖**——这正是本扩展的目标。

---

## 各层详细说明

### 1. MCAL（微控制器抽象层）

统一风格：`*_Init(ConfigPtr)` + 操作 API + `GetStatus`，底层硬件经 `*_Hw_*` 回调
抽象（POSIX/HIL 各有实现），多带 `Det_ReportError` 防御守卫。

| 模块 | 行数 | 公开 API | 需求 |
|---|---|---:|---|
| `Can.c` | 293 | `Can_Init/DeInit/SetControllerMode/GetControllerMode/Write/MainFunction_Write/MainFunction_Read/MainFunction_BusOff/GetErrorCounters/GetControllerErrorState` | SWR-BSW-001..005 |
| `Spi.c` | 157 | `Spi_Init/DeInit/GetStatus/WriteIB/ReadIB/SyncTransmit` | SWR-BSW-006 |
| `Adc.c` | 127 | `Adc_Init/DeInit/GetStatus/StartGroupConversion/ReadGroup` | SWR-BSW-007 |
| `Pwm.c` | 112 | `Pwm_Init/DeInit/GetStatus/SetDutyCycle/SetOutputToIdle` | SWR-BSW-008 |
| `Dio.c` | 88 | `Dio_Init/DeInit/ReadChannel/WriteChannel/FlipChannel` | SWR-BSW-009 |
| `Gpt.c` | 142 | `Gpt_Init/DeInit/GetStatus/StartTimer/StopTimer/GetTimeElapsed` | SWR-BSW-010 |
| `Uart.c` | 129 | `Uart_Init/DeInit/GetStatus/ReadRxData/MainFunction` | QM |

**E2E 评估**：MCAL 是纯硬件/寄存器抽象，逻辑以状态与硬件回调为主，单元测试已覆盖；
上层（SWC 与 BSW 服务）通过 mock 使用它们。**不建议**做 BSW E2E（详见「不建议」）。

### 2. ECUAL（ECU 抽象层）

| 模块 | 行数 | 公开 API | 需求 |
|---|---|---:|---|
| `CanIf.c` | 118 | `CanIf_Init/Transmit/RxIndication/ControllerBusOff` | SWR-BSW-011/012 |
| `PduR.c` | 108 | `PduR_Init/CanIfRxIndication/Transmit/DcmTransmit/CanTpTransmit` | SWR-BSW-013 |
| `IoHwAb.c`（+Posix/Hil 变体） | 423+362+498 | `IoHwAb_Init`、`Read*`（踏板/转向/电流/温度/电压/制动/编码器/EStop）、`Set*`（电机 PWM/舵机/制动伺服） | SWR-BSW-014 |

**E2E 评估**：`CanIf`/`PduR` 是**路由骨架**，状态简单（init 守卫 + 表查找 + 转发），
但其「RX 查表→可选 E2E 回调→路由到 Com/Dcm/CanTp/Xcp」与「TX→Can_Write」链有
系统性价值；与 `Com.c` 合链可覆盖 BSW 通信主路径。IoHwAb 是传感器/执行器抽象，
行为偏硬件接口转发，由 HIL 覆盖更合理。

### 3. Services（服务层）—— E2E 扩展的核心

| 模块 | 行数 | 公开 API | 需求 |
|---|---|---:|---|
| `Com.c` | 839 | `Com_Init/SendSignal/ReceiveSignal/RxIndication/MainFunction_Tx/MainFunction_Rx/GetRxPduQuality/TriggerIPDUSend/FlushTxPdu` | SWR-BSW-015/016 |
| `E2E.c` | 332 | `E2E_Init/CalcCRC8/Protect/Check/SMInit/SMCheck` | SWR-BSW-023/024/025 |
| `E2E_Sm.c` | 124 | `E2E_Sm_Init/E2E_Sm_Check`（滑窗状态机） | SWR-BSW-023..025 |
| `WdgM.c` | 166 | `WdgM_Init/CheckpointReached/MainFunction/GetLocalStatus/GetGlobalStatus` | SWR-BSW-021/022 |
| `BswM.c` | 133 | `BswM_Init/MainFunction/RequestMode/GetCurrentMode` | SWR-BSW-022 |
| `Dem.c` | 350 | `Dem_Init/ReportErrorStatus/GetEventStatus/GetOccurrenceCounter/ClearAllDTCs/SetEcuId/SetDtcCode/SetBroadcastPduId/MainFunction` | SWR-BSW-018/019/020 |
| `Dcm.c` | 492 | `Dcm_Init/MainFunction/RxIndication/GetCurrentSession/TpRxIndication/IsSecurityUnlocked` | SWR-BSW-017 |
| `CanTp.c` | 462 | `CanTp_Init/MainFunction/RxIndication/Transmit/GetRxState/GetTxState` | SWR-BSW-042 |
| `CanSM.c` | 157 | `CanSM_Init/RequestComMode/ControllerBusOff/MainFunction/GetState/IsCommunicationAllowed` | SWR-BSW-026 |
| `FiM.c` | 101 | `FiM_Init/GetFunctionPermission/MainFunction` | SWR-BSW-027 |
| `NvM.c` | 141 | `NvM_ReadBlock/WriteBlock`（POSIX 文件后端 / 目标 stub） | SWR-BSW-031 |
| `SchM.c` | 131 | `SchM_Enter/Exit_Exclusive/GetNestingDepth` + `SchM_Timing*` | SWR-BSW-028/041 |
| `Det.c` + `Det_Callout_Sil.c` | 120+97 | `Det_ReportError/Det_ReportRuntimeError` | SWR-BSW-040 |
| `Xcp.c` | 618 | `Xcp_Init/RxIndication/IsConnected` | QM |
| `Sil_Time.c` | 143 | `Sil_Time_Init/Sleep/GetTickUs/GetScale` | SIL 专用 |

**E2E 评估**：本层是有状态、安全关键、跨模块交互最密集的一层，是 BSW E2E 的最佳
靶区（详见下文映射与优先级）。

### 4. RTE（运行时环境）

| 模块 | 行数 | 公开 API | 需求 |
|---|---|---:|---|
| `Rte.c` | 235 | `Rte_Init/Write/Read/MainFunction` | SWR-BSW-026/027 |

**E2E 评估**：`Rte_MainFunction` 内含优先级排序的 runnable 分派 + WdgM checkpoint，
是「调度正确性」的权威实现。ASW 侧 scheduler feature 只覆盖 SWC 静态表，**未**驱动
真实分派循环，存在直接 E2E 价值。

### 5. OS（bootstrap OSEK 风格内核）

`os/bootstrap/src/` 下 Os_Task / Os_Scheduler / Os_Alarm / Os_Event / Os_Resource /
Os_MemProt / Os_ServiceProt / Os_TimingProt / Os_Core / Os_Interrupt 等（SWR-BSW-050）。
已有独立测试目录 `os/bootstrap/test/`（约 30 个 `test_Os_*` 文件）。

**E2E 评估**：OS 有专属单元/移植测试与 SIL 覆盖，且与 CAN 服务层正交；**不建议**纳入
本 E2E 框架。

---

## BSW 模块到测试的映射

> 表内「E2E 候选」指在 ASW E2E 框架中新增 `bsw_<module>.feature` 的直接可测性：
> 驱动真实 BSW 源文件、注入 phase 脚本、经 harness mock 观测内部状态/底层调用。

| 模块 | 现有单元测试 | 现有集成/SIL/HIL | E2E 候选 | 主要被测行为 | 需要的 harness mock |
|---|---|---:|---|---|---|
| `E2E.c` | `test_E2E_asild.c`、`test_E2E_negative_generated.c`、`test_E2E_messages_generated.c` | `test_int_e2e_chain_asild.c`、`sil_009_e2e_corruption.yaml`、`test_hil_e2e.py` | **高** | CRC-8 已知向量、Protect/Check 往返、alive 回绕 15→0、DataId 校验、MaxDelta、NULL/长度守卫（Det）、SM NODATA/INIT/VALID/INVALID 迁移与窗口 | `Det_ReportError` stub、公开 `E2E_StateType` 直接观测 |
| `E2E_Sm.c` | `test_E2E_Sm_asild.c`、`test_E2E_SM_full_generated.c` | 同上（经 Com 链） | **高** | 滑窗校验（WindowSize 饱和、INIT→VALID→INVALID）、窗口缓冲 | 直接公开结构体观测 |
| `WdgM.c` | `test_WdgM_asild.c`、`test_WdgM_generated.c` | `test_int_wdgm_supervision_asild.c`、`sil_005_watchdog_timeout_cvc.yaml`、`test_hil_wdgm.py` | **高** | SE alive 监控（ExpectedAliveMin/Max）、FAILED→EXPIRED 容差升级、全局状态、Dio_FlipChannel 喂狗、Dem 上报、未初始化/越界守卫 | `Dio_FlipChannel`、`Dem_ReportErrorStatus` mock、UNIT_TEST getter 观测 SE 状态 |
| `BswM.c` | `test_BswM_asild.c` | `test_int_safe_state_asild.c`、SIL 启动/故障场景 | **高** | 前向模式机 STARTUP→RUN→DEGRADED→SAFE_STOP→SHUTDOWN、非法迁移拒绝、动作回调按模式执行、NULL 守卫 | 动作回调计数器、`Det` stub、公开 `BswM_GetCurrentMode` |
| `Com.c` | `test_Com_asild.c`、`test_Com_signals_generated.c`、`test_Com_negative_generated.c`、`test_Com_TxAutoPull_asild.c` | `test_int_e2e_chain_asild.c`、`test_cvc_full.py` 等 | **高** | 信号打包/解包（8/16/跨字节）、TX 周期调度（PERIODIC/DIRECT/MIXED/事件）、启动延迟、RX 超时→质量 TIMED_OUT→RTE 状态、E2E TX protect / RX check+SM+DEM、TX 卡死检测 | mock `PduR_Transmit`/`Rte_*`/`Dem_ReportErrorStatus`，`-DUNIT_TEST` 直接观测 `com_tx_pdu_buf` |
| `Dem.c` | `test_Dem_asilb.c`、`test_Dem_generated.c` | `test_int_dem_to_dcm_asilc.c`、SIL DTC 场景 | **高** | 去抖计数（FAILED 累加/PASSED 递减、阈值确认）、statusByte 位演变、occurrence 计数、NvM 持久化往返、0x500 广播帧打包与去重、ClearAllDTCs | mock `NvM_Read/WriteBlock`、`PduR_Transmit`、UNIT_TEST 观测 events/广播表 |
| `CanSM.c` | `test_CanSM_asild.c`、`test_CanSM_full_generated.c` | `sil_004_can_busoff_fzc.yaml` | **高** | L1/L2 两级恢复（timer→attempt→升级→永久 BUS_OFF）、RequestComMode 状态、`Can_SetControllerMode` 调用、IsCommunicationAllowed | mock `Can_SetControllerMode`、UNIT_TEST getter 观测 level/attempt/timer |
| `CanTp.c` | `test_CanTp_asild.c` | HIL UDS 链 | **中** | SF/FF/CF/FC 分帧与重组、序列号、N_Cr/N_Bs 超时、STmin、FlowControl OVERFLOW | mock `PduR_CanTpTransmit`/`Dcm_TpRxIndication`、公开 GetRx/TxState |
| `FiM.c` | `test_FiM_generated.c` | 间接 | **中** | Dem 状态掩码→函数抑制、权限缓存、MainFunction 更新 | mock `Dem_GetEventStatus`、公开 `GetFunctionPermission` |
| `Rte.c` | `test_Rte_asild.c`、`test_Rte_generated.c` | `test_hil_scheduler.py` | **中** | 信号读写边界、runnable 优先级分派顺序、tick 周期调度、WdgM checkpoint、NULL/越界守卫 | mock runnable 入口计数器 + `WdgM_CheckpointReached` |
| `CanIf.c`/`PduR.c` | `test_CanIf_asild.c`、`test_PduR_asild.c` | `test_int_e2e_chain_asild.c` | **中** | RX 查表路由（→Com/Dcm/CanTp/Xcp）、未知 ID 静默丢弃、E2E 回调拒绝丢帧、TX 转发 `Can_Write` | mock `Can_Write`、`Com_RxIndication` 等目标、E2E 回调 |
| `Dcm.c` | `test_Dcm_qm.c` | `test_hil_uds.py` | **中** | UDS 服务分发（10/11/14/19/22/27/2E/3E）、NRC 构造、会话/安全访问 | mock DID 表回调 + 诊断通道输入 |
| `NvM.c` | —（间接 `test_Com_asild.c`） | SIL 持久化 | **低** | 文件块读写、路径构建、首启 E_OK、NULL 守卫 | 临时目录文件系统 |
| `Xcp.c` | `test_XCP_security_generated.c` | 台架 | 低（QM） | XCP-over-ETH 最小服务 | UDP mock |
| `SchM.c`/`Det.c` | `test_SchM_asild.c`、`test_Det_asild.c`、`test_Det_Callout_Sil_asild.c` | — | **不建议** | 临界区嵌套/IRQ、DET 记录 | 平台/日志面，单元已覆盖 |
| MCAL 驱动 | 各有 `test_<module>_asil*.c` | HIL | **不建议** | 硬件寄存器/状态 | 硬件面，HIL 更合适 |
| OS bootstrap | `os/bootstrap/test/test_Os_*.c`（约 30） | SIL | **不建议** | 任务/告警/调度/内存保护 | 独立 OS 测试套件更合适 |

---

## 主要 BSW E2E 覆盖缺口

1. **BSW 服务层专属 feature 仍稀缺**：`Com.c`、`E2E.c`、`WdgM.c`、`BswM.c`、`Dem.c`、
   `CanSM.c`、`CanTp.c`、`FiM.c`、`Rte.c` 均无 `bsw_*.feature` 直接驱动；它们目前
   只被「SWC E2E（BSW 作 mock）」与「SIL/HIL 黑盒」间接覆盖。**已闭合的缺口**是
   「生成配置数据表」：`bsw_comcfg_cvc.feature` 已直接读回 arxmlgen 生成的
   `Com_Cfg_Cvc.c`（TX/RX PDU、信号位定义、结构不变量），验证 DBC 一致性。
2. **状态机/时序语义未在 BDD 层固化**：`WdgM` 的 FAILED→EXPIRED 容差、`BswM` 的
   前向模式机、`CanSM` 的 L1/L2 升级、`E2E_SM` 的滑窗——这些纯状态迁移最适合用
   feature 的可读步骤固化，目前只存在于 Unity 测试中。
3. **Com 的信号打包与 E2E 集成本体未被直接观测**：SWC E2E 通过 mock RTE/PduR 观测
   SWC 输出，但真实 `Com.c` 的位打包、周期调度、RX 质量状态机（FRESH/E2E_FAIL/
   TIMED_OUT）与 TX 卡死检测未被 feature 直接验证。
4. **跨模块链只被 SIL 覆盖**：E2E→Com→PduR→CanIf 链有 `test_int_e2e_chain_asild.c`
   集成测试，但没有「以 BDD 步骤表达」的链级 feature（如注入损坏帧→断言
   `Com_GetRxPduQuality==E2E_FAIL`→断言 DEM 上报）。

---

## 面向 BSW 的 E2E 框架适配

每个新增 BSW feature 需配套以下文件（与 ASW 链完全一致，仅目标代码从
`firmware/ecu/*/src/*.c` 换成 `firmware/bsw/**/*.c`）：

1. **原生 harness** `gateway/fault_inject/native/bsw_<module>_harness.c`
   - 直接链接真实 BSW 源文件（如 `firmware/bsw/services/E2E/src/E2E.c`），
   - 以 stdin phase 脚本驱动公开 API（与 `sc_watchdog_harness.c` 相同的 phase 模型），
   - 在 harness 内 mock 底层依赖：`Det_ReportError`（所有 BSW 模块必调）、`SchM_*`
     （POSIX 下 `SchM.h` 已是 no-op，可直接用）、以及各模块专属依赖
     （`Dio_FlipChannel`、`Dem_ReportErrorStatus`、`PduR_Transmit`、`Can_Write`、
     `NvM_*`、`Rte_*` 等），
   - 观测内部静态状态：优先复用公开 API（`WdgM_GetLocalStatus`、`BswM_GetCurrentMode`、
     `CanTp_GetRxState`、`CanSM_GetState`、`FiM_GetFunctionPermission`、
     `Com_GetRxPduQuality`、`E2E` 的 `E2E_StateType` 结构体）；必要时按既有先例增加
     `#ifdef UNIT_TEST` 保护的观测 getter（生产固件不含）。
2. **Feature 文件** `e2e-tests/src/test/resources/features/bsw_<module>.feature`
   - 复用「背景: 假如存在 / 当POST /api/test/bsw/<module> / 那么response should be」
     三段式。
3. **设计文档** `e2e-tests/src/test/resources/test-design/bsw-<module>-e2e.md`
   - 复用 `sc-watchdog-e2e.md` 结构（被测功能、流程图、输入/输出因子、用例表、
     覆盖率实测、无法覆盖代码说明）。
4. **Java 侧**：`dto/Bsw<Module>Setup/Phase.java`、`spec/Bsw<Module>Setups/Phases.java`、
   `Factories.java`、`EntityFactory.java` 登记。
5. **服务端**：`gateway/fault_inject/app.py` 增加 `Bsw<Module>Phase/SetupBody/RunBody`
   模型、`/api/test/bsw/<module>/setup` 与 `/api/test/bsw/<module>` 端点、`*_phase_to_line`
   序列化、`_<module>_HARNESS` 常量与 `_generate_coverage_html` 登记。
6. **构建**：`gateway/fault_inject/Dockerfile` 增加 `bsw_<module>_harness` 编译段，
   链接 `firmware/bsw/.../<module>.c` + harness + 必要的 mock/stub 源；沿用
   `-fprofile-instr-generate -fcoverage-mapping` 与 `-DUNIT_TEST`。

> **覆盖率导出正则注意**：`_generate_coverage_html` 已支持**按 harness 覆盖
> ignore 正则**（harnesses 列表的 tuple 第三元素）。默认忽略
> `/app/fault_inject/.*`（harness 测试代码）与 `/app/firmware/bsw/.*`（ASW
> harness 链接的 BSW 依赖）；BSW E2E harness 应按需覆盖——链接真实 BSW 服务
> 模块（有可执行代码）时去掉 `/app/firmware/bsw/.*`，使 BSW 生产模块进入报告。
>
> **纯数据配置（如 `Com_Cfg_<Ecu>.c`）无函数可插桩**：clang 覆盖率只对函数
> 计数，生成的 `static const` 配置表是编译期常量、零可执行代码，`llvm-cov export`
> 对该文件返回空轨迹 → 纯数据 harness **不注册进覆盖率 harnesses 列表**
> （`app.py` 中注有 NOTE），避免空 lcov 轨迹；其数据表以「访问覆盖」口径衡量
> （每条目是否被读回/遍历），由 feature 的读回场景与结构不变量共同保证。harness
> 测试代码按约定一律不进 HTML 报告（`/app/fault_inject/.*` 统一忽略），其自身
> 覆盖（`bsw_comcfg_harness.c` 行 90.23% / 函数 100%）仅经 `llvm-cov` 直接统计
> 记录在设计文档中。
>
> **生成配置代码（如 `Rte_TaskBodies_<Ecu>.c`）有函数、可直接进报告**：该文件
> 位于 `firmware/ecu/<ecu>/cfg/`，不在默认忽略正则范围内，harness 按默认忽略
> 注册即可使生成文件进入报告（`bsw_rtetaskbodies_cvc` 实测
> `Rte_TaskBodies_Cvc.c` 行/函数/区域覆盖 **100%**，见
> `e2e-tests/build/coverage/firmware/ecu/cvc/cfg/Rte_TaskBodies_Cvc.c.gcov.html`）。
> 因此生成文件需先判断「纯数据」还是「含函数」：前者走访问覆盖口径 + harness
> 代码进报告，后者直接产生真实逐行覆盖率。

> 与 ASW harness 的区别：BSW harness 通常需要链接 **Det.c**（或提供 `Det_ReportError`
> 计数 stub）与 **SchM.c**（UNIT_TEST 变体），并 mock 数量更少的底层依赖，因为 BSW
> 模块自身就是「底层」。部分模块（Com）在 `UNIT_TEST` 下已将内部 PDU 缓冲非静态化
> （`com_tx_pdu_buf`/`com_rx_pdu_buf`），可零改动直接白盒观测。

---

## 扩展优先级与建议顺序

> **已完成**：
> - `Com_Cfg` 生成配置一致性（`bsw_comcfg_cvc.feature`，30 场景，2026-08-20）。
>   纯数据表读回，沉淀了「BSW harness + feature + Java + app.py + Dockerfile」
>   全链路模板。
> - `Rte_TaskBodies` 生成任务体分派（`bsw_rtetaskbodies_cvc.feature`，6 场景，
>   2026-08-20）。首个**带真实逐行覆盖率的生成文件**（`Rte_TaskBodies_Cvc.c`
>   行/函数/区域 100%），证明「生成配置代码」与「纯数据配置」需区分覆盖口径。

### 高优先级（ASIL D 安全关键、有状态机、与 SWC E2E 互补）

| 优先级 | 模块 | Feature 建议 | 关键场景 |
|---|---|---|---|
| 1 | `E2E` | `bsw_e2e.feature` | CRC-8 已知向量（"123456789"→0x4B）、Protect→Check 往返、alive 0→15→0 回绕、DataId 失配/CRC 损坏/WRONG_SEQ/REPEATED、NULL 与长度守卫（Det 计数）、E2E_SM 窗口迁移 |
| 2 | `WdgM` | `bsw_wdgm.feature` | checkpoint 递增、ExpectedAliveMin/Max 越界→FAILED、FailedRefCycleTol 内失败计数→EXPIRED、Dem 事件 15 上报、全局状态 OK/FAILED、Dio_FlipChannel 喂狗/饿死、重复 Init、越界/未初始化守卫 |
| 3 | `BswM` | `bsw_bswm.feature` | 合法前向迁移全路径、非法迁移拒绝（状态不变+Det）、SHUTDOWN 终态无迁出、同模式 no-op、ModeActions 按模式回调计数、NULL 配置守卫 |
| 4 | `Com` | `bsw_com.feature` | 8/16/跨字节打包解包往返、PERIODIC 周期调度边界、DIRECT/MIXED/事件触发、启动延迟抑制、RX 超时→TIMED_OUT→RTE COMM_TIMEOUT、E2E 保护 TX（字节 0/1 布局）、E2E 失败→quality E2E_FAIL+DEM+信号清零、恢复→DEM PASSED |
| 5 | `Dem` | `bsw_dem.feature` | FAILED 去抖→TEST_FAILED/PENDING→阈值→CONFIRMED、occurrence 计数、PASSED 递减/清除、NvM 持久化往返（重新 Init 恢复计数）、0x500 广播帧字节布局与去重、ClearAllDTCs |
| 6 | `CanSM` | `bsw_cansm.feature` | L1 快速恢复边界（attempt≤Max）、L1 耗尽升级 L2、L2 耗尽永久 BUS_OFF、bus-off 后 timer 推进、`Can_SetControllerMode` 调用序列 |

### 中优先级（有价值但非最紧迫）

| 优先级 | 模块 | Feature 建议 | 关键场景 |
|---|---|---|---|
| 7 | `CanTp` | `bsw_cantp.feature` | SF 直通、FF+CF 重组、FC CTS/Wait/Overflow、序列号校验、N_Cr/N_Bs 超时、STmin、Tx 分帧（128B 上限） |
| 8 | `FiM` | `bsw_fim.feature` | Dem 状态掩码命中→抑制、未命中→放行、MainFunction 缓存刷新、权限默认 TRUE、越界/未初始化守卫 |
| 9 | `Rte` | `bsw_rte.feature` | 信号读写边界与初值、runnable 优先级排序分派、tick 周期分派计数、WdgM checkpoint 每 SE 一次、NULL/越界守卫 |
| 10 | `CanIf`/`PduR` | `bsw_canif_pdur.feature` | RX 查表→Com/Dcm/CanTp/Xcp 路由、未知 ID 静默、E2E 回调拒绝丢帧、TX 转发 `Can_Write`、`CanIf_ControllerBusOff`→CanSM |

### 低优先级 / 不建议

| 模块 | 说明 |
|---|---|
| `NvM` | POSIX 文件后端逻辑简单（路径构建+open/read/write），目标 stub 无逻辑；SIL 持久化已覆盖 |
| `Xcp` | QM 且已有生成测试；台架/调试面 |
| `Dcm` | QM（诊断服务分发），HIL UDS 已覆盖主要服务；可作为中后期可选 |
| `SchM`/`Det` | 平台临界区 / 开发期日志，单元测试已覆盖，且是其它 BSW 的 mock 依赖 |
| MCAL 驱动 | 硬件寄存器面，HIL 是更合适的验证层 |
| OS bootstrap | 有独立 OS 测试套件与 SIL 覆盖，正交于 CAN 服务层 |

### 建议扩展顺序

> `E2E → WdgM → BswM → Com → Dem → CanSM`（高优先级），随后按资源评估
> `CanTp → FiM → Rte → CanIf/PduR`（中优先级）。

先做 `E2E` 与 `WdgM` 收益最大：模块小、状态清晰、公开 API 完整，可快速跑通
「BSW harness + feature + 覆盖率」全链路并沉淀模板；`Com` 最复杂（839 行、多依赖），
建议在其后投入，可参照 `UNIT_TEST` 下已非静态化的 PDU 缓冲降低白盒观测成本。

每个新 feature 需配套：`gateway/fault_inject/native/bsw_<module>_harness.c`、
`features/bsw_<module>.feature`、`test-design/bsw-<module>-e2e.md`、Java
DTO/spec/Factory、`app.py` 端点与 `Dockerfile` 编译段。

---

## 无法覆盖 / 风险说明（按既有先例）

参照 ASW E2E 各设计文档的「无法覆盖的代码说明」惯例，BSW E2E 需提前识别的
编译期/不可达代码：

- **`#ifdef PLATFORM_HIL` 分支**：`E2E.c` 的 alive 容差放宽（`delta > 8u`）、`Com.c`
  无、`CanSM.c` 无。harness 以生产配置编译（不定义 HIL）时被预处理器排除，由 HIL
  测试覆盖。
- **`#ifdef SIL_DIAG` 日志块**：`Dem.c`、`Det_Callout_Sil.c` 的 fprintf 分支，预处理器
  排除，不计入行统计。
- **防御性守卫的不可达侧**：与 `Swc_Watchdog`/`Swc_Scheduler` 同理，`WdgM`/`BswM`/
  `Com`/`Rte` 的 `ConfigPtr == NULL_PTR` 守卫经「Init 先检查 NULL」不变式，其 true 侧
  在重复 Init 场景中**可**构造（对 NULL Init 断言 Det 错误+失败态），与 SWC 的
  `Wdg_Initialized==TRUE⟹CfgPtr!=NULL` 不同——BSW 模块普遍允许「Init(NULL)」路径，
  这反而是负向用例，覆盖更容易。
- **`NvM` 目标 stub**：`#else` 分支仅目标构建编译，harness 走 POSIX 分支，stub 由
  HIL 目标验证。
- **`SchM`**：UNIT_TEST 变体用布尔跟踪 IRQ 状态，非 UNIT_TEST 才调 `__disable_irq`；
  E2E harness 应编译 UNIT_TEST 变体（无硬件指令）。
- **纯数据配置（`Com_Cfg_<Ecu>.c`）无函数可插桩**（首条 BSW E2E 实测结论）：
  clang 覆盖率只对函数计数，生成的 `static const` 配置表是编译期常量、零可执行
  代码，对该文件 `llvm-cov export` 返回空轨迹 → 纯数据 harness **不注册进
  覆盖率 harnesses 列表**（加入只会产生空 lcov 轨迹）；数据表本身以「数据表
  访问覆盖」口径衡量（每条目是否被读回/遍历）。harness 测试代码（含
  `bsw_comcfg_harness.c`，实测行 90.23% / 分支 75.96% / 函数 100%）一律不进
  HTML 报告，其覆盖仅经 `llvm-cov` 直接统计记录在设计文档。未覆盖行均为
  「合法输入下不可达」的枚举分支（CVC 配置无 MIXED/SINT16/BOOL）、结构不变量
  失败分支（配置正确时恒假，正是测试要防护的 codegen bug）与无效输入错误路径。
- **生成配置代码（`Rte_TaskBodies_<Ecu>.c`）可直接进报告**（第二条 BSW E2E
  实测结论）：有函数的生成文件按默认忽略注册即可，报告显示真实逐行覆盖
  （`Rte_TaskBodies_Cvc.c` 行/函数/区域 100%）。Idle 任务的死循环在 harness 用
  `setjmp`/`longjmp` 限定迭代次数覆盖，属测试专用机制（生产代码不变）。

---

## 结论

BSW 层的单元与集成测试覆盖扎实，但缺少与 ASW 对等的**可读 BDD 端到端测试层**。
现有 ASW E2E 框架（native harness + feature + 覆盖率）可直接复用，最大缺口集中在
**Services 层的有状态安全关键模块**（E2E、WdgM、BswM、Com、Dem、CanSM）。已落地
两条 BSW E2E 链（`bsw_comcfg_cvc` 配置一致性、`bsw_rtetaskbodies_cvc` 任务体分派）
并验证全量无回归，后者更让**生成配置代码**首次以 100% 真实逐行覆盖率进入报告。
按 `E2E → WdgM → BswM → Com → Dem → CanSM →（CanTp/FiM/Rte/CanIf+PduR）` 的顺序扩展，
可在保持「DBC 为真、生成不手改、平台抽象、fail-closed」原则的同时，把 BSW 服务层
的关键行为以业务可读的 feature 固化下来，补足当前 SIL/HIL 黑盒测试与 Unity 单元测试
之间的中间层。
