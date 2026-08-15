# language: zh-CN
功能: CVC 紧急停止 (Swc_EStop)

  场景: 未初始化时主函数不动作
    假如存在:
      """
      CvcEStopSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 3, "pin": 1, "skipInit": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 0
      rteEstopActive: 0
      demReportCount: 0
      rteWriteCount: 0
      broadcastActive: 0
    }
    """

  场景: 按钮释放 (LOW) 保持未激活
    假如存在:
      """
      CvcEStopSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 3, "pin": 0 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 0
      rteEstopActive: 0
      demReportCount: 0
      rteWriteCount: 0
      broadcastActive: 0
    }
    """

  场景: 按钮按下 (HIGH) 经消抖后锁存激活
    假如存在:
      """
      CvcEStopSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 1, "pin": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 1
      rteEstopActive: 1
      demEventId: 7
      demEventStatus: 1
      demReportCount: 2
      rteWriteCount: 2
      broadcastActive: 1
      broadcastSource: 1
    }
    """

  场景: 读取失败触发失效保护激活
    假如存在:
      """
      CvcEStopSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 1, "pin": 0, "readFail": true }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 1
      rteEstopActive: 1
      demEventId: 7
      demReportCount: 2
      broadcastActive: 1
    }
    """

  场景: 释放按钮后锁存保持激活
    假如存在:
      """
      CvcEStopSetup: {
        phases: [
          { cycles: 1 pin: 1 }
        ]
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 3, "pin": 0 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 1
      rteEstopActive: 1
      demEventId: 7
      broadcastActive: 1
    }
    """

  场景: 锁存后每周期循环刷新广播与 DTC
    假如存在:
      """
      CvcEStopSetup: {
        phases: []
      }
      """
    当POST "/api/test/asw/cvc/estop":
    """
    {
      "phases": [
        { "cycles": 5, "pin": 1 }
      ]
    }
    """
    那么response should be:
    """
    body.json: {
      isActive: 1
      rteEstopActive: 1
      demEventId: 7
      demReportCount: 6
      rteWriteCount: 6
      broadcastActive: 1
      broadcastSource: 1
    }
    """
