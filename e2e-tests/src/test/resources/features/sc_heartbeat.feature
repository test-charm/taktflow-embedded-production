# language: zh-CN
功能: SC 心跳监控 (sc_heartbeat)

  sc_heartbeat.c 心跳监控 SWC（SWR-SC-004/005/006/027/028，ASIL D）的
  端到端测试：三路独立 ECU（CVC/FZC/RZC）心跳超时检测（150 tick）、确认
  窗口（20 tick）锁存、3 心跳恢复去抖、启动宽限期（1500 tick）、故障 LED
  驱动，以及内容校验（DEGRADED/LIMP 累计 100、FaultStatus≥2 bit 累计 20）。

  背景:
    假如存在:
      """
      ScHeartbeatSetup: {
        phases: []
      }
      """

  规则: 初始化与启动宽限期 — SC_Heartbeat_Init / Monitor

    Init 清零全部计数与标志并置宽限计数器 1500；宽限期内 Monitor 直接返回，
    不递增任何计数器；宽限结束后每 tick 递增三路独立计数器。

    场景: 初始化后全部计数器清零且宽限期就绪
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: {
          counters: [0, 0, 0]
          timedOut: [0, 0, 0]
          confirmed: [0, 0, 0]
          recoveryCounts: [0, 0, 0]
          confirmCounters: [0, 0, 0]
          stuckDegraded: [0, 0, 0]
          faultEscalate: [0, 0, 0]
          contentFault: [0, 0, 0]
          lastFaultStatus: [0, 0, 0]
          leds: [0, 0, 0]
          startupGrace: 1500
          anyConfirmed: 0
          fzcBrakeFault: 0
        }
      }
      """

    场景: 启动宽限期内不误报超时
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1499 },
          { "op": "monitor", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [0, 0, 0]
        timedOut: [0, 0, 0]
        startupGrace: 0
      }
      """

    场景: 宽限结束后计数器开始递增
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [1, 1, 1]
        timedOut: [0, 0, 0]
      }
      """

  规则: 超时检测 — SWR-SC-004 / SWR-SC-005

    宽限后每 tick 三路计数器独立递增；达到 150 tick 时置超时标志并点亮
    对应故障 LED；超时前保持未超时且 LED 熄灭。

    场景: 149 tick 未达超时阈值
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 149 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [149, 149, 149]
        timedOut: [0, 0, 0]
        leds: [0, 0, 0]
      }
      """

    场景: 150 tick 触发超时并点亮故障 LED
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [150, 150, 150]
        timedOut: [1, 1, 1]
        confirmed: [0, 0, 0]
        leds: [1, 1, 1]
        anyConfirmed: 0
      }
      """

    场景: NotifyRx 复位计数器后 49 tick 不超时
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 100 },
          { "op": "notifyRx", "ecu": 0 },
          { "op": "monitor", "ticks": 49 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4].state: {
        counters: [49, 149, 149]
        timedOut: [0, 0, 0]
      }
      """

    场景: 越界 ECU 的 NotifyRx 被忽略
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "notifyRx", "ecu": 3 },
          { "op": "notifyRx", "ecu": 255 },
          { "op": "monitor", "ticks": 150 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4].state: {
        timedOut: [1, 1, 1]
      }
      """

    场景: 独立 ECU 超时 — 仅 CVC 无心跳
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150, "notifyA": 1, "notifyB": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [150, 1, 1]
        timedOut: [1, 0, 0]
        leds: [1, 0, 0]
      }
      """

    场景: 确认锁存后计数器停止递增（防御守卫）
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 65535 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        counters: [170, 170, 170]
        confirmed: [1, 1, 1]
        anyConfirmed: 1
      }
      """

  规则: 确认窗口与锁存 — SWR-SC-006

    超时后再经 20 tick 确认锁存；锁存后 NotifyRx 无法恢复（仅断电复位）；
    确认窗口内收到 3 个连续心跳可取消超时。

    场景: 超时后 19 tick 未确认
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150 },
          { "op": "monitor", "ticks": 19 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        timedOut: [1, 1, 1]
        confirmed: [0, 0, 0]
        confirmCounters: [19, 19, 19]
        anyConfirmed: 0
      }
      """

    场景: 20 tick 确认锁存
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150 },
          { "op": "monitor", "ticks": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        confirmed: [1, 1, 1]
        confirmCounters: [20, 20, 20]
        anyConfirmed: 1
      }
      """

    场景: 确认后 NotifyRx 无法恢复（锁存）
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 170 },
          { "op": "notifyRx", "ecu": 0, "repeats": 6 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        confirmed: [1, 1, 1]
        timedOut: [1, 1, 1]
        anyConfirmed: 1
      }
      """

    场景: 2 个心跳不足以恢复超时
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150 },
          { "op": "notifyRx", "ecu": 0, "repeats": 2 },
          { "op": "monitor", "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4].state: {
        timedOut: [1, 1, 1]
        recoveryCounts: [2, 0, 0]
        counters: [1, 151, 151]
      }
      """

    场景: 3 个连续心跳恢复超时并熄灭 LED
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "monitor", "ticks": 1500 },
          { "op": "monitor", "ticks": 150 },
          { "op": "notifyRx", "ecu": 0, "repeats": 3 },
          { "op": "monitor", "ticks": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4].state: {
        timedOut: [0, 1, 1]
        leds: [0, 1, 1]
        confirmed: [0, 1, 1]
      }
      """

  规则: 内容校验 — SWR-SC-027 / SWR-SC-028

    ValidateContent 解析 heartbeat byte3（低 4 位 OperatingMode、高 4 位
    FaultStatus）；DEGRADED(2)/LIMP(3) 模式累计 stuckDegraded（阈值 100）、
    FaultStatus≥2 bit 累计 faultEscalate（阈值 20），任一超限锁存内容故障。
    计数为 uint8，达 0xFF 后饱和。

    场景: DEGRADED 模式累计 99 次未锁存内容故障
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 0, "payload3": 2, "repeats": 99 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        stuckDegraded: [99, 0, 0]
        contentFault: [0, 0, 0]
      }
      """

    场景: DEGRADED 模式累计 100 次锁存内容故障
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 0, "payload3": 2, "repeats": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        stuckDegraded: [100, 0, 0]
        contentFault: [1, 0, 0]
      }
      """

    场景: DEGRADED 累计计数 0xFF 饱和
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 0, "payload3": 2, "repeats": 300 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        stuckDegraded: [255, 0, 0]
        contentFault: [1, 0, 0]
      }
      """

    场景: NORMAL 模式清零 DEGRADED 累计
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 0, "payload3": 2, "repeats": 10 },
          { "op": "validate", "ecu": 0, "payload3": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        stuckDegraded: [0, 0, 0]
      }
      """

    场景: 双故障位累计 19 次未锁存内容故障
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 1, "payload3": 48, "repeats": 19 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        faultEscalate: [0, 19, 0]
        contentFault: [0, 0, 0]
      }
      """

    场景: 双故障位累计 20 次锁存内容故障
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 1, "payload3": 48, "repeats": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        faultEscalate: [0, 20, 0]
        contentFault: [0, 1, 0]
      }
      """

    场景: 双故障位累计计数 0xFF 饱和
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 1, "payload3": 48, "repeats": 300 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        faultEscalate: [0, 255, 0]
        contentFault: [0, 1, 0]
      }
      """

    场景: 单故障位清零升级累计
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 1, "payload3": 48, "repeats": 5 },
          { "op": "validate", "ecu": 1, "payload3": 16 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faultEscalate: [0, 0, 0]
      }
      """

    场景: 四个故障位全部置位被解析
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 2, "payload3": 240 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        lastFaultStatus: [0, 0, 15]
        faultEscalate: [0, 0, 1]
      }
      """

    场景: LIMP 模式累计降级计数
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 2, "payload3": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        stuckDegraded: [0, 0, 1]
      }
      """

    场景: 越界 ECU 的 validate 被忽略
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 3, "payload3": 48, "repeats": 100 },
          { "op": "validate", "ecu": 255, "payload3": 48, "repeats": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faultEscalate: [0, 0, 0]
        contentFault: [0, 0, 0]
        lastFaultStatus: [0, 0, 0]
      }
      """

    场景: FZC 制动故障位被检测
      当POST "/api/test/asw/sc/heartbeat":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "validate", "ecu": 1, "payload3": 32 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        lastFaultStatus: [0, 2, 0]
        fzcBrakeFault: 1
      }
      """
