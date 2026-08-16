# language: zh-CN
功能: RZC CAN 通信 (Swc_RzcCom)

  Swc_RzcCom 的端到端测试：E2E 发送保护（CRC-8 0x1D + 4-bit alive 计数器）、
  E2E 接收校验（CRC 校验 + alive 单调检查，3 次连续失败 → 扭矩安全默认 0）、
  RX 周期处理（0x001 E-stop 广播、0x100 Vehicle_State + Torque、100ms 扭矩
  超时强制扭矩 0、E2E 失败 → DEM CAN_BUS_OFF）、TX 周期调度（心跳 0x012、
  电机状态 0x300、电机电流 0x301、电机温度 0x302、电池状态 0x303）。

  背景:
    假如存在:
      """
      RzcComSetup: {
        phases: []
      }
      """

  规则: E2E 发送保护

    这些场景覆盖 Swc_RzcCom_E2eProtect：CRC-8 写入 byte0、alive 计数器写入
    byte1 低半字节、alive 递增与回绕、各 TX PDU 的 dataId 映射、非法参数拒绝。

    场景: 心跳 PDU (0x012) 保护成功且 CRC 写入 byte0
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        op: e2eProtect
        ret: 0
        data= '6900000000000000'
      }]
      """

    场景: 电机状态 PDU (0x300) 使用 0x0E dataId 计算 CRC
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 1, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '0100000000000000'
      }]
      """

    场景: 电机电流 PDU (0x301) 使用 0x0F dataId 计算 CRC
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 2, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '5c00000000000000'
      }]
      """

    场景: 电池状态 PDU (0x303) 使用 0x13 dataId 计算 CRC
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 4, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '5e00000000000000'
      }]
      """

    场景: 电机温度 PDU (0x302) 使用 0x00 dataId 计算 CRC
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 3, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 0
        data= '0000000000000000'
      }]
      """

    场景: alive 计数器随保护调用递增
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" },
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= '6900000000000000'
      },{
        data= '3401000000000000'
      }]
      """

    场景: alive 计数器在 15 处回绕到 0
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000", "repeats": 16 },
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        data= '350f000000000000'
      },{
        data= '6900000000000000'
      }]
      """

    场景: 空指针 / 过短长度 / 越界 PDU 均被拒绝
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "null" },
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000", "length": 1 },
          { "op": "e2eProtect", "pduId": 16, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
                         | 1   |
                         | 1   |
      """

  规则: E2E 接收校验

    这些场景覆盖 Swc_RzcCom_E2eCheck：CRC 校验、alive 单调检查（重放拒绝）、
    失败计数与重同步、RX PDU dataId 回退（Vehicle_Torque → 0x05）、非法参数拒绝。

    场景: 合法心跳帧通过校验
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" },
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" },
          { "op": "e2eCheck", "pduId": 0, "data": "3401000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        op: e2eCheck
        ret: 0
      }
      """

    场景: CRC 损坏的帧被拒绝
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 0, "data": "0001000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: [{
        ret: 1
      }]
      """

    场景: 同一 alive 计数器重放被拒绝
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" },
          { "op": "e2eProtect", "pduId": 0, "data": "0000000000000000" },
          { "op": "e2eCheck", "pduId": 0, "data": "3401000000000000" },
          { "op": "e2eCheck", "pduId": 0, "data": "3401000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | *   |
                         | *   |
                         | 0   |
                         | 1   |
      """

    场景: Vehicle_Torque 帧使用 RX dataId 0x05 校验通过
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 7, "data": "6901000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 0   |
      """

    场景: 未知 RX PDU 使用默认 dataId 0x00 校验通过
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 5, "data": "5d01000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 0   |
      """

    场景: pdu0 E2E 校验使用 TX 心跳 dataId 0x04（E-stop dataId 查找不可达）
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 0, "data": "0001000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
      """

    场景: RX alive 计数器在 15 处回绕后合法帧通过
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 0, "data": "350f000000000000" },
          { "op": "e2eCheck", "pduId": 0, "data": "6900000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
                         | 0   |
      """

    场景: 空指针 / 过短长度 / 越界 PDU 校验均被拒绝
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 0, "data": "null" },
          { "op": "e2eCheck", "pduId": 0, "data": "0000000000000000", "length": 1 },
          { "op": "e2eCheck", "pduId": 16, "data": "0000000000000000" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results: | ret |
                         | 1   |
                         | 1   |
                         | 1   |
      """

  规则: RX 周期处理

    这些场景覆盖 Swc_RzcCom_Receive：未初始化守卫、E-stop 直通、E2E 3 次连续
    失败 → 扭矩安全默认 0 + DEM CAN_BUS_OFF、扭矩超时强制扭矩 0、新扭矩重置
    超时。

    场景: 未初始化时 Receive 空转
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 1, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 50
        estopActive: 0
        demBusOff: -1
      }
      """

    场景: E-stop 激活时保持 RTE E-stop 信号
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 1, "vehicleState": 1, "torqueCmd": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 50
        estopActive: 1
        demBusOff: -1
      }
      """

    场景: E-stop 未激活时不写 E-stop 信号
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 50
        estopActive: 0
        demBusOff: -1
      }
      """

    场景: 3 次连续 E2E 失败触发扭矩安全默认 0 并上报 CAN 总线关闭 DTC
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 7, "data": "0000000000000000", "repeats": 3 },
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].ret: 1
        torqueCmd: 0
        demBusOff: 1
      }
      """

    场景: 2 次 E2E 失败低于阈值时扭矩照常传递
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "e2eCheck", "pduId": 7, "data": "0000000000000000", "repeats": 2 },
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 50
        demBusOff: -1
      }
      """

    场景: 扭矩指令 100ms 超时强制为 0
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 11 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 0
        demBusOff: -1
      }
      """

    场景: 扭矩超时边界（10 周期）尚未触发
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 50
      }
      """

    场景: 扭矩超时计数器在 0xFFFF 处饱和不溢出
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 0, "cycles": 70000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 0
        demBusOff: -1
      }
      """

    场景: 新扭矩指令重置超时计数
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 50, "cycles": 6 },
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 60, "cycles": 1 },
          { "op": "receive", "estop": 0, "vehicleState": 1, "torqueCmd": 60, "cycles": 6 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueCmd: 60
      }
      """

  规则: TX 周期调度

    这些场景覆盖 Swc_RzcCom_TransmitSchedule：未初始化守卫、心跳故障状态组合、
    电机状态/电流信号、reverse 方向位与过流标志、电机温度（cycle%10==3）与
    电池状态（cycle%20==7）定时发射。

    场景: 未初始化时 TX 不发送任何信号
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 1, "faultMask": 0, "torqueEcho": 42, "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        hbEcuId: 0
        hbFaultStatus: 0
        mstatTorqueEcho: 0
      }
      """

    场景: 健康 TX 发送心跳与电机状态信号
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 1, "faultMask": 0,
            "torqueEcho": 42, "speedRpm": 100, "motorDir": 1, "motorEnable": 1,
            "motorFault": 0, "currentMa": 500, "overcurrent": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        hbEcuId: 3
        hbFaultStatus: 1
        mstatTorqueEcho: 42
        mstatSpeedRpm: 100
        mstatDirection: 1
        mstatEnable: 1
        mstatFault: 0
        curMa: 500
        curDirReverse: 0
        curEnable: 1
        curOvercurrent: 0
        curTorqueEcho: 42
      }
      """

    场景: 心跳故障状态组合车辆状态与故障掩码
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 4, "faultMask": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        hbFaultStatus: 164
      }
      """

    场景: reverse 方向与过流标志置位
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1, "vehicleState": 1, "faultMask": 0,
            "motorDir": 2, "motorEnable": 1, "overcurrent": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        curDirReverse: 1
        curOvercurrent: 1
        curEnable: 1
      }
      """

    场景: 电机温度在第 3 个周期发射
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 3, "vehicleState": 1, "faultMask": 0,
            "temp1Dc": 250, "temp2Dc": 350, "deratingPct": 75 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1C: 250
        temp2C: 350
        deratingPct: 75
      }
      """

    场景: 电池状态在第 7 个周期发射
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 7, "vehicleState": 1, "faultMask": 0,
            "batteryMv": 12000, "batteryStatus": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        batteryMv: 12000
        batteryLevel: 2
      }
      """

    场景: TX 调度周期在 1000 处回绕后继续发射
      当POST "/api/test/asw/rzc/rzccom":
      """
      {
        "phases": [
          { "op": "tx", "cycles": 1000, "vehicleState": 1, "faultMask": 0,
            "torqueEcho": 42, "speedRpm": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        hbEcuId: 3
        mstatTorqueEcho: 42
        mstatSpeedRpm: 100
      }
      """
