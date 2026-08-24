# BSW 测试分析与端到端测试标准（真端到端）

## 范围

本文档仿照 `docs/asw-test-analysis.md`，总结本仓库围绕 AUTOSAR 风格 BSW 层的当前
测试全景，并确立 **BSW 端到端测试的标准做法：真端到端**——被测系统是真实编译、
真实插桩的 SIL ECU 二进制，测试通过 BDD feature 断言其在 vcan0 总线上可观测的真实
行为。重点回答：

1. BSW 层有哪些模块、各模块的公开 API 与安全需求（SWR-BSW-xxx），
2. 每个模块现有的单元 / 集成 / SIL / HIL 覆盖情况，
3. **什么行为适合做"真端到端"（总线可观测），什么行为只能留在单元 / SIL / HIL / 
   工具层（白盒状态机、内部配置参数）**，
4. 新增一个 BSW 真端到端 feature 的标准步骤（gateway op → feature → 插桩 ECU →
   覆盖率回收），
5. 优先级排序与建议扩展顺序。

分析基于当前仓库结构：

- `firmware/bsw/mcal/*`（MCAL：Can、Spi、Adc、Pwm、Dio、Gpt、Uart）
- `firmware/bsw/ecual/*`（ECUAL：CanIf、PduR、IoHwAb）
- `firmware/bsw/services/*`（Services：Com、Dcm、Dem、E2E、E2E_Sm、WdgM、BswM、
  NvM、SchM、Det、CanTp、CanSM、FiM、Xcp、Sil）
- `firmware/bsw/rte/*`（RTE：Rte）、`firmware/bsw/os/*`（OS：bootstrap 调度器）
- `firmware/bsw/test/`、`test/unit/bsw/`（BSW 单元测试）
- `test/framework/test_int_*.c`（BSW 集成测试）
- `test/sil/`、`test/hil/`、`test/pil/`（xIL 场景）
- `e2e-tests/src/test/resources/features/`、`gateway/fault_inject/`
- `firmware/platform/posix/`、`docker/Dockerfile.vecu`、`docker/docker-compose.dev.yml`
  （真端到端测试的插桩 ECU 基础设施）

---

## 执行摘要

BSW 层（约 10.4k 行 C，跨 MCAL / ECUAL / Services / RTE / OS 五层）已有**较全面的
单元测试**与**若干集成测试**：

- **单元测试**：`firmware/bsw/test/`（25 个手写测试）+ `test/unit/bsw/`（41 个文件，
  含生成的负向 / 全路径用例），覆盖几乎所有 BSW 模块（Com、E2E、WdgM、BswM、Dem、
  Dcm、CanIf、PduR、CanTp、Rte、Det、SchM、CanSM、FiM、IoHwAb、Xcp、MCAL 驱动）。
- **集成测试**：`test/framework/test_int_*.c`（如 `test_int_e2e_chain_asild.c` 覆盖
  E2E → Com → PduR → CanIf 真实模块链）。
- **SIL / HIL**：通过多 ECU 进程与真实台架验证总线可见的 BSW 行为（E2E CRC、bus-off
  恢复、RX 超时、DTC 广播、诊断栈等）。

### BSW 端到端测试标准（2026-08-24 确立）

BSW 层的 BDD 端到端测试**一律走"真端到端"**，不再走"harness 链接真实模块 + mock
底层依赖"的白盒模式。定义如下：

- **被测系统**：真实 SIL ECU 二进制（`Dockerfile.vecu` 构建、`LLVM_COV=1` 用
  clang 插桩），运行在 `docker compose -f docker/docker-compose.dev.yml up` 的
  SIL 栈中，7 个 vECU 共享宿主机 vcan0 总线。
- **一端（输入/激励）**：向真实系统注入——观测类（bus-probe 只读总线）、时序类
  （cadence 测周期）、触发类（ftti 经 UDP DIO 注入 E-Stop，与 SIL-003 同一侧信道）。
