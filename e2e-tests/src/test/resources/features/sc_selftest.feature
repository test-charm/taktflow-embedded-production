# language: zh-CN
功能: SC 启动/运行时自检 (sc_selftest)

  sc_selftest.c 启动/运行时自检模块（SWR-SC-016/017/018/019/020/021，
  ASIL D）的端到端测试：7 步启动 BIST（lockstep、RAM PBIST、Flash CRC-32、
  DCAN 回环、GPIO 读回、灯测试、看门狗测试）任一步失败立即返回对应步骤号
  1..7 并阻断后续步骤；运行期 60s 周期分步检查（Flash CRC 增量 / RAM 32 字节
  模式 / DCAN 错误状态 / GIO 继电器读回），任一失败使运行期不健康且锁存；
  堆栈金丝雀 SWR-SC-021 完整性；IsHealthy 仅在启动通过且运行期健康时成立。
  harness 以生产 TMS570 配置编译（无 PLATFORM_POSIX/HIL），硬件诊断经
  mock 注入并按调用计数，UNIT_TEST 钩子观测内部 tick/健康标志并注入金丝雀
  与 RAM 模式损坏以驱动失败分支。

  背景:
    假如存在:
      """
      ScSelfTestSetup: {
        phases: []
      }
      """

  规则: 初始化与堆栈金丝雀 — SWR-SC-021

    SC_SelfTest_Init 植入金丝雀值 0xDEADBEEF、写入 RAM 交替模式（0xAA/0x55）
    并复位状态标志；SC_SelfTest_StackCanaryOk 在金丝雀被破坏时返回 FALSE。

    场景: 初始化后金丝雀正确且未启动健康为假
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: init
        canaryOk: 1
        healthy: 0
        tick: 0
      }
      """

    场景: 金丝雀损坏后检查失败
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "canary", "corruptCanary": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].canaryOk: 1
        results[1]: {
          op: canary
          canaryOk: 0
        }
      }
      """

  规则: 启动自检全通过 — SWR-SC-019

    7 项硬件诊断全部通过时返回 0 并将 startup_passed 置 TRUE，
    IsHealthy 成立；每项诊断恰被调用一次。

    场景: 七项启动自检全部通过
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: startup
        result: 0
        healthy: 1
        startupPassed: 1
        lockstepCalls: 1
        ramPbistCalls: 1
        flashCrcCalls: 1
        dcanLoopbackCalls: 1
        gpioReadbackCalls: 1
        lampCalls: 1
        watchdogCalls: 1
      }
      """

    场景: 重复启动自检幂等
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "startup" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[0].result: 0
        results[1].result: 0
        state.healthy: 1
        state.lockstepCalls: 2
      }
      """

  规则: 启动自检失败 — 返回步骤号并阻断后续步骤

    任一步失败立即置 startup_passed=FALSE 并返回对应步骤号 1..7，
    后续步骤不再执行（mock 调用计数证明）。

    场景: 第一步 lockstep BIST 失败返回 1 并阻断后续
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b1": 0, "b7": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 1
        healthy: 0
        startupPassed: 0
        lockstepCalls: 1
        ramPbistCalls: 0
        flashCrcCalls: 0
        dcanLoopbackCalls: 0
        gpioReadbackCalls: 0
        lampCalls: 0
        watchdogCalls: 0
      }
      """

    场景: 第二步 RAM PBIST 失败返回 2
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b2": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 2
        healthy: 0
        ramPbistCalls: 1
        flashCrcCalls: 0
      }
      """

    场景: 第三步 Flash CRC-32 失败返回 3
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b3": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 3
        healthy: 0
        flashCrcCalls: 1
        dcanLoopbackCalls: 0
      }
      """

    场景: 第四步 DCAN 回环失败返回 4
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b4": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 4
        healthy: 0
        dcanLoopbackCalls: 1
        gpioReadbackCalls: 0
      }
      """

    场景: 第五步 GPIO 读回失败返回 5
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b5": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 5
        healthy: 0
        gpioReadbackCalls: 1
        lampCalls: 0
      }
      """

    场景: 第六步 灯测试失败返回 6
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b6": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 6
        healthy: 0
        lampCalls: 1
        watchdogCalls: 0
      }
      """

    场景: 第七步 看门狗测试失败返回 7
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b7": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        result: 7
        healthy: 0
        startupPassed: 0
        watchdogCalls: 1
      }
      """

  规则: 运行期周期分步 — SWR-SC-020

    启动通过后每次 SC_SelfTest_Runtime 递增 tick；tick 1 执行 Flash CRC
    增量、1500 执行 RAM 32 字节模式校验、3000 执行 DCAN 错误状态检查、
    4500 执行 GIO 继电器读回（非关键、仅读取）、非步骤 tick 直接返回；
    tick 到达 6000 后回绕重新从 0 开始。

    场景: tick 1 执行 Flash CRC 增量步骤
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: runtime
        tick: 1
        healthy: 1
        startupPassed: 1
        runtimeHealthy: 1
      }
      """

    场景: tick 1500 执行 RAM 模式校验并保持健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 1500 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 1500
        healthy: 1
        runtimeHealthy: 1
      }
      """

    场景: tick 3000 执行 DCAN 错误状态检查
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 3000 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 3000
        healthy: 1
        runtimeHealthy: 1
      }
      """

    场景: tick 4500 执行 GIO 继电器读回步骤
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 4500, "readback": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 4500
        healthy: 1
        runtimeHealthy: 1
      }
      """

    场景: 非步骤 tick 直接返回不改变健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 10
        healthy: 1
        runtimeHealthy: 1
      }
      """

    场景: tick 6000 周期回绕后重新从第一步开始
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 6001 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[1].tick: 1
        results[1].healthy: 1
        state.flashIncrCalls: 2
      }
      """

  规则: 运行期失败 — 锁存不健康

    Flash CRC 增量（tick 1）/ RAM 模式校验（tick 1500）/ DCAN 错误状态
    （tick 3000）任一失败即置 runtime_healthy=FALSE 并锁存，IsHealthy 不再
    成立。

    场景: 运行期 Flash CRC 增量失败导致不健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 1, "flashIncr": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 1
        healthy: 0
        startupPassed: 1
        runtimeHealthy: 0
      }
      """

    场景: 运行期 RAM 模式校验失败导致不健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 1500, "corruptRam": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 1500
        healthy: 0
        startupPassed: 1
        runtimeHealthy: 0
      }
      """

    场景: 运行期 DCAN 错误状态检查失败导致不健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "runtime", "repeats": 3000, "dcanErr": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 3000
        healthy: 0
        startupPassed: 1
        runtimeHealthy: 0
      }
      """

  规则: 启动未完成即运行 — fail-closed

    启动自检未通过时运行期直接置 runtime_healthy=FALSE 并返回（不递增
    tick、不执行任何硬件检查）。

    场景: 启动未完成时运行期直接不健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "runtime", "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        tick: 0
        healthy: 0
        startupPassed: 0
        runtimeHealthy: 0
      }
      """

  规则: 健康状态组合与复位 — SWR-SC-016/017

    IsHealthy 仅在 startup_passed 与 runtime_healthy 同时成立时返回 TRUE；
    启动失败后运行期也进入不健康；重复 Init 复位全部状态标志。

    场景: 启动失败后运行期进入不健康
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup", "b1": 0 },
          { "op": "runtime", "repeats": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        tick: 0
        healthy: 0
        startupPassed: 0
        runtimeHealthy: 0
      }
      """

    场景: 重复 Init 复位自检状态
      当POST "/api/test/asw/sc/selftest":
      """
      {
        "phases": [
          { "op": "startup" },
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: init
        canaryOk: 1
        healthy: 0
        tick: 0
      }
      """
