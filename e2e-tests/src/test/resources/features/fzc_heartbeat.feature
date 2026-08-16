# language: zh-CN
功能: FZC 心跳 (Swc_Heartbeat)

  FZC Swc_Heartbeat 心跳 SWC 的端到端测试：TX 50ms 边界调度（存活计数器
  递增与 15 回绕、车辆状态/故障位掩码发布、CAN bus-off TX 抑制）、Init 时
  ECU ID 写入。

  背景:
    假如存在:
      """
      FzcHeartbeatSetup: {
        phases: []
      }
      """

  规则: 初始化 — Swc_Heartbeat_Init

    Swc_Heartbeat_Init 清零周期/存活计数器、置位初始化标志，并把 FZC ECU ID
    写入 RTE 心跳 ECU_ID 信号供 Com TX 拉取。

    场景: 初始化后写入 FZC ECU ID 且计数器清零
      当POST "/api/test/asw/fzc/heartbeat":
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
        aliveCounter: 0
        cycleCounter: 1
        initialized: 1
        ecuId: 2
        operatingMode: 0
        faultStatus: 0
        alive: 0
      }
      """

  规则: TX 调度 — Swc_Heartbeat_MainFunction

    Swc_Heartbeat_MainFunction 每 10ms 周期调用：累计周期计数器，在 50ms
    (5 周期) 边界读取车辆状态与故障掩码，把 OperatingMode / FaultStatus /
    Alive 写入 RTE 供 Com TX 自动拉取，并递增 4-bit 存活计数器 (15→0 回绕)。
    CAN bus-off 位置位时抑制 TX。

    场景: 未初始化时主函数不动作
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 10, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 0
        cycleCounter: 0
        initialized: 0
        ecuId: 0
        operatingMode: 0
        faultStatus: 0
        alive: 0
      }
      """

    场景: 初始化后 4 周期内未到 TX 边界不发送
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 0
        cycleCounter: 4
        operatingMode: 0
        faultStatus: 0
        alive: 0
      }
      """

    场景: 恰好在 5 周期 (50ms) 边界发送心跳
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 0
        alive: 0
      }
      """

    场景: 每 5 周期发送一次 (10 周期两次)
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 2
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 0
        alive: 1
      }
      """

    场景: 存活计数器在第 16 次发送时从 15 回绕到 0
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 80 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 0
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 0
        alive: 15
      }
      """

    场景: TX 边界将车辆状态写入心跳 OperatingMode
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "vehicleState": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 2
        faultStatus: 0
        alive: 0
      }
      """

    场景: OperatingMode 只取车辆状态低 4 位
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "vehicleState": 31 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 15
        faultStatus: 0
        alive: 0
      }
      """

    场景: TX 边界将故障掩码写入心跳 FaultStatus
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "faultMask": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 1
        alive: 0
      }
      """

    场景: FaultStatus 只取故障掩码低 4 位
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "faultMask": 49 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 1
        alive: 0
      }
      """

  规则: CAN 总线关闭抑制 — fault_mask bus-off

    fault_mask 的 bus-off 位（bit8=0x0100）置位时，MainFunction 在 TX 边界
    读取后立即返回，不写入任何心跳信号、不递增存活计数器。

    场景: CAN 总线关闭时抑制心跳 TX
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 10, "faultMask": 256 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 0
        cycleCounter: 0
        operatingMode: 0
        faultStatus: 0
        alive: 0
      }
      """

    场景: 总线关闭清除后心跳 TX 恢复
      假如存在:
        """
        FzcHeartbeatSetup: {
          phases: [
            { cycles: 5, faultMask: 256 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "faultMask": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 1
        cycleCounter: 0
        operatingMode: 1
        faultStatus: 0
        alive: 0
      }
      """

    场景: 车辆状态变化在后续周期透传到 OperatingMode
      假如存在:
        """
        FzcHeartbeatSetup: {
          phases: [
            { cycles: 5, vehicleState: 1 }
          ]
        }
        """
      当POST "/api/test/asw/fzc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 5, "vehicleState": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        aliveCounter: 2
        cycleCounter: 0
        operatingMode: 4
        faultStatus: 0
        alive: 1
      }
      """