- **另一端（断言）**：vcan0 上的**真实帧**，逐项与 DBC 唯一真源
  （`gateway/taktflow_vehicle.dbc`）比对——DLC、周期、E2E 头（DataId/Alive/CRC）
  与 cantools 解码出的信号值；所有 BSW 层（Com / E2E / PduR / CanIf / MCAL /
  RTE / OS）都在链路中以**真实代码**运行，不 mock。
- **覆盖率**：真实 ECU 二进制带 LLVM 插桩，运行期周期 flush `.profraw` 到共享
  `cov` 卷；网关 `_collect_ecu_coverage_info` 合并进 lcov 报告（对 BSW 不做
  ignore）。**总线测试本身 = 覆盖来源**，无需任何人为"覆盖驱动"。

**两条 BSW 真端到端链已落地**（2026-08-24 转换）：

| feature | 场景 | 断言内容 |
|---|---|---|
| `bsw_comcfg_cvc.feature` | 6 | bus-probe 观测 CVC 真实帧：CVC_Heartbeat / Vehicle_State / Torque_Request / Steer_Command 的 DLC、cadence、E2E dataId·alive·CRC；Body_Control_Cmd 无 E2E；未知消息 fail-closed |
| `bsw_rtetaskbodies_cvc.feature` | 4 | cadence（10ms 任务驱动 VS/Torque、50ms 驱动心跳）；ftti（UDP 注入 E-Stop → 0x001 广播 FTTI 内上总线，锁存流 E2E 有效，结束后重启 CVC 清锁存） |

全量 `./gradlew cucumber` 实测 **768 场景 / 4637 步全部通过**（无回归）。覆盖报告
`e2e-tests/build/coverage/` 中，BSW 模块（`bsw/services/Com/src/Com.c`、
`bsw/services/E2E/src/E2E.c`、`bsw/ecual/PduR`、`bsw/ecual/CanIf`、
`bsw/mcal/*`、`bsw/rte/Rte.c`、OS、`platform/posix/src/Can_Posix.c` 等）均来自
**真实 ECU 在总线测试中的执行**。

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
| BSW E2E（BDD，真端到端） | `e2e-tests/src/test/resources/features/` | **2 个 feature / 10 场景**（`bsw_comcfg_cvc`、`bsw_rtetaskbodies_cvc`） | vcan0 总线观测：帧 DLC/周期/E2E、cadence、E-Stop FTTI |

> 覆盖面说明：总线测试直接驱动真实 ECU，因此 BSW 服务的最大头（Com 打包/调度、
> E2E 保护、PduR/CanIf 路由、MCAL 收发、RTE 分派）在报告中出现真实的逐行覆盖；
> 白盒状态机语义（见「缺口」）仍由单元测试与 SIL/HIL 承担。

---

## 与 ASW E2E 框架的关系

ASW 侧 37 条 `.feature` 通过 **native harness** 链接 SWC 并 **mock BSW 依赖**
（Com / E2E / Dem / WdgM / Dio / Rte 等），因此它们**并不驱动真实 BSW 模块**：

| 现有 feature | 驱动的真实代码 | 对 BSW 的间接覆盖 |
|---|---|---|
| `sc_e2e.feature` | `sc_e2e.c`（SC 自身 E2E） | 与 BSW `E2E.c` 逻辑同构但**是不同文件** |
| `cvc_cvccom.feature` / `fzc_fzccom.feature` / `rzc_rzccom.feature` | SWC Com 层 | mock PduR/CanIf/E2E，**未**链接真实 `Com.c`/`E2E.c` |
| `cvc_watchdog.feature` / `sc_watchdog.feature` | SWC 喂狗门控 | **非** BSW `WdgM.c` 的 SE 状态机 |
| `cvc_nvm.feature` / `fzc_nvm.feature` / `rzc_nvm.feature` | SWC NVM 持久化 | mock `NvM_*`，**非**真实 `NvM.c` |

**BSW 真端到端与 ASW E2E 的关键区别**：BSW 真端到端**不复用**「harness 链接 BSW +
mock 依赖 + 覆盖率」的白盒模式；它驱动**真实 SIL ECU 二进制**，feature 断言的是
**总线上的真实帧**。因此：

