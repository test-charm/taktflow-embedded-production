# language: zh-CN
功能: FZC CAN 通信 (Swc_FzcCom)

  Swc_FzcCom 的端到端测试：E2E 发送保护（CRC-8 0x1D + 4-bit alive 计数器）、
  E2E 接收校验（CRC 校验）、RX 周期处理（CAN 监视器通知，防止误报 CAN 丢失）、
  TX 周期调度（心跳 0x011、转向状态 0x200、制动状态 0x201、制动故障 0x210、
  电机切断 0x211、激光雷达距离 0x220）。

  背景:
    假如存在:
      """
      FzcFzcComSetup: {
        phases: []
      }
      """

  规则: E2E 发送保护

    这些场景覆盖 Swc_FzcCom_E2eProtect：CRC-8 写入 byte0、alive 计数器写入
    byte1 低半字节、alive 递增与回绕、byte1 高半字节保留、Data ID 参与 CRC
    种子、非法参数拒绝、重新 Init 复位 alive。

    场景: 心跳 Data ID (0x03) 保护成功且 CRC 写入 byte0
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        op: e2eProtect
        ret: 0
        data= '1200000000000000'
      }]
      """

    场景: 制动指令 Data ID (0x08) 保护成功
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 8, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '2700000000000000'
      }]
      """

    场景: 零 Data ID 保护成功（CRC 种子与初始化值直接异或）
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 0, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= 'f500000000000000'
      }]
      """

    场景: 自定义 Data ID (0x10) 保护成功
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 16, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '4c00000000000000'
      }]
      """

    场景: alive 计数器随保护调用递增
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" },
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= '1200000000000000'
      },{
        data= '4f01000000000000'
      }]
      """

    场景: alive 计数器在 15 处回绕到 0
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000", "repeats": 16 },
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= '4e0f000000000000'
      },{
        data= '1200000000000000'
      }]
      """

    场景: 保护保留 byte1 高半字节
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "00a0000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= 'dca0000000000000'
      }]
      """

    场景: 重新 Init 复位 alive 计数器
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" },
          { "op": "init" },
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= '1200000000000000'
      },{
        op: init
      },{
        data= '1200000000000000'
      }]
      """

    场景: 空指针 / 过短长度保护均被拒绝
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "null" },
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000", "length": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
                         | 1   |
      """

  规则: E2E 接收校验

    这些场景覆盖 Swc_FzcCom_E2eCheck：合法帧通过、CRC 损坏拒绝、Data ID 不
    匹配拒绝、非法参数拒绝。

    场景: 合法保护帧通过校验
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" },
          { "op": "e2eCheck", "dataId": 3, "data": "1200000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 0   |
                         | 0   |
      """

    场景: CRC 损坏的帧被拒绝
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "dataId": 3, "data": "1200000100000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        op: e2eCheck
        ret: 1
      }]
      """

    场景: 与保护时不同的 Data ID 校验被拒绝
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "dataId": 3, "data": "0000000000000000" },
          { "op": "e2eCheck", "dataId": 8, "data": "1200000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 0   |
                         | 1   |
      """

    场景: 空指针 / 过短长度校验均被拒绝
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "dataId": 3, "data": "null" },
          { "op": "e2eCheck", "dataId": 3, "data": "0000000000000000", "length": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
                         | 1   |
      """

  规则: RX 周期处理

    这些场景覆盖 Swc_FzcCom_Receive：未初始化守卫、每个 RX 周期通知 CAN
    监视器（复位静默计数器，防止误报 CAN 丢失）。

    场景: 未初始化时 Receive 空转且不通知 CAN 监视器
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "receive", "cycles": 3, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        op: receive
        canmonNotify: 0
      }]
      """

    场景: 单个 RX 周期通知一次 CAN 监视器
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "receive", "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        canmonNotify: 1
      }]
      """

    场景: 多个 RX 周期每个周期都通知 CAN 监视器
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "receive", "cycles": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        canmonNotify: 5
      }]
      """

  规则: TX 周期调度

    这些场景覆盖 Swc_FzcCom_TransmitSchedule：未初始化守卫、心跳信号
    （ECU ID / 运行模式 / 故障状态）、转向与制动状态、制动故障与电机切断、
    激光雷达距离信号、多周期计数器、TX 调度周期 1000 回绕。

    场景: 未初始化时 TX 不发送任何信号
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 1, "faultMask": 5, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteDispatch: 0
        hbEcuId: 0
        hbFaultStatus: 0
      }
      """

    场景: 健康 TX 发送全部信号（心跳 / 转向 / 制动 / 故障 / 电机切断 / 激光雷达）
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 1, "faultMask": 5,
            "steerAngle": 45, "steerFault": 1, "brakePos": 75,
            "brakeFault": 1, "motorCutoff": 1,
            "lidarZone": 2, "lidarDist": 291, "lidarSignal": 43981 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteDispatch: 1
        steerComSend: 1
        hbEcuId: 2
        hbOpMode: 1
        hbFaultStatus: 5
        steerAngle: 45
        steerFault: 1
        brakePos: 75
        brakeFaultType: 1
        motorCutoffReq: 1
        lidarZone: 2
        lidarRange: 291
        lidarSignal: 171
      }
      """

    场景: 心跳运行模式与故障状态仅取低半字节
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 31, "faultMask": 255 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        hbOpMode: 15
        hbFaultStatus: 15
      }
      """

    场景: 多次 TX 周期递增调试计数器
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 3, "vehicleState": 1, "faultMask": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteDispatch: 3
        steerComSend: 3
        hbEcuId: 2
      }
      """

    场景: TX 调度周期在 1000 处回绕后继续发射
      当POST "/api/test/asw/fzc/fzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1000, "vehicleState": 1, "faultMask": 0,
            "steerAngle": 45, "brakePos": 75 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        rteDispatch: 1000
        steerComSend: 1000
        hbEcuId: 2
        steerAngle: 45
        brakePos: 75
      }
      """
