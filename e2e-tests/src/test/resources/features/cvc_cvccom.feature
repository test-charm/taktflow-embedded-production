# language: zh-CN
功能: CVC CAN 通信 (Swc_CvcCom)

  Swc_CvcCom_TransmitSchedule 与 Swc_CvcCom_BridgeRxToRte 的端到端测试。

  背景:
    假如存在:
      """
      CvcCvcComSetup: {
        phases: []
      }
      """

  规则: TX 调度 — Swc_CvcCom_TransmitSchedule

    Swc_CvcCom_TransmitSchedule 负责 CVC 的 CAN 发送调度：心跳（ECU_ID +
    OperatingMode）、0x100 Vehicle_State（faultMask 8 位组合 + 扭矩钳位）、
    转向/制动命令（SAFE_STOP 起最大制动）、E-Stop 广播桥、Body_Control_Cmd、
    Torque_Request 桥。以下场景驱动该函数的 TX 全链路。

    场景: 未初始化时 TX 不动作
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "rxMotorCutoff": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatEcuId: 0
        heartbeatMode: 0
        vehicleStateMode: 0
        faultMask: 0
        brakeForceCmd: 0
        estopBroadcastActive: 0
        torqueCommandPct: 0
      }
      """

    场景: RUN 状态下健康 TX 全链路
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatEcuId: 1
        heartbeatMode: 1
        vehicleStateMode: 1
        faultMask: 0
        torqueLimit: 0
        steerAngleCmd: 0
        brakeForceCmd: 0
        estopBroadcastActive: 0
        estopBroadcastSource: 1
        torqueCommandPct: 0
        bodyHeadlight: 0
        bodyTaillight: 0
        bodyHazard: 0
        bodyTurnSignal: 0
        bodyDoorLock: 0
        rteFaultMask: 0
      }
      """

    场景: E-Stop 激活时广播且故障掩码置位 0x01
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "estop": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 1
        estopBroadcastActive: 1
        estopBroadcastSource: 1
        rteFaultMask: 1
      }
      """

    场景: SC 继电器切断置位故障掩码 0x02
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 2
        rteFaultMask: 2
      }
      """

    场景: 电机切断与制动故障置位 0x04|0x08
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "motorCutoff": 1, "brakeFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 12
        rteFaultMask: 12
      }
      """

    场景: 转向与踏板故障置位 0x10|0x20
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "steerFault": 1, "pedalFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 48
        rteFaultMask: 48
      }
      """

    场景: FZC/RZC 通信超时置位 0x40|0x80
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "fzcComm": 1, "rzcComm": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 192
        rteFaultMask: 192
      }
      """

    场景: 全部故障置位故障掩码 0xFF
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "estop": 1, "relayKill": 0, "motorCutoff": 1, "brakeFault": 1, "steerFault": 1, "pedalFault": 1, "fzcComm": 1, "rzcComm": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 255
        rteFaultMask: 255
      }
      """

    场景: 扭矩超限被钳位到 100%
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "torque": 150 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueLimit: 100
        torqueCommandPct: 100
      }
      """

    场景: 扭矩在 0-100 范围内原样透传
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 1, "relayKill": 1, "torque": 60 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueLimit: 60
        torqueCommandPct: 60
      }
      """

    场景: SAFE_STOP 状态制动指令覆盖为最大制动
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 4, "relayKill": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatMode: 4
        vehicleStateMode: 4
        brakeForceCmd: 100
        steerAngleCmd: 0
      }
      """

    场景: SHUTDOWN 状态同样覆盖为最大制动
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 5, "relayKill": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatMode: 5
        vehicleStateMode: 5
        brakeForceCmd: 100
        steerAngleCmd: 0
      }
      """

    场景: DEGRADED 状态心跳与状态信号透传模式
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 2, "relayKill": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatMode: 2
        vehicleStateMode: 2
        brakeForceCmd: 0
      }
      """

    场景: 多阶段脚本中车辆状态由给定阶段驱动
      假如存在:
        """
        CvcCvcComSetup: {
          phases: [
            { cycles: 1 vehicleState: 1 relayKill: 1 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 4, "relayKill": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        heartbeatMode: 4
        vehicleStateMode: 4
        brakeForceCmd: 100
      }
      """

  规则: RX 桥接 — Swc_CvcCom_BridgeRxToRte

    Swc_CvcCom_BridgeRxToRte 负责将 Com RX 影子（制动故障双源、电机切断、
    SC 继电器、电池、转向/电机故障、FZC/RZC 心跳存活计数器）桥接到 RTE，
    供 Swc_VehicleState 消费。以下场景驱动该函数的 RX 桥接全链路。

    场景: 未初始化时 RX 桥接不动作
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "bridgeRx": true, "rxMotorCutoff": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteMotorCutoff: 0
      }
      """

    场景: RX 桥接制动故障取周期状态 (0x201)
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "bridgeRx": true, "vehicleState": 1, "relayKill": 1, "rxBrakeEvent": 0, "rxBrakeStatus": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteBrakeFault: 1
      }
      """

    场景: RX 桥接制动故障事件帧优先 (0x210)
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "bridgeRx": true, "vehicleState": 1, "relayKill": 1, "rxBrakeEvent": 1, "rxBrakeStatus": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteBrakeFault: 1
      }
      """

    场景: RX 桥接全部故障信号与心跳存活计数器
      当POST "/api/test/asw/cvc/cvccom":
      """
      {
        "phases": [
          { "cycles": 1, "bridgeRx": true, "vehicleState": 1, "relayKill": 1,
            "rxMotorCutoff": 1, "rxScRelay": 0, "rxBattery": 1,
            "rxSteerFault": 1, "rxMotorFault": 1,
            "rxFzcAlive": 5, "rxRzcAlive": 9 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteMotorCutoff: 1
        rteScRelayKill: 0
        rteBattery: 1
        rteSteerFault: 1
        rteMotorFaultRzc: 1
        rteFzcAlive: 5
        rteRzcAlive: 9
      }
      """
