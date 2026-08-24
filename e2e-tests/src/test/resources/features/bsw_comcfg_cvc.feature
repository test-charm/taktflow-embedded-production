# language: zh-CN
功能: CVC Com 真端到端 — vcan0 总线观测 (bsw_comcfg_cvc)

  CVC Com 层的真端到端测试：运行在 SIL Docker 栈（docker compose -f
  docker/docker-compose.dev.yml up，7 个真实 vECU 共享宿主机 vcan0）上，通过
  /api/test/bsw/comcfg/cvc 的 bus-probe 观测 CVC 真实发出的 CAN 帧，验证可观测
  总线属性与 DBC 唯一真源 (gateway/taktflow_vehicle.dbc) 一致：

    - DLC 与 DBC 一致                     → dlcOk
    - 周期与 DBC GenMsgCycleTime 兼容     → cycleOk（有界带宽 + 稳定周期性，
      兼容 Docker 非实时调度的约 3x 节拍差异）
    - E2E 头（byte0 = Alive:4|DataId:4、byte1 = CRC-8 SAE-J1850）真实有效
                                         → dataIdOk / aliveMonotonic / crcValid
    - 查找失败不崩溃（fail-closed）       → found=false + busUp 状态

  被测链路（全真实）：
    [任务体 → Com_MainFunction_Tx → PduR → CanIf → Can_Posix → vcan0]

  覆盖映射（原静态配置读回用例 → 本文件的真端到端断言）：
    - 安全关键 TX PDU 的 DLC/周期/E2E DataId（EStop/心跳/VS/Torque/Steer）
      → 本文件的 bus-probe 各消息场景；EStop 锁存流帧属性由
      bsw_rtetaskbodies 的 ftti 规则断言
    - 非安全关键 Body_Control_Cmd（DLC=4、100ms、无 E2E）→ bus-probe 场景
    - E2E 头与信号布局 → 帧字节级 dataId/alive/CRC 校验 + cantools 解码
    - 结构不变量 / 表规模 / 越界查找     → 帧级 dlcOk/共存与 fail-closed 场景
    - RX 超时窗口 / SM 窗口 / Dem 事件等生成配置内部参数 → 由 BSW 单测与
      SIL 场景（如 sil_009 E2E corruption）在行为层覆盖，本文件只断言
      总线可观测行为

  背景:
    假如存在:
      """
      BswComCfgSetup: {
        phases: []
      }
      """

  规则: 真端到端 — 安全关键 TX 帧总线属性与 DBC 一致

    bus-probe 每次 POST 探测指定的 DBC 消息（results[0] 恒为目标消息）。

    场景: CVC_Heartbeat 总线 50ms cadence、E2E dataId=2 有效
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["CVC_Heartbeat"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        dlc: 4
        dlcOk: true
        cycleMs: 50
        cycleOk: true
        e2e: true
        dataId: 2
        dataIdOk: true
        aliveMonotonic: true
        crcValid: true
      }
      """

    场景: Vehicle_State 总线 10ms cadence、E2E dataId=5 有效
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["Vehicle_State"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        dlc: 6
        dlcOk: true
        cycleMs: 10
        cycleOk: true
        e2e: true
        dataId: 5
        dataIdOk: true
        aliveMonotonic: true
        crcValid: true
      }
      """

    场景: Torque_Request 总线 10ms cadence、E2E dataId=6 有效
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["Torque_Request"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        dlc: 8
        dlcOk: true
        cycleMs: 10
        cycleOk: true
        e2e: true
        dataId: 6
        dataIdOk: true
        aliveMonotonic: true
        crcValid: true
      }
      """

    场景: Steer_Command 总线 10ms cadence、E2E dataId=7 有效
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["Steer_Command"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        dlc: 8
        dlcOk: true
        cycleMs: 10
        cycleOk: true
        e2e: true
        dataId: 7
        dataIdOk: true
        aliveMonotonic: true
        crcValid: true
      }
      """

  规则: 真端到端 — 非安全关键 TX 帧（无 E2E）

    场景: Body_Control_Cmd 总线 100ms cadence、无 E2E 保护
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["Body_Control_Cmd"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        dlc: 4
        dlcOk: true
        cycleMs: 100
        cycleOk: true
        e2e: false
      }
      """

  规则: 真端到端 — UDS 诊断链路（PduR→Dcm→CanTp 真实路由）
    与 HIL `test_hil_uds.py` 同族：向 CVC 0x7E0 发 UDS 请求，观测 0x7E8 响应。
    驱动 RX 链（CanIf→PduR→Dcm→CanTp）与 TX 链（PduR_DcmTransmit→CanIf→
    Can_Write）——单元测试 `test_PduR_RxIndication_routes_to_dcm` 与
    `test_PduR_DcmTransmit_delegates_to_pdur_transmit` 覆盖的路径在此转为
    真实总线行为。

    场景: UDS 读取 DID 0xF190 → 0x62 正响应（诊断链路端到端）
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "uds", "did": 61840 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        requestSent: true
        responseSeen: true
        responseSid: 98
        did: 61840
        hasData: true
      }
      """

    场景: UDS 未知 DID → 0x7F 负响应（路由仍正确处理，NRC 31）
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "uds", "did": 65535 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: true
        busUp: true
        ecu: "cvc"
        requestSent: true
        responseSeen: true
        responseSid: 127
        hasData: false
      }
      """
      那么response should be:
      """
      body.json.results[0].state.nrc = 49
      """

  规则: 真端到端 — fail-closed（查找失败不崩溃）

    场景: 未知消息探测 fail-closed（查不到 → found=false，bus 仍可用）
      当POST "/api/test/bsw/comcfg/cvc":
      """
      {
        "phases": [
          { "op": "bus-probe", "targets": ["Msg_Does_Not_Exist"] }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        found: false
        busUp: true
      }
      """
