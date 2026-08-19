# ASW 现有测试分析

## 范围

本文档总结了本仓库中围绕 AUTOSAR 风格 ASW 层的当前测试全景，重点关注：

1. 已有哪些测试层级，
2. 各测试层级的工作原理，
3. 各层级验证的输入/输出，
4. 哪些 ASW 函数已有直接测试，
5. 哪些 ASW 函数仅通过集成/SIL/HIL/PIL 测试间接覆盖，
6. 如果要增加 `panda/e2e-tests` 风格的 ASW 端到端测试，主要缺口在哪里。

分析基于以下目录中的当前仓库结构：

- `firmware/ecu/*/src`
- `firmware/ecu/*/test`
- `test/unit/bsw`
- `test/framework`
- `test/integration`
- `test/sil`
- `test/hil`
- `test/pil`
- `test/mil`

---

## 执行摘要

本仓库已有**广泛的测试覆盖**，但覆盖分布在多个层级：

- **ASW/SWC 单元测试**：对大多数 ECU 应用组件有较强覆盖
- **BSW 单元测试**：对 Com/E2E/Det/WdgM/IoHwAb/Rte 及相关服务有大量覆盖
- **BSW 集成测试**：通过 Com/PduR/CanIf/E2E 的真实模块链测试
- **POSIX/vCAN 集成测试**：多 ECU 进程级测试
- **SIL/HIL/PIL 测试**：验证 CAN 可见行为、时序、故障响应和诊断的场景及台架测试

**曾缺失的**不是测试量，而是一个 **`panda/e2e-tests` 风格的 ASW 适配层**。该层已开始落地：

- 已有 `.feature` + RESTful-cucumber 步骤定义风格的 BDD 层（`e2e-tests/src/test/resources/features/`），
- 已有针对 ASW 内部的原生测试 shim（`gateway/fault_inject/native/*_harness.c`），
- 已有以 `Given/When/Then`（或 `When/Then`）表达功能行为的 ASW 领域特定语言。

当前已覆盖二十条 ASW 链：

1. **CVC 踏板 → Torque_Request**（`cvc_pedal_torque_request.feature`，17 场景）
2. **CVC 车辆状态机**（`cvc_vehicle_state.feature`，42 场景）
3. **CVC 紧急停止**（`cvc_estop.feature`，6 场景）
4. **CVC CAN 通信**（`cvc_cvccom.feature`，18 场景）
5. **CVC 心跳**（`cvc_heartbeat.feature`，12 场景）
6. **CVC CAN 丢失监控**（`cvc_canmonitor.feature`，14 场景）
7. **CVC 看门狗**（`cvc_watchdog.feature`，10 场景）
8. **CVC 启动自检**（`cvc_selftest.feature`，10 场景）
9. **CVC 调度器**（`cvc_scheduler.feature`，12 场景）
10. **CVC NVM 持久化**（`cvc_nvm.feature`，21 场景）
11. **FZC CAN 通信**（`fzc_fzccom.feature`，21 场景）
12. **FZC 心跳**（`fzc_heartbeat.feature`，13 场景）
13. **FZC CAN 丢失监控**（`fzc_canmonitor.feature`，14 场景）
14. **FZC 转向伺服控制**（`fzc_steering.feature`，26 场景）
15. **FZC 刹车伺服控制**（`fzc_brake.feature`，22 场景）
16. **FZC 激光雷达障碍物检测**（`fzc_lidar.feature`，29 场景）
17. **RZC 电机控制**（`rzc_motor.feature`，35 场景）
18. **RZC 电池监控**（`rzc_battery.feature`，28 场景）
19. **RZC 温度监控**（`rzc_temponitor.feature`，31 场景）
20. **RZC CAN 通信**（`rzc_rzccom.feature`，32 场景）

换句话说，仓库在验证 ASW 行为**效果**的同时，已开始提供与 `panda/e2e-tests` 可比的统一**可读 ASW E2E 测试框架**，已覆盖 CVC 的十个 SWC、FZC 的六个 SWC 与 RZC 的全部四个 SWC。

---

## 当前测试清单

| 层级 | 位置 | 数量 | 主要目的 |
|---|---:|---|
| ECU ASW/SWC 单元测试 | `firmware/ecu/*/test/` | 69 个文件 | 使用 Unity + mock 直接验证应用组件 |
| ASW E2E（BDD） | `e2e-tests/src/test/resources/features/` | 20 个 feature / 413 场景 | 通过测试专用 API + 原生 harness 执行真实 SWC 生产代码，可读断言 |
| BSW 单元测试 | `test/unit/bsw/` | 40 个文件 | 验证单个 BSW 模块及生成的负向/全路径用例 |
| BSW 集成测试 | `test/framework/test_int_*.c` | 11 个文件 | 验证真实模块链，如 E2E -> Com -> PduR -> CanIf |
| POSIX 多 ECU 集成 | `test/integration/` | 8 个文件 | 在 `vcan0` 上运行 POSIX ECU 二进制，验证总线可见的集成行为 |
| SIL 场景测试 | `test/sil/scenarios/` | 16 个 YAML 场景 | 多 ECU Docker/软件在环系统场景 |
| SIL 逐跳测试 | `test/sil/*.py` | 4 个 Python 测试 | 逐步跟踪特定信号/故障链 |
| HIL 场景/测试 | `test/hil/scenarios/`, `test/hil/test_*.py` | 37 个 YAML + 11 个 Python | 在真实 CAN 和混合台架上验证物理 ECU 行为 |
| PIL 场景 | `test/pil/scenarios/` | 5 个 YAML 场景 | 验证一个真实 DUT，注入模拟的对等心跳/环境 |
| MIL | `test/mil/` | 仅有骨架 | 目录存在，但未找到可比较的可执行 MIL 套件 |

---

## 与 `panda/e2e-tests` 的对比

参考项目 `panda/e2e-tests` 在结构上有显著不同：

| 方面 | `panda/e2e-tests` | 当前仓库 |
|---|---|---|
| 测试表达 | Java + Cucumber `.feature` + 步骤定义 | C/Unity、Python 脚本、YAML 场景 + Java Cucumber `.feature`（ASW E2E） |
| ASW/内部访问 | JNA/原生库适配器（`BodyPandaClient`、`PandaClient`） | 原生 C harness（`cvc_pedal_harness.c`、`cvc_vehiclestate_harness.c`、`cvc_estop_harness.c`、`cvc_cvccom_harness.c`、`cvc_heartbeat_harness.c`、`cvc_canmonitor_harness.c`、`cvc_watchdog_harness.c`、`cvc_selftest_harness.c`、`cvc_scheduler_harness.c`、`cvc_nvm_harness.c`、`fzc_fzccom_harness.c`、`fzc_heartbeat_harness.c`、`fzc_canmonitor_harness.c`、`fzc_steering_harness.c`、`fzc_brake_harness.c`、`fzc_lidar_harness.c`、`rzc_motor_harness.c`、`rzc_battery_harness.c`、`rzc_temponitor_harness.c`、`rzc_rzccom_harness.c`）链接真实 SWC 生产代码 |
| 场景风格 | BDD 业务可读步骤 | 面向 CAN/系统/故障的测试脚本和 YAML + 业务可读的 ASW BDD 场景 |
| 内部断言 | 通过原生 shim 轻松断言内部状态 | 通过原生 harness 直接断言 RTE/Com 输出 + mock、CAN 流量、DTC、状态推断 |
| 当前规模 | 47 个 feature 文件 / 392 个场景 | 20 个 feature 文件 / 413 个 ASW E2E 场景（其余层级覆盖广泛） |

**含义：** 本仓库已有强大的验证基础设施，且已开始补齐 `panda/e2e-tests` 风格的**可读 ASW E2E 测试框架**（原生 harness + BDD feature）。当前规模已扩展至 20 条链，覆盖 CVC 十个 SWC、FZC 六个 SWC 与 RZC 四个 SWC。

---

## 各测试层级的详细说明

### 1. ECU ASW/SWC 单元测试

这是最接近 AUTOSAR ASW 层的现有测试。它们通常：

- 编译单个 SWC 或应用模块，
- 用 mock 替换 RTE/IoHwAb/Com/Dem/BswM/Dio/Pwm 等，
- 向 SWC 输入受控的输入，
- 验证输出写入、状态转换、故障或执行器命令。

| 项目 | 详情 |
|---|---|
| **位置** | `firmware/ecu/*/test/test_*.c` |
| **示例** | `firmware/ecu/cvc/test/test_Swc_VehicleState_asild.c`、`firmware/ecu/fzc/test/test_Swc_Steering_asild.c`、`firmware/ecu/rzc/test/test_Swc_Motor_asild.c` |
| **典型输入** | 模拟的 `Rte_Read()` 值、虚假传感器值、故障位、心跳状态、命令超时、CAN 相关影子数据、模拟的硬件回读 |
| **典型输出** | `Rte_Write()` 值、`Com_SendSignal()` 载荷、`Dem_ReportErrorStatus()` 调用、`BswM_RequestMode()` 调用、`Pwm_SetDutyCycle()`/`Dio_WriteChannel()` 执行器输出 |
| **验证点** | 状态机转换、合理性检查、输出钳位、超时处理、降额、看门狗门控、心跳处理、DTC 升级、调度器表 |

**具体示例**

1. `test_Swc_VehicleState_asild.c`
   - **输入**：踏板故障、CAN 超时、紧急停止、SC 切断、电机/制动/转向故障、电池状态
   - **输出**：车辆状态、心跳模式镜像、BswM 模式请求、DEM 报告
   - **检查**：INIT/RUN/DEGRADED/LIMP/SAFE_STOP/SHUTDOWN 转换及锁存行为

2. `test_Swc_Steering_asild.c`
   - **输入**：转向命令、实测转向角度、超时、SPI 读取失败、车辆模式
   - **输出**：PWM 占空比、禁用引脚、转向故障信号、DEM 事件
   - **检查**：角度到 PWM 映射、范围检查、速率限制、回中、故障锁存清除

3. `test_Swc_Motor_asild.c`
   - **输入**：扭矩命令、紧急停止、车辆状态、过流/温度标志、超时
   - **输出**：H 桥 PWM 占空比、电机方向、使能引脚、扭矩回显、电机故障码
   - **检查**：扭矩限制、直通防护、死区时间、命令超时恢复、安全状态行为

### 2. BSW 单元测试

这些测试针对独立的 BSW 模块和生成的负向/全路径测试。

| 项目 | 详情 |
|---|---|
| **位置** | `test/unit/bsw/` |
| **示例** | `test_E2E_asild.c`、`test_Com_asild.c`、`test_CanIf_asild.c`、`test_WdgM_asild.c`、`test_XCP_security_generated.c` |
| **典型输入** | API 调用、PDU、信号 ID、定时器/计数器、无效参数、生成的边界用例向量 |
| **典型输出** | 返回码、更新的内部状态、保护/检查结果、路由后的 PDU 内容、DET/DEM 通知 |
| **验证点** | AUTOSAR 服务行为、错误处理、负向用例、超时处理、E2E CRC/状态机行为、生成的边界用例 |

**具体示例**

1. `test_E2E_asild.c`
   - **输入**：载荷字节、DataId、存活计数器、故意损坏的帧
   - **输出**：E2E 保护/检查状态
   - **检查**：CRC 有效性、存活计数器增量规则、错误检测行为

2. `test_Com_asild.c`
   - **输入**：信号写/读请求和 PDU 定时行为
   - **输出**：信号影子值、TX/RX 处理结果
   - **检查**：打包/解包、周期性行为、超时/质量处理

3. `test_WdgM_asild.c`
   - **输入**：检查点进度 / 超时条件
   - **输出**：监控状态和反应
   - **检查**：存活/截止时间监控和故障升级

### 3. BSW 集成测试

