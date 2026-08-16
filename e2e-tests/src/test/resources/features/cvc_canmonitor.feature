# language: zh-CN
功能: CVC CAN 总线监控 (Swc_CanMonitor)

  Swc_CanMonitor CAN 总线健康监控 SWC 的端到端测试：总线丢失检测（bus-off
  立即 SAFE_STOP、200ms 静默触发、错误警告持续 500ms 触发）、总线恢复
  （10s 窗口内最多 3 次恢复尝试，第 4 次失败 → SHUTDOWN 终态短路）。

  背景:
    假如存在:
      """
      CvcCanMonitorSetup: {
        phases: []
      }
      """

  规则: 总线丢失检测 — Swc_CanMonitor_Check（bus-off）

    总线关闭是最高优先级的故障：只要 isBusOff 输入为 TRUE，立即把内部状态
    置为 BUSOFF 并返回 SAFE_STOP，不等待静默或错误警告判定。

    场景: 初始化后默认状态为 OK
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        initialized: 1
        lastRxCount: 0
        errorWarnActive: 0
        recoveryAttempts: 0
      }
      """

    场景: 未初始化时 Check 返回 OK 不动作
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        initialized: 0
        recoveryAttempts: 0
      }
      """

    场景: 总线关闭立即触发 SAFE_STOP
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "isBusOff": true, "timeStartMs": 1000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        checkResult: 4
        initialized: 1
      }
      """

  规则: 200ms 静默检测 — Swc_CanMonitor_Check（silence）

    当 CAN 总线上不再有新消息（rxMsgCount 不递增）且持续超过 200ms 时判定
    CAN 静默 → 状态置 SILENCE 并返回 SAFE_STOP。新消息到达会重置静默定时器。

    场景: 静默不足 200ms 不触发
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 2, "rxMsgCount": 5, "timeStartMs": 0, "timeStepMs": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        lastRxCount: 5
        lastRxTimeMs: 0
      }
      """

    场景: 静默恰达 200ms 触发 SAFE_STOP
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 3, "rxMsgCount": 5, "timeStartMs": 0, "timeStepMs": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        checkResult: 4
        lastRxCount: 5
        lastRxTimeMs: 0
      }
      """

    场景: 消息持续到达重置静默定时器
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 5, "rxMsgCount": 5, "rxInc": true, "timeStartMs": 0, "timeStepMs": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        lastRxCount: 9
        lastRxTimeMs: 400
      }
      """

  规则: 错误警告检测 — Swc_CanMonitor_Check（error warning）

    当 CAN 错误警告标志持续为 TRUE 超过 500ms 时判定总线退化 → 状态置
    ERROR_WARNING 并返回 SAFE_STOP。错误警告在 500ms 内清除会重置计时。

    场景: 错误警告持续不足 500ms 不触发
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 5, "rxInc": true, "errorWarning": true, "timeStartMs": 0, "timeStepMs": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        errorWarnActive: 1
        errorWarnStartMs: 0
      }
      """

    场景: 错误警告持续 500ms 触发 SAFE_STOP
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 6, "rxInc": true, "errorWarning": true, "timeStartMs": 0, "timeStepMs": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 3
        checkResult: 4
        errorWarnActive: 1
        errorWarnStartMs: 0
      }
      """

    场景: 错误警告清除后重新计时
      假如存在:
        """
        CvcCanMonitorSetup: {
          phases: [
            { cycles: 1, rxInc: true, errorWarning: true, timeStartMs: 0 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "rxInc": true, "errorWarning": false, "timeStartMs": 300 },
          { "cycles": 1, "rxInc": true, "errorWarning": true, "timeStartMs": 400 },
          { "cycles": 1, "rxInc": true, "errorWarning": true, "timeStartMs": 899 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        checkResult: 0
        errorWarnActive: 1
        errorWarnStartMs: 400
      }
      """

  规则: 总线恢复 — Swc_CanMonitor_Recovery

    Recovery 在 10s 窗口内最多允许 3 次恢复尝试，成功则把状态复位为 OK 并
    清除错误警告追踪；第 4 次失败进入 SHUTDOWN 终态。窗口过期后计数器重置。
    SHUTDOWN 终态下 Check/Recovery 均短路。

    场景: 未初始化时恢复返回 E_NOT_OK
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "recovery": true, "recoveryTimeMs": 1000, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        recoveryResult: 1
        status: 0
        initialized: 0
        recoveryAttempts: 0
      }
      """

    场景: 总线关闭后恢复成功复位 OK
      假如存在:
        """
        CvcCanMonitorSetup: {
          phases: [
            { cycles: 1, isBusOff: true, timeStartMs: 1000 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "recovery": true, "recoveryTimeMs": 1000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        recoveryResult: 0
        status: 0
        recoveryAttempts: 1
        recoveryWindowStartMs: 1000
      }
      """

    场景: 3 次恢复失败后第 4 次触发 SHUTDOWN
      假如存在:
        """
        CvcCanMonitorSetup: {
          phases: [
            { cycles: 1, isBusOff: true, timeStartMs: 1000 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "recovery": true, "recoveryTimeMs": 1000 },
          { "cycles": 1, "isBusOff": true, "timeStartMs": 2000 },
          { "recovery": true, "recoveryTimeMs": 2000 },
          { "cycles": 1, "isBusOff": true, "timeStartMs": 3000 },
          { "recovery": true, "recoveryTimeMs": 3000 },
          { "cycles": 1, "isBusOff": true, "timeStartMs": 4000 },
          { "recovery": true, "recoveryTimeMs": 4000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        recoveryResult: 1
        status: 5
        recoveryAttempts: 4
      }
      """

    场景: 恢复窗口过期后计数器重置
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "recovery": true, "recoveryTimeMs": 0 },
          { "recovery": true, "recoveryTimeMs": 1000 },
          { "recovery": true, "recoveryTimeMs": 2000 },
          { "recovery": true, "recoveryTimeMs": 15000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        recoveryResult: 0
        status: 0
        recoveryAttempts: 1
        recoveryWindowStartMs: 15000
      }
      """

    场景: SHUTDOWN 终态下 Check 和 Recovery 均短路
      假如存在:
        """
        CvcCanMonitorSetup: {
          phases: [
            { cycles: 1, isBusOff: true, timeStartMs: 1000 },
            { recovery: true, recoveryTimeMs: 1000 },
            { cycles: 1, isBusOff: true, timeStartMs: 2000 },
            { recovery: true, recoveryTimeMs: 2000 },
            { cycles: 1, isBusOff: true, timeStartMs: 3000 },
            { recovery: true, recoveryTimeMs: 3000 },
            { cycles: 1, isBusOff: true, timeStartMs: 4000 },
            { recovery: true, recoveryTimeMs: 4000 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "isBusOff": false, "timeStartMs": 4000 },
          { "recovery": true, "recoveryTimeMs": 4000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 5
        checkResult: 5
        recoveryResult: 1
        recoveryAttempts: 4
      }
      """
