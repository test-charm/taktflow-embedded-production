# CVC 踏板 -> Torque_Request E2E 测试设计

## 被测功能

**CVC ASW 踏板处理到 Torque_Request 输出**

覆盖链路：

```text
踏板传感器输入
  → Swc_Pedal_MainFunction
  → RTE 扭矩/故障输出
  → Swc_CvcCom_TransmitSchedule
  → Torque_Request 命令信号
```

这是首个面向 ASW 的 E2E 用例，因为它是：

1. 比紧急停止/多 ECU 安全状态链更简单，
2. 仍然代表核心车辆控制逻辑，
3. 直接关联 CVC ASW 层，
4. 接近 `panda` 原生固件 feature 测试的风格。

测试**不**通过仪表盘/系统 E2E 运行器。  
而是使用**测试专用 API**，在原生测试框架内执行 `Swc_Pedal.c` 和 `Swc_CvcCom.c` 的真实 C 生产代码。

---

## 输入和输出

### 输入因子

| 因子 | 含义 | 等价类/范围 | 选定值 |
|---|---|---|---|
| `sensor1Pct` | 踏板传感器 1 百分比 | 0..100，标称相等，大幅不匹配 | `40`、`20`、`100` |
| `sensor2Pct` | 踏板传感器 2 百分比 | 0..100，标称相等，大幅不匹配 | `40`、`80`、`100` |
| `vehicleState` | CVC 模式限制源 | `RUN`、`DEGRADED`、`LIMP`、`SAFE_STOP`、`SHUTDOWN`、`INIT` | `RUN`、`DEGRADED` |
| `cycles` | 要执行的 10ms 踏板周期数 | 消抖/斜坡不足，消抖足够，斜坡饱和足够 | `2`、`100`、`200` |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `outputs.torqueRequestPct` | ASW 写入的最终扭矩请求百分比 | `40`、`0`、`75` |
| `outputs.pedalFaultName` | ASW 踏板故障分类 | `NONE`、`PLAUSIBILITY` |
| `outputs.torqueDirection` | 扭矩方向信号 | 扭矩 > 0 时为 `1`，扭矩 = 0 时为 `0` |
| `outputs.comSignals.torqueRequestCommandPct` | `Swc_CvcCom` 转发的值 | 与 `torqueRequestPct` 相同 |

---

## 输入范围分析

### 1. 踏板传感器百分比

测试框架接受百分比 `0..100` 并将其转换为 `Swc_Pedal` 使用的原始 14 位值。

相关等价类：

1. **匹配的标称值** — 无合理性故障
2. **大幅不匹配** — 超过合理性阈值，应触发故障
3. **高需求** — 斜坡稳定后达到模式限制行为

### 2. 车辆状态

`Swc_Pedal` 应用模式限制：

- `RUN` → 100%
- `DEGRADED` → 75%
- `LIMP` → 30%
- `SAFE_STOP / SHUTDOWN / INIT` → 0%

首个 feature 集覆盖：

- `RUN` 用于标称路径
- `DEGRADED` 用于代表性限制行为

### 3. 周期数

此因子至关重要，因为：

- 合理性使用消抖，
- 扭矩通过斜坡限制上升，
- 因此单个周期不具有代表性。

选定的等价类：

1. **2 个周期** — 足以触发合理性消抖
2. **100 个周期** — 足以使标称 40% 扭矩稳定
3. **200 个周期** — 足以使 100% 输入稳定然后被 DEGRADED 模式限制

---

## 流程

```text
[接收踏板请求]
  ═══→ [转换百分比 -> 原始传感器值]
  ═══→ [初始化 Swc_Pedal + Swc_CvcCom]
  ═══→ [运行 N 个踏板周期]
  ═══→ {传感器不匹配？}
         ├─ 是 → [踏板故障锁存 / 扭矩 = 0]
         └─ 否 → [扭矩查表 + 斜坡]
                  ═══→ {车辆状态限制激活？}
                        ├─ 是 → [限制扭矩]
                        └─ 否 → [保持扭矩]
  ═══→ [通过 CvcCom 桥接扭矩]
  ═══→ [返回 JSON 输出]
```

---

## 测试用例

| 用例名称 | sensor1Pct | sensor2Pct | vehicleState | cycles | 期望 torqueRequestPct | 期望故障 | 期望方向 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| run_matching_40pct_produces_40pct_torque | 40 | 40 | RUN | 100 | 40 | NONE | 1 |
| run_mismatched_pedals_zero_torque_after_debounce | 20 | 80 | RUN | 2 | 0 | PLAUSIBILITY | 0 |
| degraded_mode_caps_full_pedal_to_75pct | 100 | 100 | DEGRADED | 200 | 75 | NONE | 1 |

---

## 覆盖检查清单

### 代码路径覆盖

- 正常无故障路径已覆盖
- 合理性故障路径已覆盖
- 模式限制路径已覆盖

### 输入覆盖

- 匹配输入已覆盖
- 不匹配输入已覆盖
- 标称状态已覆盖
- 降级状态已覆盖
- 消抖敏感的周期数已覆盖
- 斜坡饱和的周期数已覆盖

### 分支覆盖

- 合理性分支：通过和失败均已覆盖
- 模式限制分支：未限制和已限制均已覆盖
- 扭矩方向分支：零和非零均已覆盖
