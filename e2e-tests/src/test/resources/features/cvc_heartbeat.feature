# language: zh-CN
功能: CVC 心跳 (Swc_Heartbeat)

  Swc_Heartbeat 心跳 TX/RX 监控 SWC 的端到端测试：TX 50ms 边界调度（存活
  计数器递增与 15 回绕、WdgM SE3 喂狗、OperatingMode RTE 写）、RX 指示
  （FZC/RZC/未知 ECU 标志位）、post-INIT 宽限期通信状态复位（OK + E2E SM
  强制 VALID）。

  背景:
    假如存在:
      """
      CvcHeartbeatSetup: {
        phases: []
      }
      """

  规则: TX 调度 — Swc_Heartbeat_MainFunction

    Swc_Heartbeat_MainFunction 每 10ms 周期调用：累计 tx_timer，在 50ms
    (5 周期) 边界读取车辆状态、触发 WdgM SE3 检查点、递增 4-bit 存活计数器
    (15→0 回绕) 并把 OperatingMode 写入 RTE 供 Com TX 自动拉取。以下场景
    驱动 TX 全链路。

    场景: 初始化后默认状态为 TIMEOUT 且 E2E SM 为 INIT
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 0
        fzcCommStatus: 1
        rzcCommStatus: 1
        fzcSmStatus: 0
        rzcSmStatus: 0
      }
      """

    场景: 未初始化时主函数不动作
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 0
        operatingMode: 0
        fzcCommStatus: 0
        rzcCommStatus: 0
        fzcSmStatus: 0
        rzcSmStatus: 0
      }
      """

    场景: 初始化后 4 周期内未到 TX 边界不发送
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 0
        operatingMode: 0
        fzcCommStatus: 1
        rzcCommStatus: 1
      }
      """

    场景: 恰好在 5 周期 (50ms) 边界发送心跳
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 1
        wdgmLastSeId: 3
        operatingMode: 1
      }
      """

    场景: 每 5 周期发送一次 (10 周期两次)
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 2
        operatingMode: 1
      }
      """

    场景: 存活计数器在第 16 次发送时从 15 回绕到 0
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 16
      }
      """

    场景: TX 边界将车辆状态写入心跳 OperatingMode
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 1
        operatingMode: 2
      }
      """

    场景: 车辆状态变化在后续周期透传到 OperatingMode
      假如存在:
        """
        CvcHeartbeatSetup: {
          phases: [
            { cycles: 5, vehicleState: 1 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/heartbeat":
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
        wdgmCheckpointCount: 2
        operatingMode: 4
      }
      """

  规则: RX 指示 — Swc_Heartbeat_RxIndication

    Swc_Heartbeat_RxIndication 由 Com 层在收到心跳帧时调用，携带源 ECU ID。
    该函数对 FZC (0x02) / RZC (0x03) 分别锁存 RX 标志位，未知 ECU ID 被忽略。

    场景: FZC 心跳指示置位 FZC RX 标志
      当POST "/api/test/asw/cvc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 1, "rxEcu": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        fzcRxFlag: 1
        rzcRxFlag: 0
      }
      """

    场景: RZC 心跳指示置位 RZC RX 标志
      当POST "/api/test/asw/cvc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 1, "rxEcu": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rzcRxFlag: 1
        fzcRxFlag: 0
      }
      """

    场景: 未知 ECU 心跳指示被忽略
      当POST "/api/test/asw/cvc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 1, "rxEcu": 255 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        fzcRxFlag: 0
        rzcRxFlag: 0
      }
      """

  规则: 通信状态复位 — Swc_Heartbeat_ResetCommStatus

    Docker 启动瞬态过后调用 ResetCommStatus：FZC/RZC 通信状态置 OK，E2E SM
    强制 VALID（跳过 MIN_OK_INIT 窗口），并把 OK 写入 RTE 通信状态信号。

    场景: 复位通信状态置 OK 并强制 E2E SM 为 VALID
      当POST "/api/test/asw/cvc/heartbeat":
      """
      {
        "phases": [
          { "cycles": 1, "resetComm": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        fzcCommStatus: 0
        rzcCommStatus: 0
        fzcSmStatus: 1
        rzcSmStatus: 1
        rteFzcCommStatus: 0
        rteRzcCommStatus: 0
      }
      """
