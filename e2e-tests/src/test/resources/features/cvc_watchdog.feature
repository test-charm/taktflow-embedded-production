# language: zh-CN
功能: CVC 看门狗 (Swc_Watchdog)

  Swc_Watchdog 外部看门狗（TPS3823 WDI）喂狗 SWC 的端到端测试：四条件门控
  （主循环完成 / 栈金丝雀完好 / RAM 模式测试通过 / CAN 未 bus-off）全部满足
  才翻转 WDI 引脚喂狗；任一条件失败即拒绝喂狗（WDI 不翻转），以及 NULL 配置
  与未初始化守卫。

  背景:
    假如存在:
      """
      CvcWatchdogSetup: {
        phases: []
      }
      """

  规则: 初始化 — Swc_Watchdog_Init

    有效配置初始化后内部就绪（Initialized=TRUE、FeedCount=0）；NULL 配置或
    跳过初始化使 SWC 保持未初始化，Feed 直接拒绝。

    场景: 有效配置初始化后内部状态就绪
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "feedCount": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        feedCount: 0
        dioFlipCount: 0
      }
      """

    场景: NULL 配置初始化使 SWC 未初始化
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "initNull": true, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        feedResult: 1
        dioFlipCount: 0
      }
      """

    场景: 未初始化时喂狗被拒绝
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "skipInit": true, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        feedResult: 1
        dioFlipCount: 0
      }
      """

  规则: 四条件喂狗门控 — Swc_Watchdog_Feed

    主循环完成（loopComplete）、栈金丝雀完好（canaryOk）、RAM 模式测试通过
    （ramOk）、CAN 未 bus-off（canOk）四个条件全部为 TRUE 时才翻转 WDI 引脚
    喂狗；任一条件为 FALSE 立即拒绝，WDI 不翻转，喂狗计数不累加。

    场景: 四个条件全部满足时喂狗并翻转 WDI
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 0
        feedCount: 1
        dioFlipCount: 1
        dioLastChannel: 6
      }
      """

    场景: 主循环未完成时拒绝喂狗
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "loopComplete": false, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 1
        feedCount: 0
        dioFlipCount: 0
      }
      """

    场景: 栈金丝雀损坏时拒绝喂狗
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "canaryOk": false, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 1
        feedCount: 0
        dioFlipCount: 0
      }
      """

    场景: RAM 模式测试失败时拒绝喂狗
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "ramOk": false, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 1
        feedCount: 0
        dioFlipCount: 0
      }
      """

    场景: CAN 总线 bus-off 时拒绝喂狗
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "canOk": false, "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 1
        feedCount: 0
        dioFlipCount: 0
      }
      """

    场景: 连续喂狗次数累加且每次翻转 WDI
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "feedCount": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 0
        feedCount: 3
        dioFlipCount: 3
      }
      """

    场景: 单条件失败后条件恢复可继续喂狗
      假如存在:
        """
        CvcWatchdogSetup: {
          phases: [
            { "feedCount": 1 }
          ]
        }
        """
      当POST "/api/test/asw/cvc/watchdog":
      """
      {
        "phases": [
          { "loopComplete": false, "feedCount": 1 },
          { "feedCount": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        feedResult: 0
        feedCount: 2
        dioFlipCount: 2
      }
      """
