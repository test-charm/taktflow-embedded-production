# language: zh-CN
功能: CVC pedal 到 Torque_Request

  场景: RUN 状态下相同踏板输入生成扭矩请求
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 40,
      "sensor2Pct": 40,
      "vehicleState": "RUN",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 40
        pedalFaultName: NONE
        torqueDirection: 1
        comSignals: {
          torqueRequestCommandPct: 40
        }
      }
    }
    """

  场景: RUN 状态下踏板不一致会将扭矩清零
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 20,
      "sensor2Pct": 80,
      "vehicleState": "RUN",
      "cycles": 2
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: PLAUSIBILITY
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: DEGRADED 状态下满踏板会被限制到 75%
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 100,
      "sensor2Pct": 100,
      "vehicleState": "DEGRADED",
      "cycles": 200
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 75
        pedalFaultName: NONE
        torqueDirection: 1
        comSignals: {
          torqueRequestCommandPct: 75
        }
      }
    }
    """