- ASW harness 框架（`gateway/fault_inject/native/*`）保留给 **ASW 层**使用；
- BSW 层的 BDD 测试统一走本文档第五节「真端到端标准」；
- 旧式「生成配置读回 / 任务体骨架驱动」的 BSW harness（`bsw_comcfg_cvc_harness`、
  `bsw_rtetaskbodies_cvc_harness`）已随转换**删除**（无任何 feature/代码使用）。

---

## 各层详细说明

### 1. MCAL（微控制器抽象层）

Can / Spi / Adc / Pwm / Dio / Gpt / Uart 驱动，POSIX 变体（`Can_Posix.c` 等）以
SocketCAN / FD 模拟硬件。单元测试充分（`test_<module>_asil*.c`）。真端到端下，这些
驱动以**真实代码**参与 ECU 收发（如 `Can_Posix.c` 在总线上真实收发、`Dio_Posix`
响应 UDP 注入），并进入覆盖报告；硬件寄存器面仍以 HIL 为准。

### 2. ECUAL（ECU 抽象层）

`CanIf.c` / `PduR.c` / `IoHwAb.c`：路由与硬件抽象。真端到端中它们运行在真实链路里
（静默丢未知 ID、E2E 回调拒帧、TX 转发 `Can_Write`），其行为通过总线帧的"存在/缺失"
可观测。`IoHwAb` 与 DIO 注入（E-Stop、pedal）是可观测激励的物理入口。

### 3. Services（服务层）—— 真端到端的核心受益层

`Com`（信号打包/周期调度/RX 质量）、`E2E`/`E2E_Sm`（帧保护/滑窗）、`WdgM`、
`BswM`、`Dem`、`CanSM`、`CanTp`、`FiM`、`NvM`、`Det`、`SchM`、`Xcp`。其中：

- **总线可观测**：`Com` 周期帧的 cadence / DLC / E2E 头、`E2E` 校验生效（损坏帧被拒 →
  行为级后果）、`Dem` 的 DTC 广播、`CanSM` 的 bus-off 后果、喂狗存活（无复位）——
  这些适合**真端到端**断言；
- **白盒状态机**：`E2E_SM` 滑窗迁移、`WdgM` FAILED→EXPIRED 容差、`BswM` 模式迁移表、
  `CanSM` 内部 L1/L2 level——总线层面只能看到"后果"，内部迁移细节由**单元测试**
  固化，不因追求 feature 而退化为白盒 harness。

### 4. RTE（运行时环境）

`Rte.c` 的信号读写、runnable 分派、tick 调度。真端到端中 RTE 以真实代码运行
（`Rte_MainFunction` 在 legacy 主循环驱动全部 runnable），行为后果（周期帧按时出现、
E-Stop 传播时机）在总线上可观测。

### 5. OS（bootstrap OSEK 风格内核）

任务/告警/调度/内存保护。POSIX SIL 走 legacy 主循环（`Rte_MainFunction` 直接分派），
**不执行生成的 OSEK 任务体骨架**（`Rte_TaskBodies_Cvc.c` 在 posix 镜像中不运行）。
OS 内核与 OSEK 骨架的结构覆盖由独立 OS 测试套件 / OSEK 目标层 / 单元层负责。

---

## BSW 模块到测试的映射

> 「真端到端可观测」指：该行为能否通过**真实帧 / 真实激励**在 vcan0 上断言。
> 「层」列：`E2E=真端到端(feature)`、`Unit=单元测试`、`SIL/HIL=场景/台架`。

