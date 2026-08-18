# language: zh-CN
功能: RZC 可运行实体调度表 (Swc_RzcScheduler)

  Swc_RzcScheduler 可运行实体配置表 SWC 的端到端测试：通过
  Swc_RzcScheduler_Init 置位初始化标志并复位 8 个可运行实体的 elapsed 计数器
  （SWR-RZC-028 生产配置：8 项，索引 0-7，含周期/优先级/WCET），未初始化时
  Tick 直接返回（fail-closed）且 GetTable 恒返回内部表（无守卫，与 FZC 的
  NULL 守卫不同）、GetUtilPct 恒可计算，重复 Init 幂等，以及 Tick 周期调度：
  CurrentMonitor(1ms) 每 tick 触发、Motor/Encoder/CanReceive(10ms) 每 10 tick、
  HeartbeatTx(50ms) 每 50 tick、Temp/Battery/Watchdog(100ms) 每 100 tick，
  总 WCET 利用率未超 80% 上限。

  背景:
    假如存在:
      """
      RzcSchedulerSetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_RzcScheduler_Init / GetTable

    有效 Init 后初始化就绪（GetInitialized 返回 1、GetTable 返回 SWR-RZC-028
    内部静态表、表项匹配）；未初始化时 GetInitialized 返回 0，但 GetTable 仍
    返回内部表（本模块 GetTable 无未初始化守卫，恒返回静态表，与 FZC 不同），
    Tick 直接返回不调度任何可运行实体（fail-closed）。重复 Init 幂等。

    场景: 生产配置表初始化后配置就绪
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          {}
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        tableMatches: 1
        runnableCount: 8
        priorityOrdered: 1
        utilUnderMax: 1
      }
      """

    场景: 未初始化时 Tick 不调度且表仍可读
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "skipInit": true, "ticks": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        tableMatches: 1
        runnableCount: 8
        ticks: 10
        dispatchTotal: 0
        currentMonitor: 0
        motor: 0
        encoder: 0
        comReceive: 0
        temp: 0
        battery: 0
        heartbeat: 0
        wdgm: 0
      }
      """

    场景: 重复初始化保持就绪且复位 elapsed
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "reinit": true, "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        tableMatches: 1
        runnableCount: 8
        ticks: 1
        currentMonitor: 1
        motor: 0
        encoder: 0
        comReceive: 0
      }
      """

  规则: 生产配置表数据正确性（SWR-RZC-028）

    通过 GetTable 读回的配置表须满足：优先级按 1(最高)→3(低) 非递减排序
    （安全任务 priority 1/2 高于 QM 任务 priority 3，即优先抢占）；总 WCET
    利用率 Swc_RzcScheduler_GetUtilPct 返回 10%（远低于 80% 上限）。表中各
    可运行实体的 periodMs / priority / wcetUs 与 SWR-RZC-028 生产配置一致。

    场景: 读回的生产表项与 SWR-RZC-028 一致
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          {}
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        tableMatches: 1
        runnableCount: 8
        runnables: | index | periodMs | priority | wcetUs |
                   | 0     | 1        | 1        | 50     |
                   | 1     | 10       | 2        | 200    |
                   | 2     | 10       | 2        | 100    |
                   | 3     | 10       | 2        | 150    |
                   | 4     | 100      | 3        | 300    |
                   | 5     | 100      | 3        | 200    |
                   | 6     | 50       | 3        | 100    |
                   | 7     | 100      | 3        | 50     |
      }
      """

    场景: 安全任务优先级高于 QM 任务优先级
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          {}
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        priorityOrdered: 1
        runnables: | priority |
                   | 1        |
                   | 2        |
                   | 2        |
                   | 2        |
                   | 3        |
                   | 3        |
                   | 3        |
                   | 3        |
      }
      """

    场景: 总 WCET 利用率未超 80% 上限
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          {}
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        utilPct: 10
        utilUnderMax: 1
      }
      """

  规则: Tick 周期调度（elapsed 计数器与分派）

    Swc_RzcScheduler_Tick 对每个可运行实体递增 elapsed 计数器，当累计值达到
    该实体的 period_ms 时复位计数器并调用其入口函数。CurrentMonitor(1ms)
    每次 tick 触发；Motor / Encoder / CanReceive(10ms) 每 10 个 tick 触发一次；
    HeartbeatTx(50ms) 每 50 个 tick 触发一次；Temp / Battery / Watchdog
    (100ms) 每 100 个 tick 触发一次。elapsed 在 Init / reinit 时复位为 0。

    场景: 单次 Tick 仅 CurrentMonitor（1ms）触发
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        ticks: 1
        dispatchTotal: 1
        currentMonitor: 1
        motor: 0
        encoder: 0
        comReceive: 0
        temp: 0
        battery: 0
        heartbeat: 0
        wdgm: 0
      }
      """

    场景: 10ms 周期实体在第 10 个 Tick 各触发一次
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "ticks": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        ticks: 10
        dispatchTotal: 13
        currentMonitor: 10
        motor: 1
        encoder: 1
        comReceive: 1
        temp: 0
        battery: 0
        heartbeat: 0
        wdgm: 0
      }
      """

    场景: 100ms 完整周期内所有可运行实体按周期触发
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "ticks": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        ticks: 100
        dispatchTotal: 135
        currentMonitor: 100
        motor: 10
        encoder: 10
        comReceive: 10
        temp: 1
        battery: 1
        heartbeat: 2
        wdgm: 1
      }
      """

    场景: 前置 phase 后再 reinit 复位 elapsed 计数
      假如存在:
        """
        RzcSchedulerSetup: {
          phases: [
            { ticks: 10 }
          ]
        }
        """
      当POST "/api/test/asw/rzc/scheduler":
      """
      {
        "phases": [
          { "reinit": true, "ticks": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        ticks: 11
        currentMonitor: 11
        motor: 1
        encoder: 1
        comReceive: 1
        heartbeat: 0
        wdgm: 0
      }
      """
