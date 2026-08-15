# language: zh-CN
功能: CVC pedal 到 Torque_Request

  场景: RUN 状态下相同踏板输入生成扭矩请求
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 40,
      "sensor2Pct": 40
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 40
      pedalFaultName: NONE
      torqueDirection: 1
      comSignals: {
        torqueRequestCommandPct: 40
      }
    }
    """

  场景: RUN 状态下踏板不一致会将扭矩清零
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 3
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 20,
      "sensor2Pct": 80
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: PLAUSIBILITY
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: DEGRADED 状态下满踏板会被限制到 75%
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: DEGRADED
        cycles: 200
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 100,
      "sensor2Pct": 100
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 75
      pedalFaultName: NONE
      torqueDirection: 1
      comSignals: {
        torqueRequestCommandPct: 75
      }
    }
    """

  场景: LIMP 状态下踏板扭矩被限制到 30%
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: LIMP
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 30
      pedalFaultName: NONE
      torqueDirection: 1
      comSignals: {
        torqueRequestCommandPct: 30
      }
    }
    """

  场景: SAFE_STOP 状态下踏板扭矩被清零
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: SAFE_STOP
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 80,
      "sensor2Pct": 80
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: NONE
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: SHUTDOWN 状态下踏板扭矩被清零
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: SHUTDOWN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 60,
      "sensor2Pct": 60
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: NONE
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: INIT 状态下踏板扭矩被清零
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: INIT
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 40,
      "sensor2Pct": 40
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: NONE
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: 传感器1 SPI 故障导致扭矩清零并上报 SENSOR1_FAIL
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
        spiFaultSensor: 0
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: SENSOR1_FAIL
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: 传感器2 SPI 故障导致扭矩清零并上报 SENSOR2_FAIL
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
        spiFaultSensor: 1
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: SENSOR2_FAIL
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: RUN 状态下低踏板在扭矩死区内输出为零
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 3,
      "sensor2Pct": 3
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: NONE
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: 合理性故障后零扭矩锁存在匹配踏板后恢复
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 3
        recoverCycles: 70
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 20,
      "sensor2Pct": 80,
      "recoverSensor1Pct": 20,
      "recoverSensor2Pct": 20
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 10
      pedalFaultName: NONE
      torqueDirection: 1
      comSignals: {
        torqueRequestCommandPct: 10
      }
    }
    """

  场景: 传感器持续卡滞触发 STUCK 故障并清零扭矩
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 102
        ditherAmplitude: 0
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50
    }
    """
    那么response should be:
    """
    body.json: {
      torqueRequestPct: 0
      pedalFaultName: STUCK
      torqueDirection: 0
      comSignals: {
        torqueRequestCommandPct: 0
      }
    }
    """

  场景: GetPosition 报告踏板位置百分比
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
      """
      {
        "sensor1Pct": 40,
        "sensor2Pct": 40,
        "getPosition": true
      }
      """
    那么response should be:
      """
      body.json: {
        getPosition: 40
        pedalFaultName: NONE
      }
      """

  场景: BridgeRxToRte 将制动与电机故障桥接到 RTE
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
      """
      {
        "sensor1Pct": 40,
        "sensor2Pct": 40,
        "bridgeRx": true,
        "rxBrakeFault": 1,
        "rxMotorCutoff": 1,
        "rxBattery": 0,
        "rxSteeringFault": 1,
        "rxMotorFault": 1,
        "rxScRelay": 0
      }
      """
    那么response should be:
      """
      body.json: {
        bridged: {
          brakeFault: 1
          motorCutoff: 1
          batteryStatus: 0
          steeringFault: 1
          motorFaultRzc: 1
          scRelayEnergized: 0
        }
      }
      """

  场景: BridgeRxToRte 心跳存活计数器桥接
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: RUN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
      """
      {
        "sensor1Pct": 40,
        "sensor2Pct": 40,
        "bridgeRx": true,
        "rxFzcAlive": 5,
        "rxzAlive": 7
      }
      """
    那么response should be:
      """
      body.json: {
        bridged: {
          brakeFault: 0
          motorCutoff: 0
          batteryStatus: 0
        }
      }
      """

  场景: SAFE_STOP 状态发送最大制动命令
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: SAFE_STOP
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
      """
      {
        "sensor1Pct": 100,
        "sensor2Pct": 100
      }
      """
    那么response should be:
      """
      body.json: {
        torqueRequestPct: 0
        brakeCommand: 100
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
      """

  场景: SHUTDOWN 状态发送最大制动命令
    假如存在:
      """
      CvcPedalSetup: {
        vehicleState: SHUTDOWN
        cycles: 100
      }
      """
    当POST "/api/test/asw/cvc/pedal-torque":
      """
      {
        "sensor1Pct": 100,
        "sensor2Pct": 100
      }
      """
    那么response should be:
      """
      body.json: {
        torqueRequestPct: 0
        brakeCommand: 100
      }
      """
