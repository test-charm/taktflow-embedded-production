# language: zh-CN
功能: CVC pedal 到 Torque_Request

  场景: RUN 状态下相同踏板输入生成扭矩请求
    假如车辆状态为 RUN
    而且执行 100 个周期
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
    假如车辆状态为 RUN
    而且执行 3 个周期
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
    假如车辆状态为 DEGRADED
    而且执行 200 个周期
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
    假如车辆状态为 LIMP
    而且执行 100 个周期
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
    假如车辆状态为 SAFE_STOP
    而且执行 100 个周期
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
    假如车辆状态为 SHUTDOWN
    而且执行 100 个周期
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
    假如车辆状态为 INIT
    而且执行 100 个周期
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
    假如车辆状态为 RUN
    而且执行 100 个周期
    而且SPI故障注入传感器 0
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
    假如车辆状态为 RUN
    而且执行 100 个周期
    而且SPI故障注入传感器 1
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
    假如车辆状态为 RUN
    而且执行 100 个周期
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

  场景: 合理性故障后零扭矩锁存在匹配踏板后恢复
    假如车辆状态为 RUN
    而且执行 3 个周期
    而且恢复阶段执行 70 个周期
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
      outputs: {
        torqueRequestPct: 10
        pedalFaultName: NONE
        torqueDirection: 1
        comSignals: {
          torqueRequestCommandPct: 10
        }
      }
    }
    """

  场景: 传感器持续卡滞触发 STUCK 故障并清零扭矩
    假如车辆状态为 RUN
    而且执行 102 个周期
    而且关闭传感器抖动
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
      outputs: {
        torqueRequestPct: 0
        pedalFaultName: STUCK
        torqueDirection: 0
        comSignals: {
          torqueRequestCommandPct: 0
        }
      }
    }
    """
