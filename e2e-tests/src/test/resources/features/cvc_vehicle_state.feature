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
