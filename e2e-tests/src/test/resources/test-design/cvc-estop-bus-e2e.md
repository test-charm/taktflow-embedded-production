# CVC E-Stop 真·ASW 端到端 (cvc_estop_bus) 测试设计

> 2026-08-24：新增。与 `cvc-estop-e2e.md`（mock-BSW 的 SWC 行为测试）互补，
> 本文件描述**真端到端**版本：一端是用户可见的按钮事件，另一端是真实
> BSW + vcan0 总线。

## 被测功能

**CVC 紧急停止全链的真实总线后果**

被测链路（全部真实生产代码，无 mock）：

```text
[UDP DIO 按钮注入 (发送 0xE500 → CVC SPI_UDP 9100)]
  → IoHwAb_ReadEStop（真实 DIO 引脚）
  → Swc_EStop（真实：消抖 → 锁存 → RTE 置位 → 广播信号）
  → Rte → Com/PduR/CanIf（真实 BSW）
  → Can_Posix → vcan0
  → EStop_Broadcast (0x001, Active=1) 周期性广播（锁存期间 ~10ms）
```

断言（全部来自真实帧）：首帧到达时延（FTTI 预算 200ms）、DLC=4、
E2E 头（byte0 = Alive:4|DataId:4，DataId=1；byte1 = CRC-8 SAE-J1850）有效、
alive 单调递增、锁存广播 cadence 与 DBC GenMsgCycleTime(10ms) 兼容。

## 输入和输出

| 因子 | 含义 | 默认 |
|---|---|---|
| `op` | estop（按钮注入+测量+恢复） | `estop` |
| `budgetMs` | FTTI 预算（首帧 0x001 到总线时延上限） | `200` |
| `restartCvc` | 结束后重启 CVC 容器清断电级 E-Stop 锁存 | `true` |

输出：`found` / `busUp` / `estopFrameSeen` / `latencyMs` / `withinBudget` /
`frame{ dlc, dlcOk, dataId, dataIdOk, aliveMonotonic, crcValid, cycleMs,
cycleOk, decoded }` / `restartCvc` / `cvcRestarted`。

## 测试用例

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `estop_bus_frame` | P0: estop budgetMs=200 restartCvc=true | found=true, busUp=true, estopFrameSeen=true, withinBudget=true, frame: dlc=4, dlcOk=true, dataId=1, dataIdOk=true, aliveMonotonic=true, crcValid=true |
| `estop_bus_cadence_restore` | P0: estop（同上） | found=true, estopFrameSeen=true, frame: cycleMs=10, cycleOk=true, restartCvc=true, cvcRestarted=true |

## 实现

- 网关：`gateway/fault_inject/app.py` 新增 `/api/test/asw/cvc/estop-bus`
  （`CvcEstopBusPhase` 模型），复用 `gateway/fault_inject/bsw_bus_probe.py`
  的 `ftti_estop`（UDP 注入 + 首帧计时 + 锁存流帧属性）。请求字段
  `budgetMs`/`minFrames`/`restartCvc`。
- 前置条件：SIL Docker 栈运行、CVC 为 LLVM_COV 插桩（覆盖自然产生）。

## 覆盖

真实 CVC 在 estop-bus 执行期间运行的代码进入 `e2e-tests/build/coverage/`：
`Swc_EStop.c`、`IoHwAb_Posix.c`、`Com.c`、`E2E.c`/`E2E_Sm.c`、`PduR`/`CanIf`/
`Can_Posix`、`Rte.c` 等均为真实执行覆盖（`/cov` .profraw → 网关
`_collect_ecu_coverage_info`）。