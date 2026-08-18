# language: zh-CN
功能: SC 切断继电器控制 (sc_relay)

  sc_relay.c 切断继电器 GPIO 控制模块（SWR-SC-010/011/012，ASIL D）的
  端到端测试：Init 置继电器 LOW（安全态）且不清除 kill 锁存、Energize 受
  锁存门控、DeEnergize 永久锁存断开（仅断电复位）、以及 10ms CheckTriggers
  级联触发——E-Stop（最高优先级）、心跳确认超时、合理性故障、蠕动防护、
  E2E 关键邮箱持久失败、自检失败、ESM 锁步错误、CAN bus-off、CAN 静默、
  GPIO 读回失配（连续 2 次阈值）。harness 以生产 TMS570 配置编译（无
  PLATFORM_POSIX/HIL），全部触发与锁存语义严格生效。

  背景:
    假如存在:
      """
      ScRelaySetup: {
        phases: []
      }
      """

  规则: 初始化与安全状态 — SC_Relay_Init

    Init 置 relay_commanded=FALSE、读回失配计数清零、kill_reason=NONE，
    并写继电器引脚 LOW（安全态）；kill 锁存（relay_killed）不被清除，
    仅断电复位（SWR-SC-011）。

    场景: 初始化后继电器 LOW 且未锁存
      当POST "/api/test/asw/sc/relay":
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
        commanded: 0
        killed: 0
        mismatchCount: 0
        reason: 0
        gioRelay: 0
      }
      """

    场景: kill 锁存不受 Init 影响（仅断电复位）
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "deEnergize" },
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        commanded: 0
        gioRelay: 0
      }
      """

  规则: 吸合 / 断开 — SWR-SC-010

    Energize 置 relay_commanded=TRUE 并写继电器引脚 HIGH；若已锁存则直接
    返回（不吸合）。DeEnergize 置 commanded=FALSE、relay_killed=TRUE 并写
    引脚 LOW（永久锁存）。

    场景: Energize 吸合继电器
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        commanded: 1
        gioRelay: 1
        killed: 0
      }
      """

    场景: DeEnergize 断开并永久锁存
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "deEnergize" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        commanded: 0
        killed: 1
        gioRelay: 0
      }
      """

    场景: 锁存后 Energize 被拒绝（不吸合）
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "deEnergize" },
          { "op": "energize" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        commanded: 0
        gioRelay: 0
      }
      """

  规则: 无触发条件 — 继电器保持吸合

    所有触发条件均为假（含自检健康、读回匹配）时，CheckTriggers 不改变
    任何状态，继电器持续吸合。

    场景: 无触发时持续吸合
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "checkTriggers", "repeats": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        commanded: 1
        killed: 0
        gioRelay: 1
        mismatchCount: 0
      }
      """

    场景: 未吸合时 CheckTriggers 安全
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        killed: 0
        mismatchCount: 0
      }
      """

  规则: E-Stop 触发 — SWR-SC-035 / GAP-SC-001

    SC_CAN_IsEStopActive 为真即触发 kill（reason=ESTOP），优先级高于心跳
    确认超时等所有后续触发。

    场景: E-Stop 命令触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "estop": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 7
        gioRelay: 0
      }
      """

    场景: E-Stop 优先级高于心跳超时
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "estop": 1, "hb": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 7
      }
      """

  规则: 心跳确认超时触发 — SWR-SC-012

    心跳确认超时（SC_Heartbeat_IsAnyConfirmed 为真）触发 kill
    （reason=HB_TIMEOUT）。

    场景: 心跳确认超时触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "hb": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 1
      }
      """

  规则: 合理性触发 — SWR-SC-012 / SSR-SC-018

    合理性故障（reason=PLAUSIBILITY）优先级高于 E2E 失败；蠕动防护
    （reason=CREEP_GUARD）由 standstill 扭矩交叉合理性故障触发。

    场景: 合理性故障触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "plaus": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 2
      }
      """

    场景: 合理性优先级高于 E2E 失败
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "plaus": 1, "e2e": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 2
      }
      """

    场景: 蠕动防护触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "creep": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 10
      }
      """

  规则: E2E 关键失败触发 — GAP-SC-002

    E2E 关键邮箱持久失败（SC_E2E_IsAnyCriticalFailed 为真）触发 kill
    （reason=E2E_FAIL），优先级高于自检失败。

    场景: E2E 关键失败触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "e2e": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 9
      }
      """

    场景: E2E 优先级高于自检失败
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "e2e": 1, "selftest": 0 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 9
      }
      """

  规则: 自检失败 / ESM 锁步错误触发 — SWR-SC-012

    自检不健康（reason=SELFTEST）或 ESM 锁步错误（reason=ESM）触发 kill。

    场景: 自检失败触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "selftest": 0 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 3
      }
      """

    场景: ESM 锁步错误触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "esm": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 4
      }
      """

  规则: CAN bus-off / 静默触发 — SWR-SC-036 / GAP-SC-003

    CAN bus-off（reason=BUSOFF）或 ≥200ms 无有效帧（reason=BUS_SILENCE）
    触发 kill。

    场景: CAN bus-off 触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "busoff": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 5
      }
      """

    场景: CAN 静默触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "busSilent": 1 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        reason: 8
      }
      """

  规则: GPIO 读回校验 — SWR-SC-012

    读回值与命令状态不符时递增失配计数；连续 2 次失配触发 kill
    （reason=READBACK）。读回匹配时计数清零。未吸合状态下读回非 0 同样
    计数。

    场景: 1 次读回失配不触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setReadback", "value": 0 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 0
        mismatchCount: 1
        reason: 0
      }
      """

    场景: 连续 2 次读回失配触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setReadback", "value": 0 },
          { "op": "checkTriggers" },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        killed: 1
        mismatchCount: 2
        reason: 6
        gioRelay: 0
      }
      """

    场景: 读回恢复匹配后失配计数清零
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setReadback", "value": 0 },
          { "op": "checkTriggers" },
          { "op": "setReadback", "value": 1 },
          { "op": "checkTriggers" },
          { "op": "setReadback", "value": 0 },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[2].state.mismatchCount: 1
        results[4].state.mismatchCount: 0
        results[6].state.mismatchCount: 1
        results[6].state.killed: 0
      }
      """

    场景: 未吸合状态读回非 0 触发 kill
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "setReadback", "value": 1 },
          { "op": "checkTriggers" },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        killed: 1
        mismatchCount: 2
        reason: 6
      }
      """

  规则: 已锁存后的行为 — SWR-SC-011

    已锁存后 CheckTriggers 直接返回，不再评估任何触发条件，kill_reason
    保持首次 kill 原因。

    场景: 已锁存后 CheckTriggers 不改变状态
      当POST "/api/test/asw/sc/relay":
      """
      {
        "phases": [
          { "op": "energize" },
          { "op": "setMock", "estop": 1 },
          { "op": "checkTriggers" },
          { "op": "setMock" },
          { "op": "checkTriggers" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[2].state: {
          killed: 1
          reason: 7
        }
        results[4].state: {
          killed: 1
          reason: 7
        }
      }
      """