| 模块 | 现有单元测试 | 现有集成/SIL/HIL | 真端到端可观测行为 | 建议层 |
|---|---|---:|---|---|
| `E2E.c`/`E2E_Sm.c` | `test_E2E_*.c`、`test_E2E_SM_*.c` | `test_int_e2e_chain_asild.c`、`sil_009_e2e_corruption.yaml`、`test_hil_e2e.py` | 真实帧 E2E 头（dataId/alive/CRC）有效；损坏帧被拒的行为后果（`sil_009` 已覆盖）；CRC 向量/滑窗细节 | **E2E**（帧级）+ Unit（向量/滑窗） |
| `Com.c` | `test_Com_*.c`（4 个） | `test_int_e2e_chain_asild.c`、SIL | 周期帧 cadence 与 DBC 一致、DLC 正确、E2E 保护生效；`bsw_comcfg_cvc` 已覆盖（bus-probe/cadence） | **E2E**（总线）+ Unit（打包/状态机细节） |
| `WdgM.c` | `test_WdgM_*.c` | `test_int_wdgm_supervision_asild.c`、`sil_005_watchdog_timeout_cvc.yaml`、HIL | 喂狗存活（系统不复位）、超时后果（`sil_005` 已覆盖）；FAILED→EXPIRED 容差细节 | SIL/HIL + Unit |
| `BswM.c` | `test_BswM_asild.c` | `test_int_safe_state_asild.c`、SIL | 状态机后果（安全/降级模式下帧内容变化）；内部迁移表 | SIL + Unit |
| `Dem.c` | `test_Dem_*.c` | `test_int_dem_to_dcm_asilc.c`、SIL | DTC 0x500 广播帧（`sil_009` 等）；去抖计数/持久化细节 | SIL/HIL + Unit |
| `CanSM.c` | `test_CanSM_*.c` | `sil_004_can_busoff_fzc.yaml` | bus-off 后总线行为的可观测后果（`sil_004`）；内部 L1/L2 level | SIL + Unit |
| `CanTp.c` | `test_CanTp_asild.c` | HIL UDS 链 | 分帧/重组在诊断会话中的行为（HIL UDS 已覆盖） | HIL + Unit |
| `FiM.c` | `test_FiM_generated.c` | 间接 | 抑制后果（降级行为） | SIL + Unit |
| `Rte.c` | `test_Rte_*.c` | `test_hil_scheduler.py` | 周期分派的帧后果（cadence）；优先级/边界细节 | **E2E**（cadence）+ Unit |
| `CanIf.c`/`PduR.c` | `test_CanIf_*.c`、`test_PduR_*.c` | `test_int_e2e_chain_asild.c` | 路由/静默丢弃/帧转发的总线后果 | **E2E**（帧级）+ Unit |
| `NvM.c` | — | SIL 持久化 | 持久化生效（重启后恢复） | SIL + Unit |
| `Dcm.c` | `test_Dcm_qm.c` | `test_hil_uds.py` | 诊断会话总线行为（HIL） | HIL |
| `SchM`/`Det` | `test_SchM_*.c`、`test_Det_*.c` | — | 平台/日志面 | 单元已覆盖，**不建议** E2E |
| MCAL 驱动 | 各有 `test_<module>_asil*.c` | HIL | 收发在真实链中参与（进覆盖报告） | 硬件面 HIL |
| OS bootstrap / OSEK 骨架 | `os/bootstrap/test/test_Os_*.c`（约 30）、单元 | SIL | POSIX 不执行 OSEK 任务体骨架 | 单元 + OSEK 目标层 |

---

## 主要覆盖缺口（现状评估）

1. **BSW 运行时的最大头已被真端到端自然覆盖**：CVC SIL ECU 插桩后，`Com.c`、
   `E2E.c`、`E2E_Sm.c`、`PduR`、`CanIf`、`Can_Posix`、`Rte.c`、MCAL、OS 在总线
   测试中真实执行并进入 `e2e-tests/build/coverage/` 报告（实测如 `Com.c` 570 行 →
   374 命中、`E2E.c` 155 → 98、`Can_Posix.c` 159 → 104）。
2. **白盒状态机语义不在 BDD 层固化（设计取舍）**：`WdgM` FAILED→EXPIRED 容差、
   `BswM` 迁移表、`CanSM` L1/L2 level、`E2E_SM` 滑窗——这些**不是总线可观测行为**，
   由 Unity 单元测试固化；总线后果（超时→复位、降级→帧变化、bus-off→静默、
   损坏→拒帧）由 SIL/HIL 场景 + 真端到端覆盖。**不**为"凑 feature/凑覆盖"退化为
   harness 白盒驱动。
