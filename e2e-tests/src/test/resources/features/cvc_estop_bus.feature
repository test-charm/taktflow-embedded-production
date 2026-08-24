# language: zh-CN
功能: CVC E-Stop 真·ASW 端到端 — 按钮到总线 (cvc_estop_bus)

  [按钮事件] → Swc_EStop(真实) → Rte → Com/PduR/CanIf(真实) → CAN帧 0x001
                    ↓
  断言：总线上真实出现 EStop_Broadcast(0x001)、E2E 头可解（dataId=1 / alive 单调 /
  CRC-8）、锁存广播 cadence 达标、且首帧在 FTTI 预算内到达。

  与 `cvc_estop.feature`（mock BSW 的 SWC 行为测试）互补：本 feature 是**真端到端**
  ——被测系统为插桩的真实 CVC SIL ECU（docker-compose.dev.yml 栈，vcan0）。按钮
  事件经 UDP DIO 注入真实 IoHwAb 引脚（用户可见侧），全链 IoHwAb → Swc_EStop →
  Rte → Com → PduR → CanIf → Can_Posix 以生产代码执行，无任何 mock；断言侧是
  vcan0 上的真实帧（真实 BSW 侧）。

  E-Stop 锁存为断电级（设计语义）：每个场景断言后经 `restartCvc` 重启 CVC 容器
  清除锁存并验证恢复（`cvcRestarted`）。

  前置条件：`docker compose -f docker/docker-compose.dev.yml up`（真实 vECU 在
  vcan0 上运行，CVC 为 LLVM_COV 插桩构建）。

  规则: 真端到端 — E-Stop 按钮 → 0x001 帧上总线（真实 BSW 链）

    场景: 按钮按压后 0x001(Active=1) 在 FTTI 预算内上总线且 E2E 可解
      当POST "/api/test/asw/cvc/estop-bus":
      """
      {
        "phases": [
          { "op": "estop", "budgetMs": 200, "restartCvc": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        estopFrameSeen: true
        withinBudget: true
        frame: {
          dlc: 4
          dlcOk: true
          dataId: 1
          dataIdOk: true
          aliveMonotonic: true
          crcValid: true
        }
      }
      """

    场景: 锁存广播流 cadence 达标且结束后重启 CVC 清锁存
      当POST "/api/test/asw/cvc/estop-bus":
      """
      {
        "phases": [
          { "op": "estop", "budgetMs": 200, "restartCvc": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        ecu: "cvc"
        estopFrameSeen: true
        frame: {
          cycleMs: 10
          cycleOk: true
        }
        restartCvc: true
        cvcRestarted: true
      }
      """