这些测试将多个真实的 BSW 模块链接在一起，只模拟最小的硬件边界。

| 项目 | 详情 |
|---|---|
| **位置** | `test/framework/test_int_*.c` |
| **示例** | `test_int_e2e_chain_asild.c`、`test_int_dem_to_dcm_asilc.c`、`test_int_wdgm_supervision_asild.c`、`test_int_safe_state_asild.c` |
| **典型输入** | 受保护的载荷、模拟的 CAN 环回、故障注入、模式请求、看门狗未命中 |
| **典型输出** | 路由后的 RX 信号、安全状态模式变更、DCM 可见的 DTC 行为、总线关闭处理 |
| **验证点** | 模块间接口、跨 BSW 层的真实数据流、Dem/DCM 联动、WdgM/BswM 反应链 |

**具体示例：`test_int_e2e_chain_asild.c`**

- **输入**：经 E2E 保护的载荷，通过 Com -> CanIf 发送，由模拟的 `Can_Write()` 捕获，然后通过 `CanIf_RxIndication()` 环回
- **输出**：RX 信号在 Com 接收侧可用
- **检查**：完整的 E2E -> Com -> PduR -> CanIf 往返及接收侧验证

### 4. POSIX/vCAN 集成测试

这些测试将 ECU 二进制作为 POSIX 进程运行，观察在 `vcan0` 上的集成行为。

| 项目 | 详情 |
|---|---|
| **位置** | `test/integration/` |
| **示例** | `layer4/test_cvc_full.py`、`layer5/test_cvc_fzc_dual.py`、`layer5/test_cvc_fzc_full.py`、`layer6/test_sc_integration.py` |
| **典型输入** | ECU 进程启动/停止、`vcan0` 上的原始 CAN 流量、进程 kill/重启、时序采集 |
| **典型输出** | 总线上的 CAN ID、E2E 头部、存活计数器、周期性消息速率、对等节点丢失后进程存活状态 |
| **验证点** | TX 存在性、DLC 正确性、E2E/DataId 存在性、总线时序、对等节点故障后的 ECU 间行为 |

**具体示例**

1. `test_cvc_full.py`
   - **输入**：仅运行 `cvc_posix`
   - **输出**：CVC 心跳、车辆状态、扭矩、转向、制动、车身命令、虚拟传感器帧
   - **检查**：TX 存在性、E2E DataId、存活计数器递增、消息速率、独立降级行为

2. `test_cvc_fzc_dual.py`
   - **输入**：运行 `cvc_posix` + `fzc_posix`，然后终止 CVC
   - **输出**：共享心跳、转向命令/状态流量、FZC 持续存活
   - **检查**：双向通信、CVC 终止后 FZC 心跳持续性

3. `test_sc_integration.py`
   - **输入**：与其它 ECU 进程一起运行 SC，然后终止一个对等节点
   - **输出**：`SC_Status` 0x013、心跳监控反应
   - **检查**：SC E2E、对等心跳可见性、SC 故障观察

### 5. SIL 场景测试

这是当前最高价值的全软件系统测试。

| 项目 | 详情 |
|---|---|
| **位置** | `test/sil/scenarios/*.yaml`、`test/sil/run_sil.sh`、`test/sil/verdict_checker.py` |
| **示例** | `sil_003_emergency_stop.yaml`、`sil_009_e2e_corruption.yaml`、`sil_006_battery_undervoltage.yaml` |
| **典型输入** | YAML `setup`/`steps`：状态等待、场景注入、原始 CAN 注入、Docker 停止/启动、故障 API / MQTT 操作 |
| **典型输出** | CAN 消息、车辆状态转换、电机 RPM、DTC 广播、MQTT 可见效果、结果日志 |
| **验证点** | 端到端安全链、故障响应延迟、安全状态转换、ECU 恢复、E2E 拒绝、DTC 确认 |

**具体示例**

1. `sil_003_emergency_stop.yaml`
   - **输入**：正常驾驶设置 + 紧急停止注入
   - **输出**：SAFE_STOP 状态、紧急停止广播 0x001、零扭矩、转向居中、电机关闭、持续心跳
   - **检查**：从 CVC 检测到多 ECU 反应的完整 ASIL-D 安全链

2. `sil_009_e2e_corruption.yaml`
   - **输入**：停止 CVC 并注入损坏的 0x100 帧
   - **输出**：RZC 拒绝帧、电机不移动、DTC 0xE601 广播、重启后车辆返回 RUN
   - **检查**：Com 层 E2E 拒绝和 Dem 升级

3. `sil_006_battery_undervoltage.yaml`
   - **输入**：持续的低压模拟
   - **输出**：电池状态变化、CVC 模式反应、安全处理
   - **检查**：端到端低压处理

### 6. SIL 逐跳测试

这些 Python 测试比 YAML 场景更窄，每次关注一个信号链。

| 项目 | 详情 |
|---|---|
| **位置** | `test/sil/test_battery_chain.py`、`test/sil/test_overtemp_hops.py`、`test/sil/test_vsm_fault_transitions.py` |
| **示例** | 电池、过温、车辆状态机故障转换 |
| **典型输入** | MQTT 注入、总线轮询、状态重置/恢复 |
| **典型输出** | 解码后的 CAN 信号值、DTC、状态转换 |
| **验证点** | 信号路径中的每一跳、中间可观察性、故障注入前的负向测试 |

### 7. HIL 测试

验证物理 ECU 或混合物理/vECU 台架上的行为。

| 项目 | 详情 |
|---|---|
| **位置** | `test/hil/test_*.py`、`test/hil/scenarios/*.yaml`、`test/hil/hil_runner.py` |
| **示例** | `test_hil_e2e.py`、`test_hil_uds.py`、`test_hil_scheduler.py`、`test_hil_body.py` |
| **典型输入** | `can0` 上的真实 CAN 总线流量、UDS 请求、MQTT 或测试台架注入、物理启动行为 |
| **典型输出** | 实时 CAN 帧、UDS 响应、时序统计、DTC 广播、ECU 模式/状态变更 |
| **验证点** | 真实总线时序、实际帧上的 CRC 正确性、硬件诊断栈、混合台架交互 |

**具体示例**

1. `test_hil_e2e.py`
   - **输入**：观察物理 `Vehicle_State` 和心跳帧
   - **输出**：实时帧字节和存活计数器
   - **检查**：真实硬件上的 CRC-8 和存活计数器递增

2. `test_hil_uds.py`
   - **输入**：向物理 CVC/FZC/RZC 发送 ISO-TP/UDS 请求
   - **输出**：0x7E8/0x7E9/0x7EA 上的 ECU 响应
   - **检查**：测试仪存在、会话控制、DID 读取、ECU 复位、诊断互操作性

3. `test_hil_scheduler.py`
   - **输入**：采集真实帧时间戳
   - **输出**：时序统计
   - **检查**：平均周期、抖动、丢帧式间隙、跨 ECU 相位多样性

### 8. PIL 测试

PIL 验证一个真实 ECU 作为 DUT，测试框架模拟其对等节点。

| 项目 | 详情 |
|---|---|
| **位置** | `test/pil/scenarios/*.yaml`、`test/pil/pil_runner.py`、`test/pil/heartbeat_injector.py` |
| **示例** | `pil_005_cvc_e2e_integrity.yaml` |
| **典型输入** | 注入的对等心跳、DUT 选择、场景步骤、CAN 观察 |
| **典型输出** | DUT 心跳/状态/命令帧、E2E 正确性、状态转换 |
| **验证点** | 受控网络模拟下的单 ECU 行为、心跳超时处理、E2E 完整性 |

**具体示例：`pil_005_cvc_e2e_integrity.yaml`**

- **输入**：等待 CVC RUN 状态并观察 0x010/0x100/0x101/0x102/0x103
- **输出**：物理 DUT 的多个连续 TX 帧
- **检查**：关键 CVC 消息上的 E2E CRC 有效性和存活计数器递增

### 9. MIL

`test/mil/` 目前包含占位概述和文件夹，但仓库当前未提供针对 ASW 行为的可比较可执行 MIL 测试套件。

**实际意义：** MIL 目前不是 ASW E2E 扩展的可用起点。

---

## 按 ECU 的 ASW 覆盖摘要

| ECU | `src/` 中的源文件 | 直接 ASW 测试文件 | 覆盖说明 |
|---:|---:|---:|---|
| BCM | 6 | 5 | 直接 SWC 覆盖强；`bcm_main.c` 仅间接覆盖 |
| CVC | 14 | 13 | 直接 SWC 覆盖强；`main.c` 主要由集成/SIL/HIL 覆盖 |
| FZC | 13 | 11 | 直接覆盖强；`Swc_FzcSensorFeeder.c` 和 `main.c` 为间接覆盖 |
| ICU | 3 | 4 | 实际覆盖良好；部分测试针对 CAN/main 辅助函数，未拆分为独立源文件 |
| RZC | 14 | 13 | 直接覆盖强；`Swc_RzcSensorFeeder.c` 和 `main.c` 为间接覆盖 |
| SC | 19 | 17 | 直接覆盖良好，但多个运行时胶水文件仅间接覆盖 |
| TCU | 5 | 6 | 直接覆盖良好；CAN/main 辅助函数即使未拆分为独立文件也有测试 |

---

## ASW 函数到测试的映射

以下表格关注**应用层函数/组件**，而非生成的 cfg 文件。

