# ASW E2E 机制 — CVC 踏板到 Torque_Request

## 目标

本文档解释当前 CVC ASW 层端到端测试的工作原理。

目标不是仪表盘测试运行器或系统级 SIL 场景。  
目标是 **CVC 应用层信号路径**：

```text
踏板传感器 1/2
  → Swc_Pedal_MainFunction
  → RTE 扭矩/故障信号
  → Swc_CvcCom_TransmitSchedule
  → Torque_Request 命令信号
```

这有意更接近 `panda` 风格：

- 从可读的 feature 文件测试，
- 驱动专用测试适配器，
- 执行真实的生产 C 代码，
- 直接断言 ASW 面向输出。

---

## 文件

### Feature 和设计

- `e2e-tests/src/test/resources/features/cvc_pedal_torque_request.feature`
- `e2e-tests/src/test/resources/test-design/cvc-pedal-torque-request-e2e.md`

### API / 测试框架

- `gateway/fault_inject/app.py`
- `gateway/fault_inject/native/cvc_pedal_harness.c`
- `gateway/fault_inject/Dockerfile`

### 测试框架执行的生产 C 代码

- `firmware/ecu/cvc/src/Swc_Pedal.c`
- `firmware/ecu/cvc/src/Swc_CvcCom.c`

---

## 高层架构

```text
[Cucumber feature]
  → [RESTful-cucumber POST]
  → [/api/test/asw/cvc/pedal-torque]
  → [原生 C 测试框架]
  → [真实 CVC 生产 C 文件]
  → [JSON 结果]
  → [DAL 断言]
```

更具体地：

```text
e2e-tests
  └─ POST /api/test/asw/cvc/pedal-torque
       └─ fault_inject FastAPI 端点
            └─ subprocess.run("/app/bin/cvc_pedal_harness ...")
                 └─ Swc_Pedal.c
                 └─ Swc_CvcCom.c
                      └─ JSON stdout
```

---

## 为什么这是 ASW E2E 而非系统 E2E

当前机制**不**依赖：

- 仪表盘 `run-sync` 包装器，
- 完整的 SIL 场景编排，
- 多 ECU 状态转换，
- MQTT 判定监控，
- 通过 CAN 总线观察推断内部行为。

而是直接执行：

1. **踏板输入处理**
2. **合理性逻辑**
3. **斜坡/扭矩映射**
4. **车辆状态模式限制**
5. **Torque_Request 信号桥接到 Com**

因此测试对象是 **ASW 行为本身**，而非仪表盘 API。

---

## 请求/响应契约

### 测试端点

`POST /api/test/asw/cvc/pedal-torque`

### 请求体

```json
{
  "sensor1Pct": 40,
  "sensor2Pct": 40,
  "vehicleState": "RUN",
  "cycles": 100
}
```

### 响应体

```json
{
  "inputs": {
    "sensor1Pct": 40,
    "sensor2Pct": 40,
    "vehicleState": 1,
    "cycles": 100
  },
  "outputs": {
    "pedalPosition": 400,
    "pedalFaultCode": 0,
    "pedalFaultName": "NONE",
    "torqueRequestPct": 40,
    "torqueDirection": 1,
    "comSignals": {
      "torqueRequestCommandPct": 40
    }
  }
}
```

---

## 原生测试框架实现

原生测试框架位于 `gateway/fault_inject/native/cvc_pedal_harness.c`。

它直接链接真实的生产源代码：

- `Swc_Pedal.c`
- `Swc_CvcCom.c`

并为以下接口提供最小测试替身：

- `IoHwAb_ReadPedalAngle`
- `Rte_Read`
- `Rte_Write`
- `Com_SendSignal`
- `Com_ReceiveSignal`
- `Dem_ReportErrorStatus`
- `Swc_VehicleState_GetState`

### 重要说明

这**不是**手工重新实现的踏板算法。  
踏板逻辑仍然由真实的 `Swc_Pedal_MainFunction` 执行。

测试框架仅提供：

1. 输入注入，
2. 所需的外围接口，
3. 结果提取。

---

## 踏板输入如何表示

feature 发送百分比（`0..100`）。  
测试框架将其转换为生产踏板 SWC 期望的原始 14 位值。

```text
传感器百分比
  → 14 位原始 AS5048A 风格值
  → IoHwAb_ReadPedalAngle
  → Swc_Pedal_MainFunction
```