3. **跨模块链**：`E2E→Com→PduR→CanIf` 的真实链路后果已由真实帧观测覆盖（例如
   损坏帧被拒后车辆行为不变——`sil_009` 同族，可在 feature 层以 cadence/帧属性断言
   表达）。
4. **非 CVC 的 ECU**：当前只插桩了 CVC。若后续 BSW 测试针对 FZC/RZC/SC 的行为，
   需把对应 ECU 也以 `LLVM_COV=1` 构建（`Dockerfile.vecu` 的构建循环即插桩
   TARGET，逻辑一致）。

---

## 面向 BSW 的真端到端标准（如何新增）

新增一个 BSW 真端到端测试的标准步骤：

### 第 0 步：判定总线可观测性

问：该 BSW 行为能否通过**真实帧 / 真实激励**断言？

- 能（帧属性 / cadence / E2E / 注入后的传播 / 帧存在与缺失）→ 走本标准；
- 不能（白盒状态机内部迁移、内部参数、计数细节）→ 留给单元 / SIL / HIL，
  **不要**为 feature 建白盒 harness。

### 第 1 步：编写 feature（`e2e-tests/src/test/resources/features/bsw_<module>.feature`）

复用三段式 `Background(假如存在)` / `当POST /api/test/bsw/<module>` /
`那么response should be`。场景断言**真实帧**的可观测属性：

```
{
  "phases": [ { "op": "bus-probe",            # 只读观测
                "targets": ["<DBC消息名>"],     # 可空 → 服务器默认
                "windowMs": 2000, "minFrames": 5 } ]
}
{
  "phases": [ { "op": "cadence", "targets": [...] } ]          # 周期
}
{
  "phases": [ { "op": "ftti", "budgetMs": 200, "restartCvc": true } ]  # 注入+计时
}
```

断言字段（全部来自真实帧）：`found` / `busUp` / `dlc`+`dlcOk` / `cycleMs`+`cycleOk`
（**有界带宽 + 稳定周期性**：平均周期落在 0.5x–8x DBC 名义周期内，min≥35%avg 且
max−min≤2×avg，兼容 Docker 非实时节拍） / `e2e` / `dataId`+`dataIdOk` /
`aliveMonotonic` / `crcValid` / `decoded`；ftti 另含 `estopFrameSeen` /
`withinBudget` / `frame{...}`。

### 第 2 步：网关实现

- `gateway/fault_inject/bsw_bus_probe.py`：新增/复用 `probe_live_messages`、
  `observe_message`、`ftti_estop`（可注入 `inject`/`clear` 回调以支持单元测试）。
  **fail-closed**：总线不可用时 per-target 返回 `found=false busUp=false`，未知消息
  返回 `found=false busUp=true`。
- `gateway/fault_inject/app.py`：`Bsw<Module>Phase` 模型扩展 op 与字段；端点按
  `_run_bsw_comcfg_bus_probe` 模式路由（`bus-probe`/`cadence`/`ftti`），禁止与
  旧静态 op 混用（400 守卫）。
- 单元测试：`gateway/tests/test_bsw_bus_probe.py`（python-can **virtual** bus +
  同一 DBC 编码器模拟真实 ECU，验证观测/注入逻辑；无需 vcan0）。

### 第 3 步：插桩 ECU（覆盖率由测试自然产生）

被测 ECU 二进制须带 LLVM 插桩：

- `firmware/platform/posix/Makefile.posix`：`LLVM_COV=1` 时 `CC=clang` +
  `-fprofile-instr-generate -fcoverage-mapping -DSIL_COVERAGE`；
- `firmware/platform/posix/src/Sil_Coverage.c`（+ `cvc_hw_posix.c` 的
  `Main_Hw_Wfi` 调用）：每 ~5s 虚拟时间 `__llvm_profile_write_file()`，补足
  常驻进程不退出、profile 永不落盘的缺口；
