# language: zh-CN
功能: FZC 本地安全监控 (Swc_FzcSafety)

  Swc_FzcSafety FZC 本地安全监控 SWC 的端到端测试：看门狗喂狗（TPS3823 WDI
  翻转，四条件门控）、故障聚合（转向/制动/激光雷达 → 统一掩码）、自检失败
  处理、CAN RX 质量超时（宽限期后 CAN_BUS_OFF）、电机切断（宽限期抑制/结束后
  置位）、安全状态发布（OK/DEGRADED/FAULT）。驱动真实 Swc_FzcSafety.c 生产代码。

  背景:
    假如存在:
      """
      FzcSafetySetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_FzcSafety_Init / MainFunction

    Init 复位看门狗翻转电平、状态为 OK、自检标志为 FALSE、宽限计数为
    FZC_POST_INIT_GRACE_CYCLES（1500）并置位初始化标志。未初始化时
    MainFunction 直接返回不动作。

    场景: 初始化后默认状态为 OK 且看门狗喂狗
      当POST "/api/test/asw/fzc/safety":
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
        graceCounter: 1499
        selfTestDone: 0
        wdiToggle: 1
        faultMask: 0
        safetyStatus: 0
        motorCutoff: 0
        dtcReported: 0
        comQueries: 0
        wdiWrites: 1
      }
      """

    场景: 未初始化时 MainFunction 空转
      当POST "/api/test/asw/fzc/safety":
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
        graceCounter: 0
        faultMask: 0
        safetyStatus: 0
        motorCutoff: 0
        dtcReported: 0
        comQueries: 0
        wdiWrites: 0
      }
      """

  规则: 看门狗喂狗 — Swc_FzcSafety_MainFunction（WDI 翻转）

    无关键故障、车辆非 SHUTDOWN、自检未失败时，看门狗每周期翻转 WDI 引脚
    （PB0）。关键故障（转向/制动）、车辆 SHUTDOWN、自检失败均抑制喂狗并上报
    FZC_DTC_WATCHDOG_FAIL DTC，掩码置 FZC_FAULT_WATCHDOG。

    场景: 无故障时看门狗每周期翻转
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        wdiToggle: 0
        faultMask: 0
        dtcReported: 0
        wdiWrites: 2
      }
      """

    场景: 转向故障抑制看门狗并上报 DTC
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 17
        motorCutoff: 0
        dtcReported: 1
        wdiWrites: 0
      }
      """

    场景: 车辆 SHUTDOWN 抑制看门狗
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "vehicleState": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 16
        safetyStatus: 0
        dtcReported: 1
        wdiWrites: 0
      }
      """

    场景: 自检完成且失败抑制看门狗并置位自检掩码
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "selfTestDone": true, "selfTestResult": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        selfTestDone: 1
        faultMask: 48
        dtcReported: 1
        wdiWrites: 0
      }
      """

  规则: 故障聚合 — 转向/制动/激光雷达统一掩码

    转向故障 → FZC_FAULT_STEER(0x01)；制动故障 → FZC_FAULT_BRAKE(0x02)；
    激光雷达故障 → FZC_FAULT_LIDAR(0x04)。多故障同时置位。安全状态：
    转向/制动任一 → FAULT；仅非关键故障（激光雷达/CAN/自检）→ DEGRADED；
    无故障 → OK。

    场景: 制动故障置位 BRAKE 掩码
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "brakeFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 18
        dtcReported: 1
      }
      """

    场景: 激光雷达故障置位 LIDAR 掩码且状态为 DEGRADED
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "lidarFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        faultMask: 4
        dtcReported: 0
        wdiWrites: 1
      }
      """

    场景: 全部故障同时置位组合掩码
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1, "brakeFault": 1, "lidarFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 23
        dtcReported: 1
      }
      """

    场景: 自检未完成时失败结果不置位自检掩码且看门狗正常
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "selfTestDone": false, "selfTestResult": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        selfTestDone: 0
        faultMask: 0
        dtcReported: 0
        wdiWrites: 1
      }
      """

    场景: 自检完成且通过时不置位自检掩码且看门狗正常
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "selfTestDone": true, "selfTestResult": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        selfTestDone: 1
        faultMask: 0
        dtcReported: 0
        wdiWrites: 1
      }
      """

  规则: 电机切断 — 宽限期抑制 / 结束后置位

    宽限期内（GraceCounter > 0）转向/制动故障抑制电机切断（写 0），故障仍
    记录于掩码。宽限期结束后（GraceCounter == 0）置位电机切断（写 1）。
    无转向/制动故障时电机切断保持 0。

    场景: 宽限期内关键故障抑制电机切断
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        faultMask: 17
        motorCutoff: 0
      }
      """

    场景: 宽限期结束后关键故障置位电机切断
      假如存在:
        """
        FzcSafetySetup: {
          phases: [
            { cycles: 1500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 2
        graceCounter: 0
        faultMask: 17
        motorCutoff: 1
        dtcReported: 1
      }
      """

    场景: 无关键故障时电机切断保持清除
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "lidarFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        faultMask: 4
        motorCutoff: 0
      }
      """

    场景: 故障清除后电机切断复位
      假如存在:
        """
        FzcSafetySetup: {
          phases: [
            { cycles: 1500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 },
          { "cycles": 1, "steerFault": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        motorCutoff: 0
        dtcReported: 1
      }
      """

  规则: CAN RX 质量 — 宽限期后 CAN_BUS_OFF 检测

    宽限期内（GraceCounter > 0）不轮询 Com_GetRxPduQuality。宽限期结束后若
    CVC Steer/Brake_Command PDU 质量 TIMED_OUT → 置 FZC_FAULT_CAN_BUS_OFF
    (0x0100)，状态 DEGRADED。CAN_BUS_OFF 为非关键故障，看门狗继续喂狗。

    场景: 宽限期内 RX 超时不置 CAN_BUS_OFF
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerCmdQuality": 2, "brakeCmdQuality": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        comQueries: 0
        dtcReported: 0
        wdiWrites: 1
      }
      """

    场景: 宽限期结束后 Steer 命令超时置 CAN_BUS_OFF
      假如存在:
        """
        FzcSafetySetup: {
          phases: [
            { cycles: 1500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerCmdQuality": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        faultMask: 256
        comQueries: 2
        dtcReported: 0
      }
      """

    场景: 宽限期结束后 Brake 命令超时同样置 CAN_BUS_OFF
      假如存在:
        """
        FzcSafetySetup: {
          phases: [
            { cycles: 1500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "brakeCmdQuality": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 1
        faultMask: 256
        comQueries: 2
        dtcReported: 0
      }
      """

    场景: 宽限期结束后 RX 质量正常不置 CAN_BUS_OFF
      假如存在:
        """
        FzcSafetySetup: {
          phases: [
            { cycles: 1500 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/safety":
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
        faultMask: 0
        comQueries: 2
        dtcReported: 0
      }
      """

  规则: 安全状态发布与重复 Init

    Safety_Status 与安全掩码每周期发布到 RTE。重复 Init 复位状态为 OK。

    场景: 故障清除后安全状态恢复 OK
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 },
          { "cycles": 1, "steerFault": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        faultMask: 0
        safetyStatus: 0
        dtcReported: 1
      }
      """

    场景: 重复 Init 复位看门狗翻转与状态
      当POST "/api/test/asw/fzc/safety":
      """
      {
        "phases": [
          { "cycles": 1, "steerFault": 1 },
          { "cycles": 1, "reinit": true, "steerFault": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        status: 0
        initialized: 1
        graceCounter: 1499
        faultMask: 0
        motorCutoff: 0
        dtcReported: 1
        wdiWrites: 1
      }
      """
