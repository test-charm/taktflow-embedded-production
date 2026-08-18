# language: zh-CN
功能: SC 外部看门狗喂狗控制 (sc_watchdog)

  sc_watchdog.c 外部看门狗（TPS3823）喂狗控制模块（SWR-SC-022，ASIL D）的
  端到端测试：Init 置 WDI 引脚 LOW、全部监控条件满足（allChecksOk==TRUE）
  时每次 Feed 翻转一次 WDI 引脚（0→1→0→1…）、任一条件失败（FALSE）时
  Feed 不写 WDI 引脚（看门狗饿死，TPS3823 超时后复位 MCU）、失败恢复后
  继续翻转。harness 以生产 TMS570 配置编译（无 PLATFORM_POSIX/HIL），
  喂狗门控语义严格生效。

  harness 启动时自动执行一次 SC_Watchdog_Init（模拟上电启动），因此
  wdiWriteCount 从 1 开始累计；每次成功喂狗（TRUE）追加 1 次 WDI 引脚
  写入，失败喂狗（FALSE）不追加。wdiPin 为 mock GIO 观测的 WDI 引脚
  当前电平。

  背景:
    假如存在:
      """
      ScWatchdogSetup: {
        phases: []
      }
      """

  规则: 初始化 — SC_Watchdog_Init

    Init 置 wdi_state=0 并写 WDI 引脚 LOW（安全态）。重复 Init 复位
    wdi_state 并重新写 LOW。

    场景: 初始化后 WDI 引脚为 LOW
      当POST "/api/test/asw/sc/watchdog":
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
        wdiPin: 0
        wdiWriteCount: 2
      }
      """

  规则: 喂狗成功 — allChecksOk==TRUE

    全部条件满足时，每次 Feed 将 WDI 引脚翻转一次（wdi_state 异或 1），
    并写回引脚；每次成功喂狗追加一次 WDI 引脚写入计数。

    场景: 单次成功喂狗翻转 WDI 为 HIGH
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        wdiPin: 1
        wdiWriteCount: 2
      }
      """

    场景: 两次成功喂狗翻转回 LOW
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        wdiPin: 0
        wdiWriteCount: 3
      }
      """

    场景: 三次成功喂狗再翻转为 HIGH
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1, "repeats": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        wdiPin: 1
        wdiWriteCount: 4
      }
      """

    场景: 连续 100 次成功喂狗偶数次回 LOW
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1, "repeats": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        wdiPin: 0
        wdiWriteCount: 101
      }
      """

  规则: 喂狗失败饿死 — allChecksOk==FALSE

    任一条件失败时 Feed 直接返回，不写 WDI 引脚（不翻转、不追加写入
    计数），看门狗保持当前电平直至 TPS3823 超时复位 MCU。

    场景: 初始失败时 WDI 保持 LOW
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 0, "repeats": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0].state: {
        wdiPin: 0
        wdiWriteCount: 1
      }
      """

    场景: 成功喂狗后失败保持 HIGH
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1 },
          { "op": "feed", "ok": 0, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        wdiPin: 1
        wdiWriteCount: 2
      }
      """

  规则: 交替成功/失败 — 保持最后成功状态

    失败只饿死看门狗，不改变 wdi_state；随后再次成功时在最后成功电平
    基础上继续翻转。

    场景: 交替成功失败后继续翻转
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1 },
          { "op": "feed", "ok": 0 },
          { "op": "feed", "ok": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].state.wdiPin: 1
        results[1].state.wdiPin: 1
        results[2].state.wdiPin: 0
      }
      """

  规则: 失败恢复 — SWR-SC-022

    长时间失败（看门狗饿死）后，条件恢复的第一个成功喂狗在最后电平
    基础上继续翻转。

    场景: 长时间失败后恢复喂狗继续翻转
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 0, "repeats": 10 },
          { "op": "feed", "ok": 1 },
          { "op": "feed", "ok": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].state.wdiPin: 0
        results[0].state.wdiWriteCount: 1
        results[1].state.wdiPin: 1
        results[2].state.wdiPin: 0
      }
      """

  规则: 重复 Init — 复位喂狗状态

    重复 Init 将 wdi_state 复位为 0 并重新写 WDI 引脚 LOW。

    场景: 重复 Init 复位 WDI 为 LOW
      当POST "/api/test/asw/sc/watchdog":
      """
      {
        "phases": [
          { "op": "feed", "ok": 1 },
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].state.wdiPin: 1
        results[1].state.wdiPin: 0
        results[1].state.wdiWriteCount: 3
      }
      """
