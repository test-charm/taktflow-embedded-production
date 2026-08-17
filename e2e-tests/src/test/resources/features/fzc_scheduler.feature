# language: zh-CN
功能: FZC 可运行实体调度表 (Swc_FzcScheduler)

  Swc_FzcScheduler 可运行实体配置表 SWC 的端到端测试：通过 Swc_FzcScheduler_Init
  置位初始化标志（SWR-FZC-029 生产配置：7 项，索引 0-6，含周期/优先级/WCET/ASIL），
  未初始化时 GetTable 返回 NULL（fail-closed）、GetCount 恒返回 7，重复 Init 幂等，
  以及配置表数据正确性（安全任务优先级高于 QM 任务、总 WCET 未超 10ms 周期 80%
  利用率上限）。

  背景:
    假如存在:
      """
      FzcSchedulerSetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_FzcScheduler_Init / GetTable

    有效 Init 后配置就绪（GetTable 返回 SWR-FZC-029 内部静态表、GetCount 返回
    表项数 7）；未初始化时 GetTable 返回 NULL（SWC 保持 fail-closed），但 GetCount
    恒返回 7（编译期常量，无守卫）。重复 Init 幂等，状态保持就绪。

    场景: 生产配置表初始化后配置就绪
      当POST "/api/test/asw/fzc/scheduler":
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
        runnableCount: 7
        safetyPriorityOk: 1
        wcetWithinCycle: 1
      }
      """

    场景: 未初始化时查询表为空但计数为 7
      当POST "/api/test/asw/fzc/scheduler":
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
        tableMatches: 0
        runnableCount: 7
        safetyPriorityOk: 0
        wcetWithinCycle: 0
      }
      """

    场景: 重复初始化保持就绪
      当POST "/api/test/asw/fzc/scheduler":
      """
      {
        "phases": [
          { "reinit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        tableMatches: 1
        runnableCount: 7
      }
      """

  规则: 生产配置表数据正确性（SWR-FZC-029）

    通过 GetTable 读回的配置表须满足：安全任务（ASIL ≥ C）优先级均高于 QM 任务
    优先级；所有可运行实体的 WCET 预算总和（2900us）须小于 10ms 周期的 80% 上限
    （8000us）。表中各可运行实体的 name / periodMs / priority / wcetUs / asilLevel
    与 SWR-FZC-029 生产配置一致。

    场景: 读回的生产表项与 SWR-FZC-029 一致
      当POST "/api/test/asw/fzc/scheduler":
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
        safetyPriorityOk: 1
        wcetWithinCycle: 1
        runnables: | index | periodMs | priority | wcetUs | asilLevel |
                   | 0     | 10       | 3        | 800    | 4         |
                   | 1     | 10       | 3        | 600    | 4         |
                   | 2     | 10       | 3        | 500    | 3         |
                   | 3     | 10       | 3        | 400    | 4         |
                   | 4     | 50       | 2        | 200    | 2         |
                   | 5     | 100      | 3        | 100    | 4         |
                   | 6     | 10       | 1        | 300    | 0         |
      }
      """

    场景: 安全任务优先级高于 QM 任务优先级
      当POST "/api/test/asw/fzc/scheduler":
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
        safetyPriorityOk: 1
        runnables: | priority |
                   | 3        |
                   | 3        |
                   | 3        |
                   | 3        |
                   | 2        |
                   | 3        |
                   | 1        |
      }
      """

    场景: 总 WCET 未超 10ms 周期 80% 上限
      当POST "/api/test/asw/fzc/scheduler":
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
        wcetTotalUs: 2900
        wcetWithinCycle: 1
      }
      """