- `docker/Dockerfile.vecu`：builder/runtime **Debian trixie + clang 19**（与
  fault-inject 网关的 llvm 同版本，保证 `.profraw` raw format 版本匹配——
  arm64 Ubuntu jammy 缺 clang profile 运行时包）；`TARGET=cvc LLVM_COV=1` 构建，
  构建于 `/app` 使源码路径与网关的 `/app/firmware` 一致（lcov 合并）；
- `docker/docker-compose.dev.yml`：被测 ECU 与 fault-inject 共享
  `cov:/cov` 卷，`LLVM_PROFILE_FILE=/cov/cvc_%p.profraw`（重启 ∈ 新 PID →
  自然累积多份 profraw，`llvm-profdata` 跨进程合并）。

### 第 4 步：覆盖率回收

`gateway/fault_inject/app.py` 的 `_collect_ecu_coverage_info`：

1. 从 `/cov` 收集 `cvc_*.profraw`；
2. `docker cp`（python-docker SDK）取插桩二进制到 `/app/ecu_bins/`；
3. `llvm-profdata merge` → `llvm-cov export`（**不对 `/app/firmware/bsw`
   ignore**，这正是要覆盖的 BSW 模块；仅忽略 `/app/fault_inject`、`/app/build`、
   `/usr`）→ 并入 lcov 报告。

报告因此**由总线测试自身产生** BSW 模块覆盖（Com/E2E/PduR/CanIf/MCAL/RTE/OS），
无需任何"覆盖 soak / 事后驱动 harness"。

### 第 5 步：运行与文档

`docker compose -f docker/docker-compose.dev.yml up -d`（vECU + fault-inject）→
`./gradlew cucumber` → 检查 `e2e-tests/build/coverage/`。配套设计文档
`e2e-tests/src/test/resources/test-design/bsw-<module>-e2e.md`。

> **基础设施角色说明**：旧的静态读回/骨架驱动 native harness 已随真端到端
> 转换删除；BSW E2E feature 只走总线真端到端（插桩 ECU + vcan0 观测）。

---

## 扩展优先级与建议顺序

> **已完成 / 标准落地（2026-08-24）**：
> - 「BSW 端到端=真端到端」标准确立（插桩 ECU + 总线 feature + 自然覆盖回收）。
> - `bsw_comcfg_cvc.feature`（6 场景）与 `bsw_rtetaskbodies_cvc.feature`（4 场景）
>   已转换为纯总线真端到端，全量 768 场景通过。

### 高优先级（总线后果可直接断言，且有安全价值）

| 优先级 | 行为面 | feature 建议 | 关键总线断言 |
|---|---|---|---|
| 1 | Com 周期调度 + E2E 保护 | `bsw_comcfg_cvc`（已落地） | 各安全 TX 帧 DLC/周期/dataId/alive/CRC；Body_Control_Cmd 无 E2E |
| 2 | 任务驱动 cadence + E-Stop FTTI | `bsw_rtetaskbodies_cvc`（已落地） | VS/Torque 10ms、心跳 50ms；EStop 广播 200ms 预算内 + 锁存流 E2E |
| 3 | E2E 拒帧行为后果 | 扩展 `bsw_comcfg_cvc` 或新增 | 总线损坏帧 → 帧不被采信（`sil_009` 同族，feature 层断言帧属性/行为不变） |
| 4 | RX 心跳超时级联 | 新增 | 停发 peer 心跳 → CVC 行为后果（配合 docker 控制，参照 ftti 模式） |
| 5 | bus-off / CanSM 后果 | 新增（或直接依赖 `sil_004`） | bus-off 后总线静默 → 恢复时限 |

### 中优先级

| 行为面 | 说明 |
|---|---|
| WdgM 喂狗存活 | 总线可观测"无复位/持续运行"，SIL/HIL 已覆盖；超时容差细节留单元 |
| Dem DTC 广播 | `sil_009` 已覆盖 DTC 0xE601 广播；可 feature 化 | 
| 诊断栈（Dcm/CanTp） | HIL UDS 已覆盖，总线行为可在 HIL 断言 |

