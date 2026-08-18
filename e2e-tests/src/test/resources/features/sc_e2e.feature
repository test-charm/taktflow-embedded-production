# language: zh-CN
功能: SC E2E CRC-8 校验 (sc_e2e)

  sc_e2e.c E2E 保护校验模块（SWR-SC-003 / GAP-SC-002 / SWR-SC-030，ASIL D）
  的端到端测试：CRC-8（SAE-J1850，poly 0x1D，init 0xFF）逐帧校验、byte0
  DataId 低半字节校验、byte0 高半字节 alive 计数器单调递增（15→0 回绕）、
  每邮箱连续失败计数（阈值 3）持久锁存、启动宽限期（5 tick）与到期复位、
  关键邮箱（E-Stop + CVC/FZC/RZC 心跳）锁存触发 relay-kill（GAP-SC-002），
  以及 SC_Status TX CRC 计算（SWR-SC-030）。harness 以生产 TMS570 配置编译
  （无 PLATFORM_POSIX/HIL），严格 3 次阈值与 5 tick 宽限生效。

  背景:
    假如存在:
      """
      ScE2eSetup: {
        phases: []
      }
      """

  规则: 初始化 — SC_E2E_Init

    Init 清零全部 per-message 状态（lastAlive/failCount/failed），置 firstRx
    全 TRUE（首帧跳过 alive 校验），并将启动宽限计数器置为 5（生产 50ms）。

    场景: 初始化后全部状态清零且宽限期就绪
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        lastAlive: [0, 0, 0, 0, 0, 0]
        firstRx: [1, 1, 1, 1, 1, 1]
        failCount: [0, 0, 0, 0, 0, 0]
        failed: [0, 0, 0, 0, 0, 0]
        isMsgFailed: [0, 0, 0, 0, 0, 0]
        grace: 5
      }
      """

  规则: 参数校验守卫 — SC_E2E_Check

    data==NULL、msgIndex 越界、dlc<2 三者任一成立即拒绝（fail-closed），
    不改变任何模块状态。

    场景: NULL 数据指针被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "dlc": 8, "alive": 1, "nullData": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 0
        state.failCount: [0, 0, 0, 0, 0, 0]
        state.firstRx: [1, 1, 1, 1, 1, 1]
      }
      """

    场景: 越界 msgIndex 被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 6 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 0
        state.lastAlive: [0, 0, 0, 0, 0, 0]
      }
      """

    场景: dlc 小于 2 被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "dlc": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "dlc": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].ret: 0
        results[2].ret: 0
      }
      """

    场景: 首帧有效消息被接受（跳过 alive 校验）
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 1
        state.lastAlive: [1, 0, 0, 0, 0, 0]
        state.firstRx: [0, 1, 1, 1, 1, 1]
      }
      """

  规则: CRC / DataId 校验 — SWR-SC-003

    重建 CRC-8（payload 字节 2..DLC-1 + DataId 最后）并与 byte1 比对，任一
    损坏（CRC 字节、byte0 DataId 低半字节、payload 字节）即拒绝；dlc=2 时
    payload 长度 0、CRC 仅覆盖 DataId；dlc>8 时 payload 截断至 6 字节。

    场景: CRC 字节损坏被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 0
        state.failCount: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: DataId 低半字节不匹配被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "dataIdCorrupt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 0
        state.failCount: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: payload 字节损坏被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "payloadCorrupt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 0
        state.failCount: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: dlc=2 零 payload 有效（CRC 仅覆盖 DataId）
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "dlc": 2, "alive": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 1
        state.lastAlive: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: dlc=255 payload 截断至 6 字节后仍有效
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "dlc": 255, "alive": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        ret: 1
        state.lastAlive: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: 内部 sc_crc8 已知向量 {0x01,0xAA,0x55} 计算结果
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "crc8", "len": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 183
      }
      """

    场景: 内部 sc_crc8 零长度返回 0x00
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "crc8", "len": 0, "nullData": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 0
      }
      """

  规则: alive 计数器校验 — SWR-SC-003

    首帧跳过 alive 校验；后续每帧要求 alive = (上一帧 + 1) & 0x0F，重复、
    跳号均被拒绝；15→0 回绕被接受。

    场景: 连续递增 alive 被接受
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].ret: 1
        results[2].ret: 1
        results[2].state.lastAlive: [2, 0, 0, 0, 0, 0]
      }
      """

    场景: 重复 alive 被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 5 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].ret: 1
        results[2].ret: 0
        results[2].state.lastAlive: [5, 0, 0, 0, 0, 0]
      }
      """

    场景: alive 跳号被拒绝
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].ret: 1
        results[2].ret: 0
        results[2].state.lastAlive: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: alive 15→0 回绕被接受
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 15 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].ret: 1
        results[2].ret: 1
        results[2].state.lastAlive: [0, 0, 0, 0, 0, 0]
      }
      """

  规则: 连续失败持久锁存 — SC_E2E_MAX_CONSEC_FAIL=3

    任一邮箱连续失败计数达 3 后持久锁存 IsMsgFailed=TRUE；有效帧重置失败
    计数；无效索引 IsMsgFailed 恒返回 TRUE（fail-closed）。

    场景: 2 次连续失败未锁存
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2, "crcCorrupt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        ret: 0
        state.failCount: [2, 0, 0, 0, 0, 0]
        state.failed: [0, 0, 0, 0, 0, 0]
        state.isMsgFailed: [0, 0, 0, 0, 0, 0]
      }
      """

    场景: 3 次连续失败持久锁存
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 3, "crcCorrupt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3]: {
        ret: 0
        state.failCount: [3, 0, 0, 0, 0, 0]
        state.failed: [1, 0, 0, 0, 0, 0]
        state.isMsgFailed: [1, 0, 0, 0, 0, 0]
      }
      """

    场景: 有效帧重置失败计数
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3]: {
        ret: 1
        state.failCount: [0, 0, 0, 0, 0, 0]
        state.failed: [0, 0, 0, 0, 0, 0]
      }
      """

    场景: 无效索引 IsMsgFailed 恒返回 TRUE
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        isMsgFailedInvalid: 1
      }
      """

  规则: 启动宽限期 — SC_E2E_IsAnyCriticalFailed

    宽限期内（5 tick）恒返回 FALSE；宽限到期瞬间复位全部失败状态（仅
    post-grace 失败才可触发 relay-kill）；宽限结束后按关键邮箱锁存判定。

    场景: 宽限期内不触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        anyCriticalFailed: 0
        state.grace: 1
      }
      """

    场景: 宽限到期复位宽限期内锁存的失败
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4]: {
        anyCriticalFailed: 0
        state.grace: 0
        state.failed: [0, 0, 0, 0, 0, 0]
        state.failCount: [0, 0, 0, 0, 0, 0]
        state.firstRx: [1, 1, 1, 1, 1, 1]
      }
      """

    场景: 宽限结束后 E-Stop 失败触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 5 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 1, "msgIndex": 0, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[5].anyCriticalFailed: 1
        results[5].state.failed: [1, 0, 0, 0, 0, 0]
      }
      """

  规则: 关键邮箱判定 — GAP-SC-002

    仅 E-Stop + CVC/FZC/RZC 心跳四个关键邮箱的持久失败触发 relay-kill；
    非关键邮箱（VehicleState/MotorCurrent）失败不触发。

    场景: CVC 心跳邮箱失败触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 5 },
          { "op": "check", "dataId": 2, "msgIndex": 1, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 2, "msgIndex": 1, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 2, "msgIndex": 1, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[5].anyCriticalFailed: 1
        results[5].state.failed: [0, 1, 0, 0, 0, 0]
      }
      """

    场景: FZC 心跳邮箱失败触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 5 },
          { "op": "check", "dataId": 3, "msgIndex": 2, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 3, "msgIndex": 2, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 3, "msgIndex": 2, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[5].anyCriticalFailed: 1
        results[5].state.failed: [0, 0, 1, 0, 0, 0]
      }
      """

    场景: RZC 心跳邮箱失败触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 5 },
          { "op": "check", "dataId": 4, "msgIndex": 3, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 4, "msgIndex": 3, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 4, "msgIndex": 3, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[5].anyCriticalFailed: 1
        results[5].state.failed: [0, 0, 0, 1, 0, 0]
      }
      """

    场景: 仅非关键邮箱失败不触发 relay-kill
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 5 },
          { "op": "check", "dataId": 5, "msgIndex": 4, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 5, "msgIndex": 4, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 5, "msgIndex": 4, "alive": 3, "crcCorrupt": 1 },
          { "op": "check", "dataId": 15, "msgIndex": 5, "alive": 1, "crcCorrupt": 1 },
          { "op": "check", "dataId": 15, "msgIndex": 5, "alive": 2, "crcCorrupt": 1 },
          { "op": "check", "dataId": 15, "msgIndex": 5, "alive": 3, "crcCorrupt": 1 },
          { "op": "drainGrace", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[8].anyCriticalFailed: 0
        results[8].state.failed: [0, 0, 0, 0, 1, 1]
        results[8].state.isMsgFailed: [0, 0, 0, 0, 1, 1]
      }
      """

  规则: SC_Status TX CRC — SWR-SC-030

    SC_E2E_ComputeCRC8 独立计算 SC_Status 帧 CRC：NULL → 0、零长度 → 0x00
    （init ^ XOR-out）、已知向量 {0x01,0xAA,0x55} → 0xB7、单字节 {0x01} →
    0x26。

    场景: ComputeCRC8 NULL 返回 0
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "compute", "len": 3, "nullData": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 0
      }
      """

    场景: ComputeCRC8 已知向量 {0x01,0xAA,0x55}
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "compute", "len": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 183
      }
      """

    场景: ComputeCRC8 零长度返回 0x00
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "compute", "len": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 0
      }
      """

    场景: ComputeCRC8 单字节 {0x01}
      当POST "/api/test/asw/sc/e2e":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "compute", "len": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        crc: 38
      }
      """