这使测试保持可读性，同时仍在真实的抽象级别进入生产代码。

---

## 为什么测试框架添加微小的输入抖动

`Swc_Pedal.c` 包含真实的**卡滞传感器检测器**。  
如果测试框架在太多周期内输入完全恒定的原始值，生产代码会正确地将其分类为 `STUCK`。

真实传感器通常有微小的自然抖动，因此测试框架在周期之间添加非常小的确定性抖动：

```text
基准原始值
  → 基准 + 0
  → 基准 + 16
  → 基准 + 0
  → 基准 + 16
  ...
```

这不会改变生产逻辑。  
它只是防止测试夹具意外地模拟不切实际的完美冻结传感器。

---

## 执行模型

请求执行以下步骤：

```text
[验证 JSON 输入]
  → [将 vehicleState 字符串映射为枚举]
  → [启动原生测试框架二进制]
  → [运行 N 个踏板周期]
  → [收集 RTE + Com 输出]
  → [返回 JSON]
```

测试框架内部：

```text
[初始化配置/状态]
  → [Swc_Pedal_Init]
  → [Swc_CvcCom_Init]
  → 重复周期：
       [设置踏板原始值]
       [Swc_Pedal_MainFunction]
       [Swc_CvcCom_TransmitSchedule]
  → [序列化输出]
```

---

## 当前覆盖的断言

feature 目前验证 3 种代表性行为：

| 场景 | 证明内容 |
|---|---|
| `RUN` 下匹配输入 | 标称踏板到扭矩生成 |
| `RUN` 下不匹配输入 | 合理性故障将扭矩归零 |
| `DEGRADED` 下满踏板 | 车辆状态限制将扭矩上限设为 75% |

这提供了对以下方面的首次覆盖：

- 正常路径
- 安全故障路径
- 降级模式限制路径

---

## 构建机制

测试框架二进制在 Docker 构建期间编译到 `fault-inject` 镜像中。

相关的 Dockerfile 步骤：

1. 安装 `gcc` 和 `libc6-dev`
2. 复制 `firmware/`
3. 将 `cvc_pedal_harness.c` 与 `Swc_Pedal.c` 和 `Swc_CvcCom.c` 一起编译
4. 将二进制暴露为：

```text
/app/bin/cvc_pedal_harness
```

运行时，FastAPI 端点使用 `subprocess.run(...)` 调用该二进制。

---

## 与 SIL 的关系

此测试**不需要**完整的仪表盘判定运行器，但它仍复用 `fault-inject` 服务作为测试专用 API 的稳定 HTTP 宿主。

因此分层为：

```text
SIL 基础设施
  └─ fault_inject 服务
       └─ ASW 测试专用 API
            └─ 原生测试框架
                 └─ 生产 CVC ASW 代码
```

这意味着：

- 我们保留现有的 SIL 环境，
- 但实际的断言目标更窄且更 ASW 特定。

---

## 为什么选择这种方法

与包装现有系统测试相比：

| 选项 | 结果 |
|---|---|
| 包装仪表盘运行器 | 测试 API/系统编排 |
| 仅断言 CAN/MQTT | 黑盒，ASW 特定性较差 |
| 测试 API 后的原生 ASW 框架 | 直接、可读、聚焦生产代码 |

选择当前方法是因为它最匹配以下要求：

- **简单但有代表性**
- **聚焦 ASW**
- **类 panda E2E 风格**

---

## 当前限制

1. 测试框架目前仅覆盖 **CVC 踏板 -> Torque_Request** 链。
2. 尚未验证：
   - 完整 CAN 帧打包，
   - E2E CRC 字段，
   - 多 ECU 反应，
   - plant 响应。
3. 这些属于更高层级，应保留在 SIL/HIL 测试中。

这种划分是有意的：

- **ASW E2E** 检查逻辑和信号输出，
- **SIL/HIL** 检查网络/系统行为。

---

## 推荐的后续扩展

在此首个 ASW E2E 之后，下一个好的候选是：

1. `踏板故障锁存清除`
2. `车辆状态模式 = SAFE_STOP -> 扭矩 = 0`
3. `CVC 紧急停止 -> 车辆状态转换`
4. `电池/过温 -> CVC 模式限制交互`

这些可以复用相同的模式：

```text
feature
  → 测试专用 API
  → 原生测试框架
  → 生产 C 模块
```