### 低优先级 / 不建议走 BSW feature

| 模块 | 说明 |
|---|---|
| `NvM` | 逻辑简单，SIL 持久化已覆盖；stub 无逻辑 |
| `Xcp` / `Dcm` | QM 且已有生成测试 / HIL |
| `SchM`/`Det` | 平台临界区 / 日志面，单元已覆盖 |
| MCAL 驱动 | 硬件寄存器面，HIL 是更合适的验证层 |
| OS bootstrap / OSEK 骨架 | 独立 OS 套件 + OSEK 目标层；POSIX SIL 不执行 OSEK 任务体骨架 |
| 白盒状态机（WdgM/BswM/CanSM/E2E_SM 内部迁移） | **在单元层固化**，不要为其建白盒 BSW harness |

### 建议扩展顺序

`bus-probe/cadence/ftti 模板复用 → E2E 拒帧行为 → RX 超时级联 → bus-off 后果`；
每个增量都复用同一套网关 op 与插桩基础设施，成本主要是 feature 编写与窗口/预算调优。

---

## 无法覆盖 / 风险说明

- **节拍与 Docker 调度**：真实 ECU 在 Docker 非实时环境下的总线 cadence 约为 DBC
  名义周期的倍数（实测 ~3x）。`cycleOk` 采用**有界带宽 + 稳定周期性**判定，覆盖
  该差异仍能抓无帧/burst/重复发射；名义周期精确达标由 HIL（RT Linux）负责。
- **E-Stop 永锁**：E-Stop 锁存至断电（设计语义）。`ftti` 默认 `restartCvc=true`
  经 docker 重启 CVC 清锁存并验证恢复；`_restart_cvc_container` 用
  `containers.get()` 精确查找（docker socket 代理下 `list()` 可能截断）。
- **纯数据配置 `Com_Cfg_<Ecu>.c`**：编译期常量、零可执行代码，`llvm-cov export`
  返回空轨迹 → 不进覆盖率报告（`app.py` 中注有原因）；其一致性以"数据表访问
  口径"由 bus-probe 的真实帧（DLC/周期/E2E）间接保证——配置错误必然导致帧属性
  与 DBC 不符。
- **生成代码 `Rte_TaskBodies_<Ecu>.c`**：POSIX SIL 走 legacy 主循环
  （`Rte_MainFunction` 直接分派），OS 任务体骨架在 posix 镜像中不执行，报告只显示
  少量命中。该文件是 OSEK 目标代码，结构覆盖由 OSEK 目标/单元层负责——这是
  **真实执行事实**的如实反映，不为凑数而事后驱动。
- **profraw 版本**：ECU 与网关工具链必须同版本（Debian trixie + clang19），否则
  llvm-profdata 报 raw profile version mismatch；`Dockerfile.vecu` 已固定。
- **多容器法**：插桩 ECU 的 `.profraw` 走共享 `cov` 卷；fault-inject 需挂载同一卷，
  且通过 docker SDK 取插桩二进制（`llvm-cov export` 需要二进制内嵌的映射）。

---

## 结论

BSW 层的单元与集成测试覆盖扎实；**BSW 端到端测试的标准做法已确立为"真端到端"**：
以 LLVM 插桩的真实 SIL ECU 为被测系统，BDD feature 断言 vcan0 总线上的真实帧
（DLC/周期/E2E/信号/FTTI），覆盖率由总线测试自身自然产生，不经任何 harness
白盒驱动。两条 BSW 链（`bsw_comcfg_cvc`、`bsw_rtetaskbodies_cvc`）已按此标准
落地并通过全量 768 场景回归；报告包含真实 BSW 模块（Com/E2E/PduR/CanIf/MCAL/
RTE/OS）的逐行覆盖。

后续新增 BSW 端到端测试遵循：**第 0 步先判定"总线可观测性"**——可观测走
`bus-probe/cadence/ftti` 模板，不可观测（白盒状态机、内部参数）留在单元 / SIL /
HIL 层，确保每条 feature 都是对真实系统行为的断言，覆盖来自真实执行。
