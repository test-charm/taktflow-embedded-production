# language: zh-CN
功能: CVC 可运行实体调度表 (Swc_Scheduler)

  Swc_Scheduler 可运行实体配置表 SWC 的端到端测试：通过 Swc_Scheduler_Init
  装载可运行实体表（SWR-CVC-032 生产配置：8 项，ID 0-7，含周期/优先级/WCET/
  ASIL），NULL 配置 / 空 runnables / 零计数三种守卫拒绝初始化，未初始化时
  GetConfig 返回 NULL、GetRunnableCount 返回 0，重复 Init 替换配置表，以及
  配置表数据正确性（安全任务优先级高于 QM 任务、总 WCET 在最短周期内）。

  背景:
    假如存在:
      """
      CvcSchedulerSetup: {
        phases: []
      }
      """

  规则: 初始化 — Swc_Scheduler_Init

    有效配置（runnables 非空且 runnableCount > 0）初始化后配置就绪
    （GetConfig 返回所传配置指针、GetRunnableCount 返回表项数）；NULL 配置、
    空 runnables 或零计数均拒绝初始化（SWC 保持未初始化）。

    场景: 生产配置表初始化后配置就绪
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        configMatches: 1
        runnableCount: 8
        safetyPriorityOk: 1
        wcetWithinCycle: 1
      }
      """

    场景: 最小表（1 项）初始化后计数为 1
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        configMatches: 1
        runnableCount: 1
      }
      """

    场景: 最大表（16 项）初始化后计数为 16
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        configMatches: 1
        runnableCount: 16
      }
      """

    场景: NULL 配置初始化被拒绝
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "initNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        runnableCount: 0
        safetyPriorityOk: 0
        wcetWithinCycle: 0
      }
      """

    场景: runnables 为空指针时初始化被拒绝
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "nullRunnables": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        runnableCount: 0
        safetyPriorityOk: 0
        wcetWithinCycle: 0
      }
      """

    场景: runnableCount 为 0 时初始化被拒绝
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "zeroCount": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        runnableCount: 0
        safetyPriorityOk: 0
        wcetWithinCycle: 0
      }
      """

    场景: 未初始化时查询配置与计数为空
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        runnableCount: 0
        safetyPriorityOk: 0
        wcetWithinCycle: 0
      }
      """

    场景: 失败初始化后有效配置可恢复
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "initNull": true },
          { "tableIndex": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        configMatches: 1
        runnableCount: 8
      }
      """

  规则: 重复初始化 — 配置替换

    重复调用 Swc_Scheduler_Init 会用新配置替换已存储的配置指针；
    GetConfig 返回最后一次 Init 传入的配置，GetRunnableCount 返回其表项数。

    场景: 重复初始化替换为最小表
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 0 },
          { "tableIndex": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        configMatches: 1
        runnableCount: 1
      }
      """

    场景: NULL 重新初始化清除已存储配置
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 0 },
          { "initNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        runnableCount: 0
      }
      """

  规则: 生产配置表数据正确性（SWR-CVC-032）

    通过 GetConfig 读回的配置表须满足：安全任务（ASIL ≥ B）优先级均高于
    QM 任务优先级；所有可运行实体的 WCET 预算总和须小于最短周期（10ms）。
    表中各可运行实体的 runnableId / periodMs / priority / wcetUs / asilLevel
    与 SWR-CVC-032 生产配置一致。

    场景: 读回的生产表项与 SWR-CVC-032 一致
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        safetyPriorityOk: 1
        wcetWithinCycle: 1
        runnables: | runnableId | periodMs | priority | wcetUs | asilLevel |
                   | 0          | 10       | 10       | 200    | 4         |
                   | ***                                                   |
                   | ***                                                   |
                   | 3          | 50       | 8        | 150    | 2         |
                   | 4          | 200      | 3        | 500    | 0         |
                   | ***                                                   |
                   | ***                                                   |
                   | 7          | 10       | 10       | 200    | 4         |
      }
      """

    场景: 安全任务优先级高于 QM 任务优先级
      当POST "/api/test/asw/cvc/scheduler":
      """
      {
        "phases": [
          { "tableIndex": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        safetyPriorityOk: 1
        runnables: | priority |
                   | *        |
                   | *        |
                   | 11       |
                   | 8        |
                   | 3        |
                   | *        |
                   | *        |
                   | *        |
      }
      """
