# language: zh-CN
功能: RZC 本地安全监控 (Swc_RzcSafety)

  Swc_RzcSafety_MainFunction 的端到端测试：TPS3823 看门狗 WDI 四条件
  门控喂狗、故障聚合位掩码（过流/过温/方向/堵转/电池/自检）、CAN 丢失
  检测（bus-off / 200ms 静默 / 500ms 错误警告 / 锁存）、CAN 丢失时电机
  禁用与安全状态发布、WATCHDOG_FAIL 边沿 DTC 上报，以及重复 Init 复位。

  背景:
    假如存在:
      """
      RzcSafetySetup: {
        phases: []
      }
      """

  规则: 初始化守卫与健康喂狗

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "skipInit": true, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 0
        dioWrites: 0
        demWatchdog: -1
        demWatchdogCount: 0
      }
      """

    场景: 健康状态喂狗且 WDI 电平翻转
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 3, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 3
        wdiLevel: 1
        demWatchdog: -1
        demWatchdogCount: 0
      }
      """

    场景: 重复 Init 后继续喂狗（幂等复位）
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overcurrent": 1 },
          { "reinit": true, "cycles": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 1
        wdiLevel: 1
      }
      """

  规则: 故障聚合位掩码

    场景: 过流故障置位 OVERCURRENT 位并进入 FAULT
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overcurrent": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 17
        safetyStatus: 2
        wdiWrites: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: 过温故障置位 OVERTEMP 位
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overtemp": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 18
        safetyStatus: 2
        wdiWrites: 0
      }
      """

    场景: 方向故障置位 DIRECTION 位
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "directionFault": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 20
        safetyStatus: 2
        wdiWrites: 0
      }
      """

    场景: 堵转故障置位 STALL 位且状态降级
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "stallFault": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 128
        safetyStatus: 1
        wdiWrites: 1
        demWatchdog: -1
      }
      """

    场景: 电池故障置位 BATTERY 位且状态降级
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "batteryFault": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 64
        safetyStatus: 1
        wdiWrites: 1
        demWatchdog: -1
      }
      """

    场景: 堵转与电池同时故障聚合位掩码并保持喂狗
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "stallFault": 1, "batteryFault": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 192
        safetyStatus: 1
        wdiWrites: 1
        demWatchdog: -1
      }
      """

    场景: 自检失败置位 SELF_TEST 位并抑制喂狗
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "selfTestResult": 0, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 48
        safetyStatus: 1
        wdiWrites: 0
        demWatchdog: 1
      }
      """

    场景: 全部故障同时置位聚合全部位
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overcurrent": 1, "overtemp": 1,
            "directionFault": 1, "stallFault": 1, "batteryFault": 1,
            "selfTestResult": 0, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 247
        safetyStatus: 2
        wdiWrites: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

  规则: CAN 丢失检测与电机禁用

    场景: CAN 静默 19 周期不触发丢失
      当POST "/api/test/asw/rzc/safety":
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
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 19
        demWatchdog: -1
      }
      """

    场景: CAN 静默满 20 周期触发丢失并禁用电机
      当POST "/api/test/asw/rzc/safety":
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
        faultMask: 24
        safetyStatus: 2
        wdiWrites: 19
        dioCh5: 0
        dioCh6: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: NotifyCanRx 每周期复位静默计数器
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 20, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 20
        demWatchdog: -1
      }
      """

    场景: 静默计数被 NotifyCanRx 中断后重新累计
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 19 },
          { "cycles": 19, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 38
        demWatchdog: -1
      }
      """

    场景: CAN 错误警告 49 周期不触发丢失
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 49, "canErrorState": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 49
        demWatchdog: -1
      }
      """

    场景: CAN 错误警告满 50 周期触发丢失
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 50, "canErrorState": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 24
        safetyStatus: 2
        wdiWrites: 49
        dioCh5: 0
        dioCh6: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: 错误警告计数在离开警告态后清零
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 49, "canErrorState": 1, "notifyCanRx": true },
          { "cycles": 1, "canErrorState": 0, "notifyCanRx": true },
          { "cycles": 49, "canErrorState": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 99
        demWatchdog: -1
      }
      """

    场景: CAN bus-off 立即触发丢失并禁用电机
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "canErrorState": 2, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 24
        safetyStatus: 2
        wdiWrites: 0
        dioCh5: 0
        dioCh6: 0
        demWatchdog: 1
      }
      """

  规则: CAN 丢失锁存

    场景: CAN 丢失锁存后即使恢复也保持禁用
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 20 },
          { "cycles": 5, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 24
        safetyStatus: 2
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: 重新 Init 清除 CAN 丢失锁存
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 20 },
          { "reinit": true, "cycles": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 20
        wdiLevel: 1
        demWatchdog: 1
      }
      """

  规则: 看门狗喂狗门控与 DTC 上报

    场景: 车辆 SHUTDOWN 状态抑制喂狗并上报 WATCHDOG_FAIL
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 5, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 16
        safetyStatus: 0
        wdiWrites: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: 自检失败抑制喂狗
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "selfTestResult": 0, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 48
        safetyStatus: 1
        wdiWrites: 0
        demWatchdog: 1
      }
      """

    场景: WATCHDOG_FAIL 仅在故障边沿上报一次
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 5, "overcurrent": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 17
        safetyStatus: 2
        wdiWrites: 0
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

    场景: 故障清除后恢复喂狗（边沿报告保持一次）
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overcurrent": 1, "notifyCanRx": true },
          { "cycles": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultMask: 0
        safetyStatus: 0
        wdiWrites: 1
        wdiLevel: 1
        demWatchdog: 1
        demWatchdogCount: 1
      }
      """

  规则: 安全状态发布

    场景: 无故障发布 OK 状态
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        safetyStatus: 0
      }
      """

    场景: 非关键故障发布 DEGRADED 状态
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "batteryFault": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        safetyStatus: 1
      }
      """

    场景: E-stop 激活发布 FAULT 状态
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "estopActive": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 0
        safetyStatus: 2
        wdiWrites: 1
        demWatchdog: -1
      }
      """

    场景: 关键故障发布 FAULT 状态
      当POST "/api/test/asw/rzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "overcurrent": 1, "notifyCanRx": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        safetyStatus: 2
      }
      """