### BCM

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Swc_BcmCan.c` | BCM CAN 初始化、状态 RX、命令 RX、状态 TX | `test_Swc_BcmCan_qm.c` | `test_hil_body.py` | 车辆/车身 CAN 帧、初始化参数 | RX 解析、TX 心跳/车身状态、初始化行为 |
| `Swc_BcmMain.c` | BCM 10ms 主循环 | `test_Swc_BcmMain_qm.c` | `test_hil_body.py` | 周期性循环调用、待处理 CAN 数据 | 循环调度、处理/发送顺序 |
| `Swc_DoorLock.c` | 手动/自动门锁 | `test_Swc_DoorLock_qm.c` | 通过 BCM 主/车身测试间接覆盖 | 锁定/解锁请求、车辆状态 | 锁状态变化和自动锁逻辑 |
| `Swc_Indicators.c` | 转向/危险灯逻辑 | `test_Swc_Indicators_qm.c` | 通过 BCM 主/车身测试间接覆盖 | 转向/危险灯请求、时序 | 闪烁模式和危险灯优先级 |
| `Swc_Lights.c` | 前照灯/尾灯控制 | `test_Swc_Lights_qm.c` | 通过 BCM 主/车身测试间接覆盖 | 灯光命令和状态 | 灯具输出选择 |
| `bcm_main.c` | BCM 入口点 | 无 | `test_hil_body.py`、SIL 启动场景 | 进程启动和主循环生命周期 | 仅启动和台架可见交互 |

### CVC

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Ssd1306.c` | OLED 驱动 | `test_Ssd1306_qm.c` | 通过 `test_Swc_Dashboard_qm.c` 间接覆盖 | 初始化/渲染/清除调用 | 显示缓冲区/I2C 面向行为 |
| `Swc_CanMonitor.c` | CAN 丢失检测和恢复 | `test_Swc_CanMonitor_asilc.c` | `sil_004_can_busoff_fzc.yaml`、`cvc_canmonitor.feature`（ASW E2E：bus-off/200ms 静默/500ms 错误警告/10s 窗口恢复/SHUTDOWN 终态） | 超时/总线丢失条件 | 故障检测和恢复路径 |
| `Swc_CvcCom.c` | CVC RX/TX + E2E 桥接 | `test_Swc_CvcCom_asild.c` | `test_cvc_full.py`、`test_cvc_fzc_full.py`、SIL 启动/E2E 场景、`cvc_cvccom.feature`（ASW E2E：TX 心跳/0x100 faultMask/制动覆盖/E-Stop 广播 + RX 桥接） | RX 帧、调度节拍、RTE 值 | 信号路由、E2E 保护 TX、周期性发送 |
| `Swc_CvcDcm.c` | UDS/DID/DTC 路由 | `test_Swc_CvcDcm_qm.c` | `test_hil_uds.py` | UDS 服务请求 | DID 响应、DTC 暴露、服务分发 |
| `Swc_Dashboard.c` | OLED 仪表盘渲染 | `test_Swc_Dashboard_qm.c` | 通过启动/显示路径间接覆盖 | 车辆状态、速度、故障 | 渲染后的状态/故障呈现 |
| `Swc_EStop.c` | 紧急停止消抖、锁存、广播 | `test_Swc_EStop_asilb.c` | `sil_003_emergency_stop.yaml` | GPIO/按钮式紧急停止信号 | 锁存、CAN 0x001、安全状态触发 |
| `Swc_Heartbeat.c` | 心跳 TX/RX 监控 | `test_Swc_Heartbeat_asilc.c` | `test_cvc_full.py`、`test_hil_heartbeat.py`、`pil_005_cvc_e2e_integrity.yaml`、`cvc_heartbeat.feature`（ASW E2E：TX 50ms 边界/alive 15 回绕/WdgM SE3/RX 指示/post-INIT 通信状态复位） | 对等心跳状态、周期性节拍 | 心跳载荷、超时检测、存活计数器 |
| `Swc_Nvm.c` | DTC 持久化/校准 NVM | `test_Swc_Nvm_asild.c` | `cvc_nvm.feature`（ASW E2E：20-slot 循环缓冲 DTC 存储/回绕/CRC 损坏检测/冻结帧 + 校准读写/CRC 损坏回退默认值 + 未初始化守卫） | 存储的故障/校准记录 | 持久化/恢复行为 |
| `Swc_Pedal.c` | 双踏板处理和扭矩映射 | `test_Swc_Pedal_asild.c` | `sil_002_pedal_ramp.yaml` | 踏板传感器值、合理性故障、模式限制 | 扭矩请求、故障检测、钳位 |
| `Swc_Scheduler.c` | 可运行实体表和时序配置 | `test_Swc_Scheduler_asild.c` | `test_hil_scheduler.py`、`cvc_scheduler.feature`（ASW E2E：生产 8 项表装载/三种守卫/NULL 清除与恢复/计数边界/优先级与 WCET 数据检查） | 调度器表内容/周期 | 可运行实体配置正确性 |
| `Swc_SelfTest.c` | 启动自检序列 | `test_Swc_SelfTest_asild.c` | `test_hil_selftest.py`、启动 SIL/HIL 流程、`cvc_selftest.feature`（ASW E2E：7 项启动自检/关键失败立即终止/DTC 上报/OLED 非关键） | 自检前提条件和失败 | 通过/失败顺序和门控 |
| `Swc_VehicleState.c` | 权威 CVC VSM | `test_Swc_VehicleState_asild.c` | `test_vsm_fault_transitions.py`、`test_hil_vsm.py`、SIL 电池/过温/紧急停止场景、`cvc_vehicle_state.feature`（ASW E2E） | 故障、通信丢失、紧急停止、电池、对等状态 | 状态转换、锁存、模式输出 |
| `Swc_Watchdog.c` | 外部看门狗喂狗门控 | `test_Swc_Watchdog_asild.c` | `sil_005_watchdog_timeout_cvc.yaml`、`test_hil_wdgm.py`、`cvc_watchdog.feature`（ASW E2E：四条件喂狗门控/NULL 配置/未初始化守卫） | 主循环完成、栈金丝雀、RAM 模式测试、CAN bus-off | WDI 喂狗使能/禁用、翻转计数和故障门控 |
| `main.c` | CVC 入口点和周期性循环 | 无 | `test_cvc_full.py`、SIL 启动/电源循环场景、HIL/PIL 启动路径 | 进程/板卡启动 | 端到端启动和周期性行为 |

### FZC

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Swc_Brake.c` | 刹车伺服控制 | `test_Swc_Brake_asild.c` | `sil_003_emergency_stop.yaml`、`test_cvc_fzc_full.py`、`fzc_brake.feature`（ASW E2E） | 刹车命令、模式、电机切断条件 | PWM/伺服行为、钳位/安全状态处理 |
| `Swc_Buzzer.c` | 警告蜂鸣器模式 | `test_Swc_Buzzer_qm.c` | 通过 FZC main 间接覆盖 | 区域/状态警告条件 | 音调/模式行为 |
| `Swc_FzcCanMonitor.c` | FZC CAN 丢失检测 | `test_Swc_FzcCanMonitor_asilc.c` | `sil_004_can_busoff_fzc.yaml`、集成心跳测试、`fzc_canmonitor.feature`（ASW E2E：500 周期宽限期/bus-off 立即安全状态/20 周期静默/TEC·REC≥96 错误警告/安全状态锁存 NO-recovery/NotifyRx 复位） | 总线关闭/静默/错误条件 | 故障检测/降级路径 |
| `Swc_FzcCom.c` | FZC RX/TX + E2E | `test_Swc_FzcCom_asild.c` | `test_cvc_fzc_dual.py`、`test_cvc_fzc_full.py`、HIL 车身/心跳、`fzc_fzccom.feature`（ASW E2E：E2E 发送保护/alive 回绕 15→0/RX 周期/CRC·Data ID 拒绝/TX 周期调度 0x011·0x200·0x201·0x210·0x211·0x220） | 对等命令帧、本地状态信号 | 路由、E2E DataId、TX 状态帧 |
| `Swc_FzcDcm.c` | FZC 诊断 | `test_Swc_FzcDcm_qm.c` | `test_hil_uds.py` | UDS 请求 | 服务处理和 DID 行为 |
| `Swc_FzcNvm.c` | FZC DTC/校准持久化 | `test_Swc_FzcNvm_asild.c`、`fzc_nvm.feature`（ASW E2E） | 诊断/安全流程 | 存储的校准和 DTC 记录 | 持久化行为、CRC 失效回退 |
| `Swc_FzcSafety.c` | 本地安全聚合/看门狗 | `test_Swc_FzcSafety_asild.c` | HIL 看门狗/自检流程 | 本地故障、看门狗、自检状态 | 聚合故障行为和安全反应 |
| `Swc_FzcScheduler.c` | 可运行实体时序配置 | `test_Swc_FzcScheduler_asild.c` | `test_hil_scheduler.py`、`hil_061_scheduler_cross_ecu.yaml`、`fzc_scheduler.feature`（ASW E2E：SWR-FZC-029 静态表读回/未初始化守卫/重复 Init 幂等/安全优先级/WCET 上限） | 调度器表定义 | 周期/优先级/WCET 正确性 |
| `Swc_FzcSensorFeeder.c` | plant-sim 虚拟传感器 -> IoHwAb | 无 | `sil_008_sensor_disagreement.yaml`、`sil_011_steering_sensor_failure.yaml` | 注入的虚拟传感器数据 | 仅间接转向/激光雷达行为 |
| `Swc_Heartbeat.c` | FZC 心跳 | `test_Swc_Heartbeat_asilc.c` | `test_cvc_fzc_dual.py`、`test_hil_heartbeat.py`、调度器 HIL 测试、`fzc_heartbeat.feature`（ASW E2E：TX 50ms 边界/alive 15 回绕/ECU ID/车辆状态与故障位掩码/OperatingMode·FaultStatus 低 4 位掩码/bus-off 抑制 TX 与恢复） | 周期性节拍、故障位掩码 | 50ms 心跳载荷和节奏 |
| `Swc_Lidar.c` | TFMini 障碍物检测 | `test_Swc_Lidar_asilc.c` | `test_cvc_fzc_full.py`、SIL 传感器场景、`fzc_lidar.feature`（ASW E2E） | 激光雷达帧/障碍物距离 | 帧解析、区域、故障处理、CAN 状态 |
| `Swc_Steering.c` | 转向伺服控制 | `test_Swc_Steering_asild.c` | `sil_008_sensor_disagreement.yaml`、`sil_011_steering_sensor_failure.yaml`、`test_cvc_fzc_dual.py`、`fzc_steering.feature`（ASW E2E） | 转向命令、实测角度、超时、SPI 故障 | PWM 映射、速率限制、RTC、故障锁存 |
| `main.c` | FZC 入口点 | 无 | `test_cvc_fzc_dual.py`、`test_cvc_fzc_full.py`、SIL/HIL 启动流程 | 进程启动 | 启动和集成操作 |

### ICU

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Swc_Dashboard.c` | 仪表盘显示/仪表 | `test_Swc_Dashboard_qm.c` | `test_hil_body.py` | 车辆状态、电池/电流、故障摘要 | 仪表盘文本/仪表呈现 |
| `Swc_DtcDisplay.c` | DTC 循环缓冲区显示逻辑 | `test_Swc_DtcDisplay_qm.c` | 通过 ICU main/车身流程间接覆盖 | 传入的 DTC 数据 | 缓冲和显示列表行为 |
| `icu_main.c` | ICU 主入口点、CAN、50ms 循环 | `test_Swc_IcuMain_qm.c`、`test_Swc_IcuCan_qm.c` | `test_hil_body.py` | 启动、CAN 初始化、循环节拍 | 台架可见的启动和仪表更新循环 |

