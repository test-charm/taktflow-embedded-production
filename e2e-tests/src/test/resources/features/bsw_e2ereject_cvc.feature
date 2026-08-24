# language: zh-CN
功能: E2E 拒帧行为后果 — 总线损坏帧不被采信 (bsw_e2ereject_cvc)

  对应 docs/bsw-test-analysis.md「扩展优先级与建议顺序」高优先级 3 的
  E2E 拒帧行为后果（sil_009 同族）。运行在 SIL Docker 栈（7 个真实 vECU
  共享宿主机 vcan0）上，通过 /api/test/bsw/e2ereject/cvc 做纯总线真端到端：

    - corrupt：向插桩 CVC 的一条 E2E 保护 RX 消息（默认 RZC→CVC 的
      Motor_Status / CAN 0x300，dataId=14）注入损坏帧（dataid / crc /
      replay / seq 四种破坏方式，分别命中 E2E_Check 的 DataId / CRC /
      REPEATED / WRONG_SEQ 分支），断言损坏帧不被采信且 CVC 自身发出的帧
      属性/行为不变（CVC_Heartbeat / Vehicle_State 的 e2e/dataIdOk/crcValid）
    - escalate：sil_009 同族 —— 停止 CVC 消除有效 0x100 后，注入损坏的
      Vehicle_State（0x100, dataId=5），断言 RZC 拒掉每一帧损坏扭矩
      （电机 TorqueEcho/RPM 不变、行为不变），持续损坏升级为 Dem 确认的
      DTC 0xE601 广播（CAN 0x500, ecu_source=3），然后恢复 CVC

  被测链路（全真实，无 mock）：
    [损坏帧 → vcan0 → CanIf_RxIndication → PduR → Com_RxIndication
     → E2E_Check → E2E_SMCheck → 拒帧/影子清零/…]

  背景:
    假如存在:
      """
      BswE2ERejectSetup: {
        phases: []
      }
      """

  规则: 真端到端 — RZC 收路径持续损坏升级（sil_009 同族）

    场景: 持续损坏 0x100 → RZC 拒帧且 Dem 升级 DTC 0xE601，行为不变
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "escalate", "count": 16, "intervalMs": 100,
            "observeMs": 5000, "restartCvc": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        dtcBroadcast: true
        dtcCode: 58881
        dtcEcuSource: 3
        rzcHeartbeatValid: true
        motorUnchanged: true
        motorTorqueMax: 0
        motorSpeedMax: 0
      }
      """
      那么response should be:
      """
      body.json.results[0].state.corruptOnBus >= 1 = true and body.json.results[0].state.dtcFrames >= 1 = true
      """

  规则: 真端到端 — CVC 收路径拒帧（损坏帧不被采信，行为不变）

    corrupt 每次 POST 向 CVC 的 E2E 保护 RX 消息注入损坏帧并观测行为不变。

    场景: 损坏 DataId（+CRC）帧被拒，CVC 行为不变（E2E_Check DataId 分支）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Motor_Current", "mode": "dataid",
            "count": 12, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """
      那么response should be:
      """
      body.json.results[0].state.injectedOnBus >= 1 = true
      """

    场景: 损坏 CRC 帧被拒，CVC 行为不变（E2E_Check CRC 分支）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Motor_Current", "mode": "crc",
            "count": 12, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """
      那么response should be:
      """
      body.json.results[0].state.injectedOnBus >= 1 = true
      """

    场景: alive 重放帧被拒，CVC 行为不变（E2E_STATUS_REPEATED 分支）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Motor_Current", "mode": "replay",
            "count": 12, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """

    场景: alive 跳变帧被拒，CVC 行为不变（E2E_STATUS_WRONG_SEQ 分支）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Motor_Current", "mode": "seq",
            "count": 12, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """

    场景: FZC 心跳损坏被拒并经 Dem 上报/恢复，CVC 行为不变（E2E→Dem 分支）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "FZC_Heartbeat", "mode": "dataid",
            "count": 120, "intervalMs": 5 },
          { "op": "corrupt", "target": "RZC_Heartbeat", "mode": "dataid",
            "count": 120, "intervalMs": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """
      那么response should be:
      """
      body.json.results[0].state.injectedOnBus >= 1 = true
      """

    场景: Lidar_Distance（0x220, dataId=13, SM 窗口 invalid=10）损坏被拒，行为不变
      与 Motor_Current 不同，Lidar_Distance 的 `E2eSmWindowInvalid=10`（DBC
      30ms 周期），需连续 ≥10 坏帧才锁存 INVALID——补充覆盖不同 SM 窗口
      配置下的拒帧（对应 `test_SM_*` 状态机的真实运行）。
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Lidar_Distance", "mode": "dataid",
            "count": 12, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        behaviourUnchanged: true
        cvCheartbeatValid: true
        vehicleStateValid: true
      }
      """
      那么response should be:
      """
      body.json.results[0].state.injectedOnBus >= 1 = true
      """

  规则: 真端到端 — fail-closed（目标消息无 E2E 保护 → 拒绝并说明）

    场景: 对无 E2E 保护的消息请求损坏（fail-closed，不崩溃）
      当POST "/api/test/bsw/e2ereject/cvc":
      """
      {
        "phases": [
          { "op": "corrupt", "target": "Body_Control_Cmd", "mode": "crc",
            "count": 4, "intervalMs": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: false
        busUp: true
      }
      """

