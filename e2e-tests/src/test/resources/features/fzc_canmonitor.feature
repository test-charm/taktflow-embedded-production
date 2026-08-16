# language: zh-CN
功能: FZC CAN 总线监控 (Swc_FzcCanMonitor)

  Swc_FzcCanMonitor FZC CAN 总线健康监控 SWC 的端到端测试：总线丢失检测
  （bus-off 立即触发、200ms 静默、错误警告持续 500ms）、启动宽限期
  （500 周期内抑制监控）、安全状态锁存（NO recovery — 断电前保持安全状态）、
  NotifyRx 静默计数器复位。驱动真实 Swc_FzcCanMonitor.c 生产代码。

  背景:
    假如存在:
      """
      FzcCanMonitorSetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_FzcCanMonitor_Init / Check

    Init 复位状态为 OK、清零静默/错误警告/宽限计数并置位初始化标志。未初始化
    时 Check 直接返回不动作。

    场景: 初始化后默认状态为 OK
      当POST "/api/test/asw/fzc/canmonitor":
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
        initialized: 1
        silenceCount: 0
        graceCycles: 1
        errWarnCount: 0
        safeLatched: 0
        brakeCmd: 0
        steerCmd: 0
        buzzerPattern: 0
        dtcReported: 0
      }
      """

    场景: 未初始化时 Check 不动作
      当POST "/api/test/asw/fzc/canmonitor":
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
        initialized: 0
        graceCycles: 0
        silenceCount: 0
        safeLatched: 0
      }
      """

  规则: 启动宽限期 — Swc_FzcCanMonitor_Check（grace period）

    启动后前 500 周期（5 秒）为宽限期：允许其他 ECU 启动并开始发送 CAN 帧，
    此间抑制所有故障检测（bus-off/静默/错误警告均不触发），每周期复位静默
    计数器。宽限期结束后监控生效。

    场景: 宽限期内 bus-off 被抑制
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "canMode": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        safeLatched: 0
        graceCycles: 1
        brakeCmd: 0
        dtcReported: 0
      }
      """

    场景: 宽限期结束后监控生效（bus-off 立即触发）
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 500 },
          { "cycles": 1, "canMode": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        safeLatched: 1
        graceCycles: 500
        brakeCmd: 100
        dtcReported: 1
      }
      """

  规则: 总线关闭检测 — Swc_FzcCanMonitor_Check（bus-off）

    宽限期后 Can_GetControllerMode(0) 返回 CAN_CS_STOPPED → 状态置 BUS_OFF，
    应用安全状态（制动 100%、转向居中、连续蜂鸣、上报 DTC）并锁存。此后
    即使总线恢复，安全状态也保持（NO recovery — 断电前不解除）。

    场景: 总线关闭立即应用安全状态
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "canMode": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        safeLatched: 1
        brakeCmd: 100
        steerCmd: 0
        buzzerPattern: 4
        dtcReported: 1
      }
      """

    场景: 总线关闭锁存后总线恢复不解除安全状态
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 },
            { cycles: 1, canMode: 1 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "canMode": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        safeLatched: 1
        brakeCmd: 100
        steerCmd: 0
        buzzerPattern: 4
        dtcReported: 2
      }
      """

  规则: 静默检测 — Swc_FzcCanMonitor_Check（silence）

    宽限期后无 NotifyRx 通知且连续静默达 20 周期（200ms）→ 状态置 SILENCE，
    应用安全状态并锁存。新消息到达（NotifyRx）会复位静默计数器。

    场景: 静默不足 20 周期不触发
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 19 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        silenceCount: 19
        safeLatched: 0
        brakeCmd: 0
        dtcReported: 0
      }
      """

    场景: 静默恰达 20 周期触发安全状态
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        silenceCount: 20
        safeLatched: 1
        brakeCmd: 100
        buzzerPattern: 4
        dtcReported: 1
      }
      """

    场景: NotifyRx 到达复位静默计数器
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 5 },
          { "cycles": 5, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        silenceCount: 1
        safeLatched: 0
        brakeCmd: 0
        dtcReported: 0
      }
      """

  规则: 错误警告检测 — Swc_FzcCanMonitor_Check（error warning）

    宽限期后 TEC 或 REC 任一 ≥ 96 且持续 50 周期（500ms）→ 状态置
    ERROR_WARNING，应用安全状态并锁存。计数清零（两计数器均 < 96）会复位
    错误警告计数器并重新计时。

    场景: 错误警告持续不足 50 周期不触发
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 49, "tec": 96, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        errWarnCount: 49
        safeLatched: 0
        brakeCmd: 0
        dtcReported: 0
      }
      """

    场景: 错误警告持续恰达 50 周期触发安全状态
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 50, "tec": 96, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 3
        errWarnCount: 50
        safeLatched: 1
        brakeCmd: 100
        buzzerPattern: 4
        dtcReported: 1
      }
      """

    场景: REC 单独达到阈值同样触发错误警告
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 50, "rec": 96, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 3
        errWarnCount: 50
        safeLatched: 1
        brakeCmd: 100
        dtcReported: 1
      }
      """

    场景: TEC 与 REC 均低于阈值时错误警告计数复位
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 1, "tec": 95, "rec": 95, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        errWarnCount: 0
        safeLatched: 0
        brakeCmd: 0
        dtcReported: 0
      }
      """

    场景: 错误警告计数清零后重新计时
      假如存在:
        """
        FzcCanMonitorSetup: {
          phases: [
            { cycles: 500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/canmonitor":
      """
      {
        "phases": [
          { "cycles": 10, "tec": 96, "notifyRx": true },
          { "cycles": 1, "tec": 0, "rec": 0, "notifyRx": true },
          { "cycles": 49, "tec": 96, "notifyRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        errWarnCount: 49
        safeLatched: 0
        brakeCmd: 0
        dtcReported: 0
      }
      """