### RZC

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Swc_Battery.c` | 电池电压监控 | `test_Swc_Battery_qm.c` | `test_battery_chain.py`、`test_hil_battery.py`、`sil_006_battery_undervoltage.yaml`、`rzc_battery.feature`（ASW E2E，行/分支/函数覆盖 100%） | 电池电压采样/注入的低压条件 | 平均电压、CAN 0x303、状态反应 |
| `Swc_CurrentMonitor.c` | 电机电流采样/滤波 | `test_Swc_CurrentMonitor_asila.c` | `sil_007_overcurrent_motor.yaml`、`test_hil_overtemp.py` | 电流传感器采样/故障阈值 | 平均电流、过流指示 |
| `Swc_Encoder.c` | 速度/RPM 和堵转逻辑 | `test_Swc_Encoder_asilc.c` | `rzc_encoder.feature`（ASW E2E，行/分支/函数覆盖 100%）、电机链测试 | 编码器脉冲/方向 | RPM、方向、堵转检测 |
| `Swc_Heartbeat.c` | RZC 心跳 | `test_Swc_Heartbeat_asilc.c` | `test_hil_heartbeat.py`、集成/调度器测试、`rzc_heartbeat.feature`（ASW E2E，行/分支/函数覆盖 100%） | 周期性节拍和故障掩码 | 50ms 心跳载荷和时序 |
| `Swc_Motor.c` | H 桥电机控制 | `test_Swc_Motor_asild.c` | `sil_007_overcurrent_motor.yaml`、`sil_003_emergency_stop.yaml`、`test_hil_overtemp.py` | 扭矩命令、紧急停止、过流/温度、车辆状态 | PWM 占空比、方向、使能、安全状态关断 |
| `Swc_RzcCom.c` | RZC RX/TX + E2E | `test_Swc_RzcCom_asild.c` | `sil_009_e2e_corruption.yaml`、启动/系统总线测试 | 对等命令帧、本地状态值 | 路由、E2E 检查/保护、超时处理 |
| `Swc_RzcDcm.c` | RZC 诊断 | `test_Swc_RzcDcm_qm.c` | `test_hil_uds.py` | UDS 请求 | DID 和诊断响应 |
| `Swc_RzcNvm.c` | DTC 持久化/冻结帧 | `test_Swc_RzcNvm_asild.c` | `rzc_nvm.feature`（ASW E2E，行/分支/函数覆盖 100%） | 存储的 DTC 和冻结帧记录 | CRC/持久化行为 |
| `Swc_RzcSafety.c` | 本地安全/看门狗/CAN 丢失监控 | `test_Swc_RzcSafety_asild.c` | `test_hil_wdgm.py`、心跳/丢失场景、`rzc_safety.feature`（ASW E2E，行/分支/函数覆盖 100%） | 看门狗和本地故障 | 安全反应和故障聚合 |
| `Swc_RzcScheduler.c` | 调度器表 | `test_Swc_RzcScheduler_asild.c` | `test_hil_scheduler.py`、`rzc_scheduler.feature`（ASW E2E，行/函数覆盖 100%，分支 85.7%） | 可运行实体定义 + Tick 分派 | 时序/优先级正确性、周期分派计数 |
| `Swc_RzcSelfTest.c` | 启动自检 | `test_Swc_RzcSelfTest_asild.c` | `test_hil_selftest.py`、`rzc_selftest.feature`（ASW E2E，行/分支/函数覆盖 100%） | 启动检查条件 | 自检门控和失败处理 |
| `Swc_RzcSensorFeeder.c` | SIL 虚拟传感器馈线 | 无 | `test_battery_chain.py`、`test_overtemp_hops.py`、`sil_006_battery_undervoltage.yaml`、`sil_010_overtemp_motor.yaml` | 注入的虚拟电池/温度/电流值 | 仅间接下游 SWC 行为 |
| `Swc_TempMonitor.c` | NTC 温度监控/降额 | `test_Swc_TempMonitor_asila.c` | `test_overtemp_hops.py`、`test_hil_overtemp.py`、`sil_010_overtemp_motor.yaml` | 温度采样和阈值 | 阶梯降额、过温故障/输出 |
| `main.c` | RZC 入口点 | 无 | SIL/HIL 启动流程和集成测试 | 启动/周期性循环 | 启动和集成操作 |

### SC

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `sc_can.c` | 仅监听 CAN 驱动 | `test_sc_can_asild.c` | `test_sc_integration.py`、HIL 心跳/E2E | CAN 帧和驱动状态 | RX 路径、仅监听行为 |
| `sc_e2e.c` | SC 侧 E2E CRC 验证 | `test_sc_e2e_asild.c` | `sc_e2e.feature`（ASW E2E，行/分支/函数 100%）、`test_hil_e2e.py`、`sil_009_e2e_corruption.yaml` | 帧字节/损坏的 E2E 数据 | CRC 检查有效性、alive 单调性、连续失败锁存、relay-kill 门控 |
| `sc_esm.c` | ESM 锁步错误处理器 | `test_sc_esm_asilc.c` | 通过 SC 运行时间接覆盖 | ESM 故障条件 | 锁步错误响应 |
| `sc_eth.c` | 台架以太网驱动 | `test_sc_eth.c` | 台架遥测流程 | 描述符/帧输入 | 描述符解析和边界 |
| `sc_eth_rx_dispatch.c` | UDP RX 分发 | 无 | `test_sc_xcp_eth.c` | UDP 数据包分类 | 间接分发/XCP 路径 |
| `sc_eth_telemetry.c` | UDP 遥测生产者 | `test_sc_eth_telemetry.c` | 台架遥测流程 | 运行时遥测状态 | 遥测帧内容 |
| `sc_eth_udp.c` | IPv4/UDP 编码器 | `test_sc_eth_udp.c` | 台架遥测/XCP 流程 | 载荷和端点数据 | 以太网/IPv4/UDP 编码 |
| `sc_heartbeat.c` | 对等心跳监控 | `test_sc_heartbeat_asilc.c` | `sc_heartbeat.feature`（ASW E2E，行/函数 100%、分支 97.6%） | 对等心跳存在/丢失 | 超时检测和监控状态 |
| `sc_led.c` | 故障 LED 面板 | `test_sc_led_qm.c` | 通过 SC 故障流程间接覆盖 | SC 故障状态 | LED 输出模式 |
| `sc_main.c` | SC 协作式主循环 | `test_sc_main_asild.c` | `test_sc_integration.py`、`sil_005_watchdog_timeout_cvc.yaml` | 启动序列和主循环钩子 | 初始化顺序和循环行为 |
| `sc_monitoring.c` | SC_Status 广播 | 无 | `test_sc_integration.py` | 内部 SC 监控状态 | 系统总线上的 0x013 载荷可见性和 E2E |
| `sc_os_cfg.c` | OSEK 任务/告警配置 | 无 | 通过 SC 启动/时序间接覆盖 | 周期性任务/告警配置 | 仅间接执行 |
| `sc_plausibility.c` | 扭矩-电流交叉检查 | `test_sc_plausibility_asilc.c` | `sc_plausibility.feature`（ASW E2E，行 98.1%/分支 96.3%/函数 100%） | 扭矩/电流组合 | 合理性失败处理 |
| `sc_relay.c` | 切断继电器控制 | `test_sc_relay_asild.c` | `sc_relay.feature`（ASW E2E，行/分支/函数 100%）、`sil_005_watchdog_timeout_cvc.yaml`、`test_sc_integration.py` | 继电器请求/故障状态 | 继电器吸合/断开逻辑、全部触发条件与锁存 |
| `sc_selftest.c` | 启动/运行时自检 | `test_sc_selftest_asild.c` | `sc_selftest.feature`（ASW E2E，行/分支/函数 100%） | 启动/运行时自检条件 | 启动/运行时自检处理 |
| `sc_startup.S` | TMS570 启动汇编 | 无 | 未找到明确的专用测试 | 引导/启动上下文 | 当前未直接单元测试 |
| `sc_state.c` | SC 运行时状态机 | `test_sc_state_asild.c` | `sc_state.feature`（ASW E2E，行/分支/函数 100%） | 对等故障/故障组合 | SC 模式转换 |
| `sc_uds_shim.c` | 仅 HIL UDS shim | 无 | 未找到明确的专用 HIL 测试 | 诊断别名流量 | 当前未直接覆盖 |
| `sc_watchdog.c` | 外部看门狗喂狗控制 | `test_sc_watchdog_asild.c` | `sc_watchdog.feature`（ASW E2E，行/分支/函数 100%）、`test_hil_wdgm.py` | 看门狗条件 | 喂狗使能/禁用行为 |
| `sc_xcp_eth.c` | XCP-over-Ethernet 从站 | `test_sc_xcp_eth.c` | 台架遥测/XCP 流程 | UDP/XCP 命令 | 最小 XCP 服务路径 |

### TCU

| 组件 | 功能 | 直接测试 | 间接/系统测试 | 测试的输入 | 输出/验证点 |
|---|---|---|---|---|---|
| `Swc_DataAggregator.c` | 缓存最新 CAN 值并带超时 | `test_Swc_DataAggregator_qm.c` | 通过 TCU main/车身流程间接覆盖 | 传入的 CAN 采样和超时间隔 | 缓存新鲜度和超时行为 |
| `Swc_DtcStore.c` | 内存 DTC 存储 | `test_Swc_DtcStore_qm.c` | 通过诊断流程间接覆盖 | DTC 插入/查询/更新操作 | DTC 管理正确性 |
| `Swc_Obd2Pids.c` | OBD-II PID 处理器 | `test_Swc_Obd2Pids_qm.c` | 通过 TCU 诊断流程间接覆盖 | PID 请求 | OBD-II 响应构建正确性 |
| `Swc_UdsServer.c` | UDS 服务端分发 | `test_Swc_UdsServer_qm.c` | 通过 TCU main/诊断路径间接覆盖 | UDS 服务请求 | ISO 14229 服务分发 |
| `tcu_main.c` | TCU 入口点和 10ms 循环 | `test_Swc_TcuMain_qm.c`、`test_Swc_TcuCan_qm.c` | `test_hil_body.py` | 启动、CAN 初始化、主循环 | 周期性循环和 CAN 面向启动行为 |

---

## 主要 ASW 覆盖缺口

### 1. `panda` 风格的 ASW 适配层（已起步，仍在扩展）

此前测试要么是重度 mock 的单元测试，要么是黑盒 CAN/系统测试，缺少一个中间层。该中间层现已落地：

- 以场景友好的方式调用 ASW 入口点，
- 注入内部状态而不重新实现完整的 ECU 框架，
- 用可读的 BDD 步骤断言内部的 ASW 可见输出。

已实现二十条链：

| 功能链 | Feature | 场景数 | 原生 harness |
|---|---|---:|---|
| CVC 踏板 → Torque_Request | `cvc_pedal_torque_request.feature` | 17 | `cvc_pedal_harness.c` |
| CVC 车辆状态机 | `cvc_vehicle_state.feature` | 42 | `cvc_vehiclestate_harness.c` |
| CVC 紧急停止 | `cvc_estop.feature` | 6 | `cvc_estop_harness.c` |
| CVC CAN 通信 | `cvc_cvccom.feature` | 18 | `cvc_cvccom_harness.c` |
| CVC 心跳 | `cvc_heartbeat.feature` | 12 | `cvc_heartbeat_harness.c` |
| CVC CAN 丢失监控 | `cvc_canmonitor.feature` | 14 | `cvc_canmonitor_harness.c` |
| CVC 看门狗 | `cvc_watchdog.feature` | 10 | `cvc_watchdog_harness.c` |
| CVC 启动自检 | `cvc_selftest.feature` | 10 | `cvc_selftest_harness.c` |
| CVC 调度器 | `cvc_scheduler.feature` | 12 | `cvc_scheduler_harness.c` |
| CVC NVM 持久化 | `cvc_nvm.feature` | 21 | `cvc_nvm_harness.c` |
| FZC CAN 通信 | `fzc_fzccom.feature` | 21 | `fzc_fzccom_harness.c` |
| FZC 心跳 | `fzc_heartbeat.feature` | 13 | `fzc_heartbeat_harness.c` |
| FZC CAN 丢失监控 | `fzc_canmonitor.feature` | 14 | `fzc_canmonitor_harness.c` |
| FZC 转向伺服控制 | `fzc_steering.feature` | 26 | `fzc_steering_harness.c` |
| FZC 刹车伺服控制 | `fzc_brake.feature` | 22 | `fzc_brake_harness.c` |
| FZC 激光雷达障碍物检测 | `fzc_lidar.feature` | 29 | `fzc_lidar_harness.c` |
| RZC 电机控制 | `rzc_motor.feature` | 35 | `rzc_motor_harness.c` |
| RZC 电池监控 | `rzc_battery.feature` | 28 | `rzc_battery_harness.c` |
| RZC 温度监控 | `rzc_temponitor.feature` | 31 | `rzc_temponitor_harness.c` |
| RZC CAN 通信 | `rzc_rzccom.feature` | 32 | `rzc_rzccom_harness.c` |
| RZC 调度器 | `rzc_scheduler.feature` | 10 | `rzc_scheduler_harness.c` |

> 说明：车辆状态机 E2E 采用 Given/When 分离：
> - **Given**（`存在:` → `/setup`）只存**前置阶段**——使车辆到达被测前置状态（如自检通过 + 保持周期 → RUN），
>   以及等待后 INIT 宽限期过期的阶段；无前置状态的场景存空 `phases: []`（同时清除上一场景的服务端残留）。
> - **When**（`POST /api/test/asw/cvc/vehicle-state`）body 携带**刺激阶段**——触发状态迁移的最后动作
>   （如 `{cycles:5, brakeFault:true}`），服务端按「前置 + 刺激」顺序执行。
>   未指定的阶段字段通过 spec `defaultValue(null)` + DTO `@JsonInclude(NON_NULL)` 从请求 JSON 中省略，
>   由服务端/harness 默认值接管。注意：JFactory 的内存仓库（`MemoryDataRepository`）默认跨场景残留，
>   其 query-first 行为会让部分匹配的 phase 复用到上一场景的对象（导致 `selfTestPass` 等未指定字段泄漏）；
>   `ApplicationSteps.resetJFactoryRepository()`（`@Before` 每场景清空仓库）已消除该残留。

### 2. 仅间接覆盖的组件

主要的仅间接覆盖的 ASW/运行时部分为：

- `bcm_main.c`
- `firmware/ecu/*/src/main.c` CVC/FZC/RZC 入口点
- `Swc_FzcSensorFeeder.c`
- `Swc_RzcSensorFeeder.c`
- `sc_eth_rx_dispatch.c`
- `sc_monitoring.c`
- `sc_os_cfg.c`
- `sc_startup.S`
- `sc_uds_shim.c`

这些主要通过集成/SIL/HIL 执行，而非通过直接的 ASW 适配测试框架。

### 3. MIL 目前无法作为 ASW-E2E 的基础

`test/mil/` 存在，但目前不提供与其它层级可比的、可运行的模型级验证层。

### 4. 当前端到端验证主要是 CAN 可见的，而非 ASW 可读的

现有 SIL/HIL/PIL 层在验证以下方面很强：

- CAN 帧，
- 时序，
- 故障响应，
- DTC，
- 系统模式转换。

在表达以下方面较弱：

- 以可读的业务/功能步骤描述细粒度的 ASW 行为，
- 内部 SWC 状态演变，
- 可复用的按功能场景词汇。

---

## 未来 ASW E2E 工作的实践结论

### ASW E2E 覆盖情况

已建成 **37 条 ASW E2E 链**（`e2e-tests/src/test/resources/features/`，758 场景，原生 harness 链接真实 SWC 生产代码），覆盖以下 ASW 模型：

- **CVC**：`Swc_Pedal` ✅（`cvc_pedal_torque_request.feature`，17 场景）、`Swc_VehicleState` ✅（`cvc_vehicle_state.feature`，42 场景）、`Swc_EStop` ✅（`cvc_estop.feature`，6 场景）、`Swc_CvcCom` ✅（`cvc_cvccom.feature`，18 场景）、`Swc_Heartbeat` ✅（`cvc_heartbeat.feature`，12 场景，行/分支/函数 100%）、`Swc_CanMonitor` ✅（`cvc_canmonitor.feature`，14 场景，行/分支/函数 100%）、`Swc_Watchdog` ✅（`cvc_watchdog.feature`，10 场景，行 93.5%/分支 92.9%/函数 100%）、`Swc_SelfTest` ✅（`cvc_selftest.feature`，10 场景，行/分支/函数 100%）、`Swc_Scheduler` ✅（`cvc_scheduler.feature`，12 场景，行 92.5%/分支 91.7%/函数 100%）、`Swc_Nvm` ✅（`cvc_nvm.feature`，21 场景，行/分支/函数 100%）
- **FZC**：`Swc_FzcCom` ✅（`fzc_fzccom.feature`，21 场景，行/分支/函数 100%）、`Swc_Heartbeat` ✅（`fzc_heartbeat.feature`，13 场景，行/分支/函数 100%）、`Swc_FzcCanMonitor` ✅（`fzc_canmonitor.feature`，14 场景，行/分支/函数 100%）、`Swc_FzcSafety` ✅（`fzc_safety.feature`，21 场景，行/分支/函数 100%）、`Swc_FzcScheduler` ✅（`fzc_scheduler.feature`，6 场景，行/分支/函数 100%）、`Swc_FzcNvm` ✅（`fzc_nvm.feature`，24 场景，行/分支/函数 100%）、`Swc_Steering` ✅（`fzc_steering.feature`，26 场景）、`Swc_Brake` ✅（`fzc_brake.feature`，22 场景）、`Swc_Lidar` ✅（`fzc_lidar.feature`，29 场景）
- **RZC**：`Swc_Encoder` ✅（`rzc_encoder.feature`，14 场景，行/分支/函数 100%）、`Swc_CurrentMonitor` ✅（`rzc_currentmonitor.feature`，18 场景，行 96.2%/分支 94.1%/函数 100%）、`Swc_Motor` ✅（`rzc_motor.feature`，35 场景，行覆盖 93.8%/函数 100%）、`Swc_Battery` ✅（`rzc_battery.feature`，28 场景，行/分支/函数 100%）、`Swc_TempMonitor` ✅（`rzc_temponitor.feature`，31 场景，行 98.2%/函数 100%）、`Swc_RzcCom` ✅（`rzc_rzccom.feature`，32 场景，行 99.3%/分支 98.7%/函数 100%）、`Swc_Heartbeat` ✅（`rzc_heartbeat.feature`，15 场景，行/分支/函数 100%）、`Swc_RzcSafety` ✅（`rzc_safety.feature`，29 场景，行/分支/函数 100%）、`Swc_RzcSelfTest` ✅（`rzc_selftest.feature`，20 场景，行/分支/函数 100%）、`Swc_RzcScheduler` ✅（`rzc_scheduler.feature`，10 场景，行 100%/函数 100%/分支 85.7%）、`Swc_RzcNvm` ✅（`rzc_nvm.feature`，16 场景，行/分支/函数 100%）
- **SC**：`sc_state` ✅（`sc_state.feature`，15 场景，行/分支/函数 100%）、`sc_heartbeat` ✅（`sc_heartbeat.feature`，26 场景，行 100%/函数 100%/分支 97.6%）、`sc_e2e` ✅（`sc_e2e.feature`，31 场景，行 100%/分支 100%/函数 100%）、`sc_relay` ✅（`sc_relay.feature`，24 场景，行/分支/函数 100%）、`sc_plausibility` ✅（`sc_plausibility.feature`，43 场景，行 98.1%/分支 96.3%/函数 100%）、`sc_watchdog` ✅（`sc_watchdog.feature`，10 场景，行/分支/函数 100%）、`sc_selftest` ✅（`sc_selftest.feature`，23 场景，行/分支/函数 100%）

> FZC `Swc_FzcCom`（2026-08-16 新增）：`fzc_fzccom.feature` 21 场景驱动真实
> `Swc_FzcCom.c`（E2E 发送保护：CRC-8 0x1D + 4-bit alive 计数器 + Data ID 种子、
> alive 递增回绕 15→0、byte1 高半字节保留、重新 Init 复位；E2E 接收校验：
> CRC 损坏/Data ID 不匹配拒绝；RX 周期：CAN 监视器通知复位静默计数器；
> TX 周期调度：心跳 0x011 / 转向状态 0x200 / 制动状态 0x201 / 制动故障 0x210 /
> 电机切断 0x211 / 激光雷达距离 0x220、TxScheduleCycle 1000 回绕）。覆盖报告：
> 行 100%（141/141）、分支 100%（22/22）、函数 100%（6/6）。详见
> `test-design/fzc-fzccom-e2e.md`。无需新增观测 getter：alive 经 E2E 缓冲区
> 观察、TX 周期经既有 `g_dbg_steer_*` 调试计数器观察，**生产代码零改动**。
>
> FZC `Swc_FzcCanMonitor`（2026-08-16 新增）：`fzc_canmonitor.feature` 14 场景
> 驱动真实 `Swc_FzcCanMonitor.c`（500 周期启动宽限期抑制监控、bus-off 立即
> 安全状态、20 周期静默、TEC/REC ≥96 持续 50 周期错误警告、安全状态锁存
> NO-recovery、NotifyRx 静默复位）。覆盖报告：行 100%（83/83）、分支 100%
> （16/16）、函数 100%（10/10，含 5 个 `#ifdef UNIT_TEST` 观测 getter，生产
> 固件不含）。详见 `test-design/fzc-canmonitor-e2e.md`。为观测 SWC 内部静态
> 状态（静默计数器/宽限计数/错误警告计数/安全锁存标志），在
> `Swc_FzcCanMonitor.c/.h` 增加了 UNIT_TEST 保护的观测 getter（仅测试编译，
> 不影响交付固件）。安全状态输出经 harness 的 mock RTE 信号表观测，DTC 上报
> 经 `Dem_ReportErrorStatus` mock 计数观测。唯一编译期排除项为
> `#ifdef PLATFORM_HIL` 解锁恢复分支（L114-129）：原生 harness 以生产固件配置
> 编译（不定义 `PLATFORM_HIL`），该 HIL 平台特性由 HIL 测试
> `sil_004_can_busoff_fzc.yaml` 覆盖，不计入行统计（详见设计文档「无法覆盖的
> 代码说明」）。
>
> FZC `Swc_Heartbeat`（2026-08-16 新增）：`fzc_heartbeat.feature` 13 场景驱动
> 真实 `Swc_Heartbeat.c`（TX 50ms 边界、存活计数器 15 回绕、ECU ID 写入、
> 车辆状态/故障位掩码发布、OperatingMode/FaultStatus 低 4 位掩码、CAN
> bus-off 抑制 TX 与恢复）。覆盖报告：行 100%（42/42）、分支 100%（8/8）、
> 函数 100%（5/5，含 3 个 `#ifdef UNIT_TEST` 观测 getter，生产固件不含）。
> 详见 `test-design/fzc-heartbeat-e2e.md`。为观测 SWC 内部静态状态
> （alive/cycle 计数器、初始化标志），在 `Swc_Heartbeat.c/.h` 增加了
> UNIT_TEST 保护的观测 getter（仅测试编译，不影响交付固件）。TX 输出信号
> 经 harness 的 mock RTE 信号表直接观测，**无需额外 getter**。
>
> FZC `Swc_FzcSafety`（2026-08-16 新增）：`fzc_safety.feature` 21 场景驱动真实
> `Swc_FzcSafety.c`（看门狗 TPS3823 WDI 翻转四条件门控：关键故障/SHUTDOWN/
> 自检失败抑制+DTC 上报、故障聚合 STEER/BRAKE/LIDAR 统一掩码、自检完成且失败
> 置 SELF_TEST 掩码、宽限期后 CAN RX 质量 TIMED_OUT 置 CAN_BUS_OFF(0x0100)、
> 电机切断宽限期抑制/结束后置位、安全状态 OK/DEGRADED/FAULT 发布、重复 Init
> 复位）。覆盖报告：行 100%（120/120）、分支 100%（40/40）、函数 100%（8/8，
> 含 5 个 `#ifdef UNIT_TEST` 观测 getter/注入钩子，生产固件不含）。详见
> `test-design/fzc-safety-e2e.md`。为观测 SWC 内部静态状态（初始化标志/宽限
> 计数/自检标志/WDI 翻转）并驱动自检失败分支（生产代码无 SelfTestDone 置位
> 路径），在 `Swc_FzcSafety.c/.h` 增加了 UNIT_TEST 保护的观测 getter 与
> `SetSelfTestDone` 注入钩子（仅测试编译，不影响交付固件）。WDI 翻转经
> `Dio_WriteChannel` mock 计数观测，DTC 上报经 `Dem_ReportErrorStatus` mock
> 计数观测。**无编译期排除项**（`#ifdef SIL_DIAG` 日志分支被预处理器排除，
> 不计入行统计，见设计文档「无法覆盖的代码说明」）。
>
> FZC `Swc_FzcScheduler`（2026-08-17 新增）：`fzc_scheduler.feature` 6 场景驱动
> 真实 `Swc_FzcScheduler.c`（SWR-FZC-029 静态可运行实体表读回：7 项名称/周期/
> 优先级/WCET/ASIL 逐项一致；未初始化守卫 GetTable 返回 NULL 但 GetCount 恒返回
> 7；重复 Init 幂等；安全任务 ASIL≥C 优先级高于 QM、总 WCET 2900us 未超 10ms
> 周期 80% 上限）。覆盖报告：行 100%（12/12）、分支 100%（2/2）、函数 100%
> （3/3）。无需新增观测 getter：`GetTable` / `GetCount` 为既有公开 API，
> 内部初始化标志经 `GetTable()!=NULL` 完全推断，**生产代码零改动**。详见
> `test-design/fzc-scheduler-e2e.md`。与 CVC `Swc_Scheduler`（存在不可达的
> `Sched_CfgPtr == NULL_PTR` 防御守卫豁免）不同，本模块唯一分支（GetTable 未
> 初始化守卫）两侧均被覆盖，**无无法覆盖的代码**。
>
> FZC `Swc_FzcNvm`（2026-08-17 新增）：`fzc_nvm.feature` 24 场景驱动真实
> `Swc_FzcNvm.c`（DTC 持久化：20 槽首空槽写入 + 满槽拒绝 + 重新 Init 后 NvM
> 后端重载 + CRC-16 损坏检测；校准数据：舵机/制动/激光雷达 7 字段读写往返 +
> 重新 Init 重载 + 后端 CRC 损坏在 Init 内回退默认值 + RAM CRC 损坏回退默认值；
> 公开 CRC-16/CCITT API：NULL→0、零长度→0xFFFF、已知向量 0x89C3）。覆盖报告：
> 行 100%（185/185）、分支 100%（44/44）、函数 100%（12/12，含 3 个
> `#ifdef UNIT_TEST` 观测/损坏注入钩子，生产固件不含）。详见
> `test-design/fzc-nvm-e2e.md`。为观测初始化标志并驱动运行期 DTC/校准 CRC
> 损坏分支，在 `Swc_FzcNvm.c/.h` 增加了 UNIT_TEST 保护的
> `TestGetInitialized` / `TestCorruptDtcCrc` / `TestCorruptCalCrc`；Init 阶段的
> “后端校准 CRC 损坏”路径由 harness 的 `NvM_ReadBlock` mock 后端驱动，**生产
> 代码零功能性改动**。全量 `./gradlew cucumber` 实测 **464 场景 / 2809 步全部通过**。
>
> RZC `Swc_CurrentMonitor`（2026-08-17 新增）：`rzc_currentmonitor.feature` 18 场景
> 驱动真实 `Swc_CurrentMonitor.c`（64 样本 zero-cal 双侧、4 样本移动平均与窗口
> 环绕、>25A/10 周期去抖触发、过流持续上报、500ms 低于阈值恢复、恢复期间单次
> 高原始采样清零恢复计数）。覆盖报告：行 96.2%（102/106）、分支 94.1%
> （32/34）、函数 100%（4/4）。详见 `test-design/rzc-currentmonitor-e2e.md`。
> **无需修改生产代码**：通过 `rzc_currentmonitor_harness.c` 直接观测 RTE/DEM/DIO。
> 唯一未覆盖项为 `CM_ComputeAverage` 中 4 行 / 2 个分支的防御性守卫
> （`count==0`、`count>window`），由于 `Swc_CurrentMonitor_MainFunction`
> 先写入样本并将 `CM_AvgCount` 饱和到不超过窗口大小，故经公开 API 结构不可达。
>
> RZC `Swc_Encoder`（2026-08-17 新增）：`rzc_encoder.feature` 14 场景驱动真实
> `Swc_Encoder.c`（36-count→600RPM、1-count→16RPM、uint32 计数回绕、反向方向
> 输出、PWM=10% 边界不触发卡滞、49→50 周期卡滞确认与锁存、真实正反转后的
> 20/10 周期宽限、命令 STOP 旁路、无位移不累计、4→5 周期方向故障确认与锁存）。
> 覆盖报告：行 100%（96/96）、分支 100%（34/34）、函数 100%（3/3）。详见
> `test-design/rzc-encoder-e2e.md`。**无需修改生产代码**：通过
> `rzc_encoder_harness.c` 直接观测编码器输入、RTE 输出、DEM 计数与 DIO 使能脚。
> 全量 `./gradlew cucumber` 实测 **496 场景 / 3001 步全部通过**。
>
> RZC `Swc_Heartbeat`（2026-08-17 新增）：`rzc_heartbeat.feature` 15 场景驱动
> 真实 `Swc_Heartbeat.c`（TX 50ms 边界、存活计数器 15 回绕、ECU ID 0x03 写入、
> 车辆状态/故障位掩码发布、OperatingMode/FaultStatus 低 4 位掩码、CAN 故障位
> 与 SAFE_STOP 双条件 TX 抑制与恢复）。覆盖报告：行 100%（43/43）、分支 100%
> （10/10）、函数 100%（5/5，含 3 个 `#ifdef UNIT_TEST` 观测 getter，生产固件
> 不含）。详见 `test-design/rzc-heartbeat-e2e.md`。为观测 SWC 内部静态状态
> （alive/cycle 计数器、初始化标志），在 `Swc_Heartbeat.c/.h` 增加了
> UNIT_TEST 保护的观测 getter（仅测试编译，不影响交付固件）。TX 输出信号
> 经 harness 的 mock RTE 信号表直接观测，**无需额外 getter**。
> 与 FZC 心跳不同，RZC 的 bus-off 抑制为 **bit3（`RZC_FAULT_CAN=0x08`）且要求
> 车辆处于 SAFE_STOP 双条件同时成立**（`L109-112`），用例分别覆盖与门两侧
> false 侧（`can_fault_without_safe_stop_no_suppress` /
> `safe_stop_without_can_fault_no_suppress`）。全量 `./gradlew cucumber` 实测
> **511 场景 / 3093 步全部通过**（含本 feature 15 场景）。
>
> RZC `Swc_RzcSafety`（2026-08-18 新增）：`rzc_safety.feature` 29 场景驱动真实
> `Swc_RzcSafety.c`（看门狗 TPS3823 WDI 四条件门控：关键故障/SHUTDOWN/自检
> 失败/bus-off 抑制+DTC 边沿上报、故障聚合 OVERCURRENT/OVERTEMP/DIRECTION/
> STALL/BATTERY/SELF_TEST 统一掩码、CAN 丢失检测 bus-off 立即/200ms 静默
> 20 周期/500ms 错误警告 50 周期/锁存不可恢复、CAN 丢失时 R_EN/L_EN 拉低
> 禁用电机、安全状态 OK/DEGRADED/FAULT 发布、NotifyCanRx 静默复位、重复
> Init 清除锁存）。覆盖报告：行 100%（145/145）、分支 100%（46/46）、
> 函数 100%（4/4）。详见 `test-design/rzc-safety-e2e.md`。**无需修改生产代码**：
> 通过 `rzc_safety_harness.c` 的 mock RTE/Can/Dio/Dem 直接观测输出与 DTC，
> WDI 翻转经 `Dio_WriteChannel` mock 计数观测。唯一编译期排除项为
> `#ifdef SIL_DIAG` 错误警告阈值 500 分支（harness 以生产配置编译，仅 50
> 阈值编译入），由 49/50 边界用例覆盖实际阈值；除此之外无无法覆盖的代码
> （初始化守卫两侧均被覆盖）。
>
> RZC `Swc_RzcSelfTest`（2026-08-18 新增）：`rzc_selftest.feature` 20 场景驱动
> 真实 `Swc_RzcSelfTest.c`（8 项注入式硬件诊断回调：BTS7960 使能引脚切换 /
> ACS723 基线校准 / NTC 温度范围 / 编码器连通性 / CAN 回环 / MPU 区域校验 /
> 栈金丝雀 / RAM 模式测试；任一失败立即禁用电机 R_EN/L_EN+PWM STOP 并上报
> 对应 DTC（SELF_TEST_FAIL / ZERO_CAL / ENCODER / CAN_BUS_OFF）；NULL 配置
> Init 守卫与未初始化守卫；每次运行结果位掩码重置）。覆盖报告：行 100%
> （131/131）、分支 100%（36/36）、函数 100%（4/4）。详见
> `test-design/rzc-selftest-e2e.md`。**无需修改生产代码**：通过
> `rzc_selftest_harness.c` 注入 8 个 mock 硬件回调（值 0=E_NOT_OK、1=E_OK、
> 2=NULL 回调指针），值 2 专门覆盖复合条件 `(pfn != NULL_PTR) &&
> (pfn() == E_OK)` 的 `pfn != NULL` 假侧子分支；DTC 经 `Dem_ReportErrorStatus`
> mock 计数、电机禁用经 `Dio_WriteChannel` / `IoHwAb_SetMotorPWM` mock 观测。
> 无编译期排除项（被测 SWC 无 `#ifdef PLATFORM_HIL` / `SIL_DIAG` / `UNIT_TEST`
> 分支），无无法覆盖的代码。全量 `./gradlew cucumber` 实测 **570 场景 /
> 3449 步全部通过**（含本 feature 20 场景）。
>
> RZC `Swc_RzcScheduler`（2026-08-18 新增）：`rzc_scheduler.feature` 10 场景驱动
> 真实 `Swc_RzcScheduler.c`（SWR-RZC-028 静态可运行实体表读回：8 项周期/优先级/
> WCET 逐项一致；Init 复位 8 个 elapsed 计数器 + 置位初始化标志；未初始化守卫
> Tick 直接返回（fail-closed）但 GetTable 恒返回表（与 FZC 的 NULL 守卫不同）；
> 重复 Init 幂等且复位 elapsed；Tick 周期分派：CurrentMonitor(1ms) 每 tick /
> Motor/Encoder/CanReceive(10ms) 每 10 tick / HeartbeatTx(50ms) 每 50 tick /
> Temp/Battery/Watchdog(100ms) 每 100 tick 精确计数；优先级非递减排序；
> GetUtilPct 返回 10% 远低于 80% 上限）。覆盖报告：行 100%（47/47）、
> 分支 85.7%（12/14）、函数 100%（5/5，含 1 个 `#ifdef UNIT_TEST` 观测 getter
> `Swc_RzcScheduler_GetInitialized`，生产固件不含）。详见
> `test-design/rzc-scheduler-e2e.md`。为观测静态初始化标志，在
> `Swc_RzcScheduler.c/.h` 增加了 UNIT_TEST 保护的观测 getter（仅测试编译，
> 不影响交付固件）；Tick 分派经 8 个 mock 可运行入口计数器逐实体观测，
> **生产逻辑零改动**。**2 个无法覆盖的分支**（行 120 `func != NULL_PTR` 假侧、
> 行 152 `period_ms > 0u` 假侧）：生产表为编译期 `static const`，8 个函数指针
> 全非空、8 个周期全为正，防御分支的假侧通过公开 API 不可达，以文档化豁免处理
> （若引入测试专用表注入钩子会破坏「编译期只读表」的不可变性设计）。
>
> CVC `Swc_Heartbeat`（2026-08-16 新增）：`cvc_heartbeat.feature` 12 场景驱动真实
> `Swc_Heartbeat.c`（TX 50ms 边界、存活计数器 15 回绕、WdgM SE3、RX 指示、
> post-INIT 通信状态复位）。覆盖报告：行 100%（70/70）、分支 100%（10/10）、
> 函数 100%（11/11，含 7 个 `#ifdef UNIT_TEST` 观测 getter，生产固件不含）。
> 详见 `test-design/cvc-heartbeat-e2e.md`。为观测 SWC 内部静态状态，在
> `Swc_Heartbeat.c/.h` 增加了 UNIT_TEST 保护的观测 getter（仅测试编译，不影响
> 交付固件）。
>
> CVC `Swc_CanMonitor`（2026-08-16 新增）：`cvc_canmonitor.feature` 14 场景驱动真实
> `Swc_CanMonitor.c`（bus-off 立即 SAFE_STOP、200ms 静默、500ms 错误警告、
> 10s 窗口 3 次恢复尝试、第 4 次失败 SHUTDOWN、终态短路）。覆盖报告：行 100%
> （118/118）、分支 100%（26/26）、函数 100%（11/11，含 7 个 `#ifdef UNIT_TEST`
> 观测 getter，生产固件不含）。详见 `test-design/cvc-canmonitor-e2e.md`。为观测
> SWC 内部静态状态（静默定时器/错误警告追踪/恢复计数器），在
> `Swc_CanMonitor.c/.h` 增加了 UNIT_TEST 保护的观测 getter（仅测试编译，不影响
> 交付固件）。
>
> CVC `Swc_Watchdog`（2026-08-16 新增）：`cvc_watchdog.feature` 10 场景驱动真实
> `Swc_Watchdog.c`（TPS3823 WDI 四条件门控：主循环完成/栈金丝雀/RAM 模式测试/
> CAN 未 bus-off；NULL 配置与未初始化守卫；WDI 翻转计数与喂狗计数）。覆盖报告：
> 行 93.5%（43/46）、分支 92.9%（13/14）、函数 100%（4/4，含 2 个
> `#ifdef UNIT_TEST` 观测 getter，生产固件不含）。详见
> `test-design/cvc-watchdog-e2e.md`。为观测 SWC 内部静态状态（初始化标志/喂狗
> 计数），在 `Swc_Watchdog.c/.h` 增加了 UNIT_TEST 保护的观测 getter（仅测试编译，
> 不影响交付固件）。唯一未覆盖分支为 `Wdg_CfgPtr == NULL_PTR` 防御守卫的 true
> 侧（3 行）：`Wdg_Initialized` 与 `Wdg_CfgPtr` 在 Init 中同步赋值，二者满足
> `Wdg_Initialized==TRUE ⟹ Wdg_CfgPtr!=NULL` 不变式，Feed 先检查初始化标志，
> 该分支经公开 API 不可达，属合理豁免（详见设计文档「无法覆盖的代码说明」）。
>
> CVC `Swc_SelfTest`（2026-08-16 新增）：`cvc_selftest.feature` 10 场景驱动真实
> `Swc_SelfTest.c`（7 项启动自检：SPI/CAN 回环、NVM 双区 CRC、OLED I2C ACK、
> MPU 区域校验、栈金丝雀、RAM 模式测试；关键检查失败立即终止并上报 DTC，
> OLED 非关键失败不阻断，步骤结果位掩码每次运行清零）。覆盖报告：行 100%
> （77/77）、分支 100%（14/14）、函数 100%（2/2）。无需新增观测 getter：
> `Swc_SelfTest_GetResults` 为既有公开 API，DTC 上报经 harness 的
> `Dem_ReportErrorStatus` 计数替身观测，**生产代码零改动**。详见
> `test-design/cvc-selftest-e2e.md`。设计文档另记录一处生产代码注释与行为
> 不一致（L91 注释称 NVM 失败「continue」，实际 L94-95 立即返回 FAILED），
> 用例按实际行为断言并固化。
>
> CVC `Swc_Scheduler`（2026-08-16 新增）：`cvc_scheduler.feature` 12 场景驱动
> 真实 `Swc_Scheduler.c`（可运行实体表装载：生产 8 项表 SWR-CVC-032 正确读回、
> NULL 配置/空 runnables/零计数守卫拒绝初始化、未初始化守卫、重复 Init 配置
> 替换与 NULL 清除、失败后恢复、最小 1 项/最大 16 项计数边界、安全任务优先级
> 高于 QM、总 WCET 在最短周期内）。覆盖报告：行 92.5%（37/40）、分支 91.7%
> （11/12）、函数 100%（3/3）。无需新增观测 getter：`GetConfig` /
> `GetRunnableCount` 为既有公开 API，**生产代码零改动**。详见
> `test-design/cvc-scheduler-e2e.md`。唯一未覆盖分支为
> `Swc_Scheduler_GetRunnableCount` 中 `Sched_CfgPtr == NULL_PTR` 防御守卫的
> true 侧（3 行）：`Sched_Initialized` 与 `Sched_CfgPtr` 在 Init 中同步赋值，
> 二者满足 `Sched_Initialized==TRUE ⟹ Sched_CfgPtr!=NULL` 不变式，GetRunnableCount
> 先检查初始化标志，该分支经公开 API 不可达，属合理豁免（与 `Swc_Watchdog`
> 的 `Wdg_CfgPtr == NULL_PTR` 守卫同理，详见设计文档「无法覆盖的代码说明」）。
>
> CVC `Swc_Nvm`（2026-08-16 新增）：`cvc_nvm.feature` 21 场景驱动真实
> `Swc_Nvm.c`（DTC 持久化：20-slot 循环缓冲 + 每条目 CRC-16 损坏检测 +
> 32B 冻结帧 NULL/模式两分支 + 回绕覆盖；校准数据：读写往返 + 自定义值 +
> CRC 损坏回退编译期默认值 + 未初始化守卫；CRC-16/CCITT 公开 API 已知向量）。
> 覆盖报告：行 100%（163/163）、分支 100%（42/42）、函数 100%（13/13，含 5 个
> `#ifdef UNIT_TEST` 观测/损坏注入钩子，生产固件不含）。详见
> `test-design/cvc-nvm-e2e.md`。为观测 SWC 内部静态状态（初始化标志/写索引/
> DTC 计数）并驱动 CRC 损坏检测分支（LoadDtc 拒绝、ReadCal 回退默认值），在
> `Swc_Nvm.c/.h` 增加了 UNIT_TEST 保护的观测 getter 与 CRC 损坏注入钩子
> （仅测试编译，不影响交付固件）。

### 扩展优先级

以下为**未**被现有 37 个 feature 覆盖、但具备补建条件的模块。

#### 高优先级（ASIL 类单测 + 现成 SIL/HIL 参考）

| ECU | 模块 | 现有单测 | 可复用的系统级参考 |
|---|---|---|---:|

#### 中优先级（QM 单测 + 有系统参考）

| ECU | 模块 | 现有单测 | 可复用的系统级参考 |
|---|---|---:|---|
| BCM | `Swc_DoorLock` | qm | BCM 主/车身测试 |
| BCM | `Swc_Indicators` | qm | BCM 主/车身测试 |
| BCM | `Swc_Lights` | qm | BCM 主/车身测试 |
| BCM | `Swc_BcmCan` | qm | `test_hil_body.py` |
| ICU | `Swc_Dashboard` | qm | `test_hil_body.py` |
| ICU | `Swc_DtcDisplay` | qm | ICU main/车身流程 |
| TCU | `Swc_UdsServer` | qm | `test_hil_uds.py` |
| TCU | `Swc_Obd2Pids` | qm | TCU 诊断流程 |
| TCU | `Swc_DtcStore` | qm | 诊断流程 |
| TCU | `Swc_DataAggregator` | qm | TCU main/车身流程 |
| CVC | `Swc_CvcDcm` | qm | `test_hil_uds.py` |
| FZC | `Swc_FzcDcm` | qm | `test_hil_uds.py` |
| RZC | `Swc_RzcDcm` | qm | `test_hil_uds.py` |
| SC | `sc_led` | qm | SC 故障流程 |

#### 可选/弱候选（仅间接覆盖或胶水层）

| ECU | 模块 | 说明 |
|---|---|---|
| FZC | `Swc_FzcSensorFeeder` | 仅间接覆盖，`sil_008_sensor_disagreement.yaml`/`sil_011_steering_sensor_failure.yaml` 可参考 |
| RZC | `Swc_RzcSensorFeeder` | 仅间接覆盖 |
| SC | `sc_monitoring` | 仅间接覆盖，SC_Status 广播 |
| SC | `sc_esm` | asilc，锁步错误处理 |
| BCM | `Swc_BcmMain` / `bcm_main.c` | 主循环胶水，系统测试已覆盖 |
| ICU | `icu_main` | 入口点胶水 |
| TCU | `tcu_main` | 入口点胶水 |

#### 不建议做 ASW E2E

- **低层驱动/硬件**：`Ssd1306`、`sc_can`、`sc_eth*`、`sc_xcp_eth`、`sc_eth_rx_dispatch`
- **非 C 逻辑**：`sc_startup.S`、`sc_os_cfg.c`、`sc_uds_shim.c`
- **入口点**：各 ECU `main.c`（集成/SIL/HIL 已覆盖）

#### 建议扩展顺序

> `RZC Swc_Heartbeat` 已于 2026-08-17 完成（`rzc_heartbeat.feature` 15 场景，
> 行/分支/函数 100%），从高优先级列表移入已覆盖清单。
>
> `RZC Swc_RzcSafety` 已于 2026-08-18 完成（`rzc_safety.feature` 29 场景，
> 行/分支/函数 100%），从高优先级列表移入已覆盖清单。
>
> `RZC Swc_RzcSelfTest` 已于 2026-08-18 完成（`rzc_selftest.feature` 20 场景，
> 行/分支/函数 100%），从高优先级列表移入已覆盖清单。
>
> `RZC Swc_RzcScheduler` 已于 2026-08-18 完成（`rzc_scheduler.feature` 10 场景，
> 行 100%/分支 85.7%/函数 100%），从高优先级列表移入已覆盖清单。
>
> `RZC Swc_RzcNvm` 已于 2026-08-18 完成（`rzc_nvm.feature` 16 场景，与
> `cvc_nvm.feature` 对偶：20-slot 循环缓冲 DTC 持久化 + 6 字段冻结帧 +
> 每条目 CRC-16/CCITT 损坏检测 fail-closed + 写索引回绕 + 未初始化守卫），
> 从高优先级列表移入已覆盖清单。覆盖报告：行 100%（117/117）、分支 100%
> （28/28）、函数 100%（9/9，含 3 个 `#ifdef UNIT_TEST` 观测/损坏注入钩子，
> 生产固件不含）。详见 `test-design/rzc-nvm-e2e.md`。为观测静态初始化标志、
> 驱动运行期 DTC CRC 损坏分支并验证静态 CRC 计算器，在 `Swc_RzcNvm.c/.h`
> 增加了 UNIT_TEST 保护的 `TestGetInitialized` / `TestCorruptDtcCrc` /
> `TestCrc16`（仅测试编译，不影响交付固件）；写索引经既有公开
> `GetWriteIndex` 观测，**生产逻辑零改动**。`RzcNvm_Crc16` 为 static 内部
> 函数无 NULL 分支，已知向量/零长度经测试钩子覆盖。全量 `./gradlew cucumber`
> 实测 **586 场景 / 3545 步全部通过**（含本 feature 16 场景）。
>
> `SC sc_state` 已于 2026-08-18 完成（`sc_state.feature` 15 场景，首个 SC
> 平台 ASW E2E：GAP-SC-006 权威运行时状态机 INIT/MONITORING/FAULT/KILL
> 合法边接受、非法迁移拒绝且状态不变（fail-closed）、KILL 终态无迁出边、
> 未知状态强制 KILL）。覆盖报告：行 100%（38/38）、分支 100%（18/18）、
> 函数 100%（4/4，含 1 个 `#ifdef UNIT_TEST` 注入钩子，生产固件不含）。
> 详见 `test-design/sc-state-e2e.md`。为驱动 `default` fail-closed 分支
> （未知内部状态 0xFF，生产固件仅内存损坏可达、无法经公开 API 构造），在
> `sc_state.c/.h` 增加了 UNIT_TEST 保护的 `SC_State_TestSetRaw`（仅测试编译，
> 不影响交付固件）；状态读回经公开 `SC_State_Get` 观测，**生产逻辑零改动**。
> 全量 `./gradlew cucumber` 实测 **601 场景 / 3635 步全部通过**（含本 feature
> 15 场景）。
>
> `SC sc_heartbeat` 已于 2026-08-18 完成（`sc_heartbeat.feature` 26 场景：
> 三路独立 ECU CVC/FZC/RZC 心跳监控，150 tick 超时检测 + 故障 LED 驱动、
> 20 tick 确认窗口锁存（不可恢复）、3 心跳恢复去抖、1500 tick 启动宽限期、
> 内容校验 SWR-SC-027/028——DEGRADED/LIMP 模式累计 100 次锁存 + FaultStatus
> ≥2 bit 累计 20 次锁存 + 计数 0xFF 饱和 + FZC 制动位检测）。覆盖报告：
> 行 100%（179/179）、函数 100%（18/18）、分支 97.6%（80/82）。详见
> `test-design/sc-heartbeat-e2e.md`。为观测模块内部静态状态（超时/确认/
> 恢复/内容校验计数与标志、宽限计数器），在 `sc_heartbeat.c/.h` 增加了
> 10 个 UNIT_TEST 保护的观测 getter（仅测试编译，不影响交付固件）；LED
> 经 harness 的 `gioSetBit` mock 观测，越界守卫经 harness guardProbe
> （SC_ECU_COUNT 索引）驱动，**生产逻辑零改动**。**2 个豁免分支**（行 139
> counter 饱和 0xFFFF false 侧——确认锁存于 170 tick 后 Monitor 跳过该 ECU
> 无法递增；行 144 冗余 `hb_confirmed==FALSE` 检查 false 侧——L134 已跳过
> 所有已确认 ECU）为防御性守卫，经公开 API 不可达，以文档化豁免处理；
> `#ifdef PLATFORM_HIL` 跳过 FZC 分支由预处理器排除（HIL 测试
> `test_hil_heartbeat.py` 覆盖）。全量 `./gradlew cucumber` 实测 **627 场景 /
> 3791 步全部通过**（含本 feature 26 场景）。
>
> `SC sc_e2e` 已于 2026-08-18 完成（`sc_e2e.feature` 31 场景：SWR-SC-003
> CRC-8（SAE-J1850，poly 0x1D，init 0xFF）逐帧校验 + byte0 DataId 低半字节
> 校验 + alive 计数器单调递增（15→0 回绕）、每邮箱连续失败计数阈值 3 持久
> 锁存 + 有效帧重置、启动宽限期 5 tick 与到期复位全部失败状态、GAP-SC-002
> 关键邮箱（E-Stop + CVC/FZC/RZC 心跳）锁存触发 relay-kill、非关键邮箱
> 不触发，以及 SWR-SC-030 SC_Status TX `SC_E2E_ComputeCRC8`（NULL→0/空→
> 0x00/已知向量 0xB7/单字节 0x26））。覆盖报告：**行 100%（149/149）、分支
> 100%（66/66）、函数 100%（12/12，含 6 个 `#ifdef UNIT_TEST` 观测 getter
> 与 `TestCrc8` 钩子，生产固件不含）**。详见 `test-design/sc-e2e-e2e.md`。
> 为观测模块内部静态状态（last_alive/first_rx/fail_count/failed/宽限计数）
> 并直接驱动静态 `sc_crc8` 零长度分支，在 `sc_e2e.c/.h` 增加了 UNIT_TEST
> 保护的观测 getter（仅测试编译，不影响交付固件）；harness 以生产 TMS570
> 配置编译（无 PLATFORM_POSIX/HIL，严格 3 次阈值与 5 tick 宽限生效），
> 越界守卫经 guardProbe（SC_MB_COUNT 索引）驱动，**生产逻辑零改动**。
> **无无法覆盖的分支**——`#ifdef PLATFORM_POSIX` 的 SIL 锁存清除/禁用强制
> 块与 `#elif PLATFORM_HIL` 宽限分支由预处理器排除（HIL 测试
> `test_hil_e2e.py` 覆盖），与 `Swc_RzcScheduler` 的编译期只读表防御分支
> 不同，本模块 66/66 分支全部两侧命中。全量 `./gradlew cucumber` 实测
> **658 场景 / 3977 步全部通过**（含本 feature 31 场景）。
>
> `SC sc_relay` 已于 2026-08-18 完成（`sc_relay.feature` 24 场景：SWR-SC-010/
> 011/012 切断继电器控制——Init 置继电器 LOW 安全态且不清除 kill 锁存、
> Energize 受锁存门控、DeEnergize 永久锁存断开（仅断电复位）、10ms
> CheckTriggers 级联触发（E-Stop 最高优先级、心跳确认超时、合理性故障、
> 蠕动防护 SSR-SC-018、E2E 关键邮箱持久失败 GAP-SC-002、自检失败、ESM
> 锁步错误、CAN bus-off、CAN 静默、GPIO 读回连续 2 次失配）及优先级交叉
> 验证）。覆盖报告：**行 100%（122/122）、分支 100%（30/30）、函数 100%
> （9/9，含 3 个 `#ifdef UNIT_TEST` 观测 getter，生产固件不含）**。详见
> `test-design/sc-relay-e2e.md`。为观测模块内部静态状态（命令状态/锁存/
> 读回失配计数），在 `sc_relay.c/.h` 增加了 UNIT_TEST 保护的观测 getter
> （仅测试编译，不影响交付固件）；外部模块 getter 为 harness 注入 mock，
> 继电器 GIO 读回值经 `setReadback` 注入驱动读回失配分支，公开
> `SC_Relay_IsKilled` 经 harness `killedApi` 观测交叉验证，**生产逻辑零改动**。
> **无无法覆盖的代码**——`#ifdef PLATFORM_HIL` 的 DeEnergize 空操作块与
> `#if defined(PLATFORM_POSIX) || defined(PLATFORM_HIL)` 的 IsKilled 抑制块
> 由预处理器排除（HIL/SIL 测试 `sil_005_watchdog_timeout_cvc.yaml`、
> `test_sc_integration.py` 覆盖），本模块 30/30 分支全部两侧命中。全量
> `./gradlew cucumber` 实测 **682 场景 / 4121 步全部通过**（含本 feature
> 24 场景）。从高优先级列表移入已覆盖清单。
>
> `SC sc_plausibility` 已于 2026-08-18 完成（`sc_plausibility.feature` 43
> 场景：SWR-SC-007/008/009/024 扭矩-电流交叉校验 + SSR-SC-018 蠕动防护——
> 16 项 LUT 线性插值（0/1/7/50/99/100/255 各边界与首/中/末区间）、合理性
> 比较（近零绝对阈值 2000mA、相对阈值 20% + 绝对下限、全部比较分支）、
> 10-tick 去抖后故障锁存 + 系统 LED、FZC 制动故障 + 电流>1000mA 的
> 10-tick 备份切断（含电流 1000/1001mA 边界、计数器回落复位、不依赖车辆
> 状态邮箱）、零扭矩 + 电流>500mA 的 2 周期蠕动防护（500/501mA 边界、
> 非零扭矩清零、CAN 缺失、宽限抑制、不可清除锁存）、1500-tick 共享启动
> 宽限）。覆盖报告：**行 98.1%（153/156）、分支 96.3%（52/54）、函数
> 100%（13/13，含 6 个 `#ifdef UNIT_TEST` 观测 getter/钩子，生产固件
> 不含）**。详见 `test-design/sc-plausibility-e2e.md`。为观测模块内部静态
> 状态（去抖/备份计数/宽限/蠕动去抖）并驱动静态 `lookup_expected_current`
> 与 `is_implausible` 全部分支，在 `sc_plausibility.c/.h` 增加了 UNIT_TEST
> 保护的观测 getter（仅测试编译，不影响交付固件）；CAN 数据（扭矩/电流）、
> 心跳制动故障标志、GIO 系统 LED 均为 harness 注入 mock，公开
> `IsFaulted`/`IsCreepFaulted` 经 harness state 快照交叉验证，**生产逻辑
> 零改动**。**唯一未覆盖代码**为 `lookup_expected_current` 内 2 个编译期
> 不可达防御分支：`pct_range==0` true 侧（L98-99，LUT 静态严格递增、相邻
> 差≥6，恒不为 0）与兜底 `return 25000`（L109，uint8 全输入 0..255 均被
> `torque==0`/`torque>=100`/插值循环捕获）——共 3 行/2 分支豁免，其余
> 52/54 分支全部两侧命中。全量 `./gradlew cucumber` 实测 **725 场景 /
> 4379 步全部通过**（含本 feature 43 场景）。从高优先级列表移入已覆盖清单。
>
> `SC sc_watchdog` 已于 2026-08-18 完成（`sc_watchdog.feature` 10 场景：
> SWR-SC-022 TPS3823 外部看门狗喂狗控制——Init 置 WDI 引脚 LOW、全部条件
> 满足（allChecksOk==TRUE）时每次 Feed 翻转 WDI 引脚（0→1→0→1…）、
> 任一条件失败（FALSE）时 Feed 不写引脚（看门狗饿死，TPS3823 超时复位）、
> 失败恢复后从最后成功电平继续翻转、交替成功/失败保持最后成功状态、
> 100 次长序列奇偶回绕边界、重复 Init 复位）。覆盖报告：**行 100%
> （10/10）、分支 100%（2/2）、函数 100%（2/2）**。详见
> `test-design/sc-watchdog-e2e.md`。**无需修改生产代码**：通过
> `sc_watchdog_harness.c` 的 mock GIO 观测 WDI 引脚电平（wdiPin）与写入
> 计数（wdiWriteCount）——`wdi_state` 与引脚电平一一对应，每次翻转必然
> 伴随一次引脚写入，无需 UNIT_TEST 观测 getter。唯一分支（`allChecksOk ==
> TRUE`）两侧均被覆盖，**无无法覆盖的代码**（模块无 `#ifdef PLATFORM_HIL`
> / `SIL_DIAG` 分支、无防御性守卫）。全量 `./gradlew cucumber` 实测
> **735 场景 / 4439 步全部通过**（含本 feature 10 场景）。从高优先级列表
> 移入已覆盖清单。
>
> `SC sc_selftest` 已于 2026-08-18 完成（`sc_selftest.feature` 23 场景：
> SWR-SC-016..021 启动/运行时自检——Init 植入堆栈金丝雀与 RAM 交替模式、
> 7 步启动 BIST 任一步失败返回步骤号 1..7 且阻断后续步骤（mock 调用计数
> 证明）、60s 周期运行期分步检查（tick 1 Flash CRC 增量 / tick 1500 RAM
> 32 字节模式校验 / tick 3000 DCAN 错误状态 / tick 4500 GIO 继电器读回 /
> tick 6000 回绕）、运行期失败锁存不健康、启动未完成 fail-closed、金丝雀
> 损坏检测、IsHealthy 双条件门控）。覆盖报告：**行 100%（99/99）、分支
> 100%（44/44）、函数 100%（10/10，含 5 个 `#ifdef UNIT_TEST` 观测/注入
> 钩子，生产固件不含）**。详见 `test-design/sc-selftest-e2e.md`。为观测
> 内部 tick/健康标志并驱动金丝雀与 RAM 模式损坏分支（生产固件仅真实内存
> 损坏可达、无法经公开 API 构造），在 `sc_selftest.c/.h` 增加了 UNIT_TEST
> 保护的 `TestGetRuntimeTick`/`TestGetStartupPassed`/`TestGetRuntimeHealthy`/
> `TestCorruptCanary`/`TestCorruptRam`（仅测试编译，不影响交付固件）；
> 7 项启动诊断与 2 项运行期检查为 harness mock 并按调用计数，
> **生产逻辑零改动**。**无无法覆盖的代码**——模块无 `#ifdef PLATFORM_HIL`
> / `SIL_DIAG` 分支、无防御性守卫，44/44 分支全部两侧命中（`runtime_step`
> 为从未被读取的残留状态变量，其赋值行经 Init/回绕用例覆盖）。全量
> `./gradlew cucumber` 实测 **758 场景 / 4577 步全部通过**（含本 feature
> 23 场景）。从高优先级列表移入已覆盖清单。

每个新 feature 需配套：`gateway/fault_inject/native/<swc>_harness.c`、`features/<ecu>_<swc>.feature`、`test-design/<name>-e2e.md`、Java DTO/spec/Factory。
