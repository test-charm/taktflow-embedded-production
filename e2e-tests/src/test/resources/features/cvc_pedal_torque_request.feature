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

  场景: LIMP 状态下踏板扭矩被限制到 30%
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50,
      "vehicleState": "LIMP",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 30
        pedalFaultName: NONE
        torqueDirection: 1
        comSignals: {
          torqueRequestCommandPct: 30
        }
      }
    }
    """

  场景: SAFE_STOP 状态下踏板扭矩被清零
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 80,
      "sensor2Pct": 80,
      "vehicleState": "SAFE_STOP",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: NONE
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: SHUTDOWN 状态下踏板扭矩被清零
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 60,
      "sensor2Pct": 60,
      "vehicleState": "SHUTDOWN",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: NONE
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: INIT 状态下踏板扭矩被清零
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 40,
      "sensor2Pct": 40,
      "vehicleState": "INIT",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: NONE
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: 传感器1 SPI 故障导致扭矩清零并上报 SENSOR1_FAIL
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50,
      "vehicleState": "RUN",
      "cycles": 100,
      "spiFaultSensor": 0
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: SENSOR1_FAIL
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: 传感器2 SPI 故障导致扭矩清零并上报 SENSOR2_FAIL
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 50,
      "sensor2Pct": 50,
      "vehicleState": "RUN",
      "cycles": 100,
      "spiFaultSensor": 1
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: SENSOR2_FAIL
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """

  场景: RUN 状态下低踏板在扭矩死区内输出为零
    当POST "/api/test/asw/cvc/pedal-torque":
    """
    {
      "sensor1Pct": 3,
      "sensor2Pct": 3,
      "vehicleState": "RUN",
      "cycles": 100
    }
    """
    那么response should be:
    """
    body.json: {
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: NONE
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """
