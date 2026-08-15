# language: zh-CN
功能: CVC 车辆状态机 (Swc_VehicleState)

  场景: INIT 状态通过自检与心跳进入 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN"
    }
    """

  场景: 无自检通过时保持 INIT
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1005 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: INIT
      stateTrace: "INIT"
    }
    """

  场景: RUN 状态下急停进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "estop": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下踏板故障进入 DEGRADED
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "pedalFault": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: DEGRADED
      bswmMode: DEGRADED
      stateTrace: "INIT,RUN,DEGRADED"
    }
    """

  场景: RUN 状态下电池临界故障进入 LIMP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 0 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: LIMP
      bswmMode: DEGRADED
      dtcNames: BATT_UNDERVOLT
      stateTrace: "INIT,RUN,LIMP"
    }
    """

  场景: RUN 状态下双 CAN 超时进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 55, "fzcComm": 1, "rzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下制动故障进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "brakeFault": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: BRAKE_FAULT_RX
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下转向故障进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "steeringFault": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: STEERING_FAULT_RX
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下电机切断进入 DEGRADED
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "motorCutoff": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: DEGRADED
      bswmMode: DEGRADED
      dtcNames: MOTOR_CUTOFF_RX
      stateTrace: "INIT,RUN,DEGRADED"
    }
    """

  场景: RUN 状态下爬行故障进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 205, "torqueRequest": 60 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: CREEP_FAULT
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: DEGRADED 状态下故障清除回到 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "pedalFault": true },
        { "cycles": 5 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,DEGRADED,RUN"
    }
    """

  场景: SAFE_STOP 恢复回到 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "estop": true },
        { "cycles": 520 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: RUN 状态下 SC 切断进入 SHUTDOWN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "scRelayEnergized": false }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SHUTDOWN
      bswmMode: SHUTDOWN
      stateTrace: "INIT,RUN,SHUTDOWN"
    }
    """

  场景: RUN 状态下双踏板故障进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "pedalFaultDual": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下持续电池临界故障进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 0 },
        { "cycles": 1, "batteryStatus": 0 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: BATT_UNDERVOLT
      stateTrace: "INIT,RUN,LIMP,SAFE_STOP"
    }
    """

  场景: RUN 状态下单侧 CAN 超时进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 55, "fzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      stateTrace: "INIT,RUN,SAFE_STOP"
    }
    """

  场景: RUN 状态下电池告警进入 DEGRADED
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: DEGRADED
      bswmMode: DEGRADED
      stateTrace: "INIT,RUN,DEGRADED"
    }
    """

  场景: 制动故障 Com 新鲜读不一致时保持 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "brakeFault": true, "comBrakeFault": 0 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      dtcNames: ""
      stateTrace: "INIT,RUN"
    }
    """

  场景: Motor_Status PDU 超时进入 DEGRADED
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "motorPduTimedOut": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: DEGRADED
      bswmMode: DEGRADED
      dtcNames: MOTOR_OVERCURRENT
      stateTrace: "INIT,RUN,DEGRADED"
    }
    """

  场景: SAFE_STOP 恢复途中故障再现后仍可恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 350 },
        { "cycles": 1, "estop": true },
        { "cycles": 520 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: DEGRADED 状态下电机切断进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "pedalFault": true },
        { "cycles": 5, "motorCutoff": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: MOTOR_CUTOFF_RX
      stateTrace: "INIT,RUN,DEGRADED,SAFE_STOP"
    }
    """

  场景: LIMP 状态下电机切断进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 0 },
        { "cycles": 5, "motorCutoff": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      dtcNames: "BATT_UNDERVOLT,MOTOR_CUTOFF_RX"
      stateTrace: "INIT,RUN,LIMP,SAFE_STOP"
    }
    """

  场景: FZC 心跳超时阻止 INIT 进入 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1005, "selfTestPass": true, "fzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: INIT
      stateTrace: "INIT"
    }
    """

  场景: RZC 心跳超时阻止 INIT 进入 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1005, "selfTestPass": true, "rzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: INIT
      stateTrace: "INIT"
    }
    """

  场景: 后 INIT 宽限期内 SC 切断保持 RUN
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "scRelayEnergized": false }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN"
    }
    """

  场景: RUN 状态下电池高位临界进入 LIMP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 4 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: LIMP
      bswmMode: DEGRADED
      dtcNames: BATT_UNDERVOLT
      stateTrace: "INIT,RUN,LIMP"
    }
    """

  场景: RUN 状态下电池高位告警进入 DEGRADED
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 3 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: DEGRADED
      bswmMode: DEGRADED
      stateTrace: "INIT,RUN,DEGRADED"
    }
    """

  场景: SAFE_STOP 恢复被电机切断再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "motorCutoff": true },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被 RZC 电机故障再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "motorFaultRzc": true },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被制动故障再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "brakeFault": true },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被转向故障再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "steeringFault": true },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被踏板故障再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "pedalFault": true },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被 SC 切断再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "scRelayEnergized": false },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被 RZC 通信超时再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "rzcComm": 1 },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: SAFE_STOP 恢复被电池临界再现中断后恢复
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "estop": true },
        { "cycles": 310 },
        { "cycles": 1, "batteryStatus": 0 },
        { "cycles": 200 },
        { "cycles": 1005, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,SAFE_STOP,INIT,RUN"
    }
    """

  场景: RUN 状态下再次注入自检通过无效果
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "selfTestPass": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN"
    }
    """

  场景: LIMP 状态下单侧 CAN 超时保持 LIMP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1, "batteryStatus": 0 },
        { "cycles": 55, "fzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: LIMP
      bswmMode: DEGRADED
      dtcNames: BATT_UNDERVOLT
      stateTrace: "INIT,RUN,LIMP"
    }
    """

  场景: 踩踏板时爬行守护不触发
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 205, "pedalPosition": 60, "torqueRequest": 60 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN"
    }
    """

  场景: 电机已转时爬行守护不触发
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 205, "motorSpeed": 60, "torqueRequest": 60 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN"
    }
    """

  场景: DEGRADED 状态下制动故障阻止故障清除
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 5, "pedalFault": true },
        { "cycles": 2, "brakeFault": true },
        { "cycles": 5 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: RUN
      bswmMode: RUN
      stateTrace: "INIT,RUN,DEGRADED,RUN"
    }
    """

  场景: INIT 状态下 SC 切断不触发迁移
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 1005, "scRelayEnergized": false }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: INIT
      stateTrace: "INIT"
    }
    """

  场景: DEGRADED 状态下单侧 CAN 超时进入 SAFE_STOP
    假如存在:
      """
      CvcVehicleStateSetup: {
        phases: [
          { cycles: 1005 selfTestPass: true }
          { cycles: 1005 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/vehicle-state":
    """
    {
      "phases": [
        { "cycles": 60, "pedalFault": true, "fzcComm": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      vehicleState: SAFE_STOP
      bswmMode: SAFE_STOP
      stateTrace: "INIT,RUN,DEGRADED,SAFE_STOP"
    }
    """
