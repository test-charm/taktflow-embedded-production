# language: zh-CN
功能: SC 扭矩-电流合理性校验 (sc_plausibility)

  sc_plausibility.c 扭矩-电流交叉合理性校验模块（SWR-SC-007/008/009/024，
  SSR-SC-018，ASIL D）的端到端测试：16 项 LUT 线性插值求期望电流（SWR-SC-007）、
  20% 相对 / 2000mA 绝对阈值 + 绝对下限的合理性比较（SWR-SC-008）、10-tick
  去抖后故障锁存与系统 LED 点亮（SWR-SC-009）、FZC 制动故障 + 电流 >1000mA
  的 10-tick 备份切断（SWR-SC-024）、以及「零扭矩 + 电流 >500mA」2 周期后
  **不可清除**锁存的蠕动防护（SSR-SC-018）。共享 1500-tick 启动宽限。
  harness 以生产 TMS570 逻辑编译（无 PLATFORM_POSIX/HIL），蠕动用例严格
  2 周期阈值生效；宽限/去抖常量按 harness 编译配置（1500 宽限 / 10 tick
  去抖，与 sc_heartbeat harness 一致）。

  背景:
    假如存在:
      """
      ScPlausibilitySetup: {
        phases: []
      }
      """

  规则: 初始化 — SC_Plausibility_Init

    Init 清零 debounce/backup_cutoff/creep_debounce 与故障锁存，置共享
    启动宽限 1500，系统 LED 熄灭。

    场景: 初始化后全部状态清零且宽限就绪
      当POST "/api/test/asw/sc/plausibility":
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
        faulted: 0
        creepFaulted: 0
        debounce: 0
        backupCutoff: 0
        startupGrace: 1500
        creepDebounce: 0
        ledSys: 0
      }
      """

  规则: 扭矩-电流查找表 — SWR-SC-007

    16 项 LUT（0..100%）线性插值：torque==0 → 0mA；torque>=100 → 25000mA
    （含 255 钳位）；0<torque<100 在相邻表项间插值。

    场景: 零扭矩期望零电流
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 0
      }
      """

    场景: 满扭矩期望满电流（上限边界）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 25000
      }
      """

    场景: 超上限扭矩被钳位到满电流
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 255 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 25000
      }
      """

    场景: LUT 表项精确命中（7% → 1750mA）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 7 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 1750
      }
      """

    场景: 中间扭矩线性插值（50% → 12500mA）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 50 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 12500
      }
      """

    场景: 首区间插值（1% → 250mA）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 250
      }
      """

    场景: 末区间插值（99% → 24750mA）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "lookup", "torque": 99 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: lookup
        expected: 24750
      }
      """

  规则: 合理性比较 — SWR-SC-008

    期望<100mA 用绝对阈值 2000mA；否则用相对阈值 20%（以 2000mA 为下限），
    绝对差超过阈值判定不合理。

    场景: 近零期望超绝对阈值判定不合理
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 0, "actual": 2500 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 1
      }
      """

    场景: 近零期望恰在绝对阈值边界判定合理
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 0, "actual": 2000 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 0
      }
      """

    场景: 相对阈值低于绝对下限时按下限判定（floor 之上）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 100, "actual": 2101 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 1
      }
      """

    场景: 相对阈值低于绝对下限时按下限判定（floor 边界）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 100, "actual": 2100 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 0
      }
      """

    场景: 满扭矩零电流判定不合理（相对阈值）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 25000, "actual": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 1
      }
      """

    场景: 相对阈值边界判定合理（20% 恰等）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 25000, "actual": 20000 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 0
      }
      """

    场景: 相对阈值边界之上判定不合理
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 25000, "actual": 19999 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 1
      }
      """

    场景: 实测高于期望的 diff 方向判定（actual>expected）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "implausible", "expected": 25000, "actual": 30000 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: implausible
        result: 0
      }
      """

  规则: 启动宽限期

    共享宽限计数器（1500 tick）内 Check/CreepGuard 直接返回，不读 CAN
    也不累计任何计数。

    场景: 宽限期内不合理数据不判故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        faulted: 0
        debounce: 0
        startupGrace: 1490
      }
      """

    场景: 宽限耗尽后计数归零
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state: {
        startupGrace: 0
        faulted: 0
      }
      """

  规则: 合理性去抖与故障锁存 — SWR-SC-008/009

    主检查需 veh_ok && cur_ok；不一致累计 debounce，达 10-tick 后置位
    faulted 并点亮系统 LED；一旦置位即锁存（Check 前置守卫直接返回），
    Init 可复位。

    场景: 去抖阈值之下不一致不判故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 9 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        debounce: 9
      }
      """

    场景: 去抖阈值达成后判故障并点亮 LED
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 1
        debounce: 10
        ledSys: 1
      }
      """

    场景: 回到合理数据重置去抖计数
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 3 },
          { "op": "check", "torque": 100, "current": 25000, "repeats": 1 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[4].state: {
        faulted: 0
        debounce: 3
      }
      """

    场景: 故障锁存后合理数据不清除
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 },
          { "op": "check", "torque": 100, "current": 25000, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[2].state.faulted: 1
        results[3].state.faulted: 1
      }
      """

    场景: 故障后 Check 前置守卫直接返回不再累计
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        faulted: 1
        debounce: 10
      }
      """

    场景: 重新 Init 清除故障锁存并复位宽限
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "repeats": 10 },
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3].state: {
        faulted: 0
        startupGrace: 1500
      }
      """

  规则: CAN 数据缺失（fail-safe）

    任一邮箱无效即跳过主检查；cur 无效同时跳过备份切断；不产生故障。

    场景: Vehicle_State 邮箱无效时不判故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "vehValid": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        debounce: 0
      }
      """

    场景: Motor_Current 邮箱无效时不判故障也不累计备份
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "curValid": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        backupCutoff: 0
      }
      """

    场景: 双邮箱无效时保持安全
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 0, "vehValid": 0, "curValid": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        debounce: 0
        backupCutoff: 0
      }
      """

  规则: 备份切断 — SWR-SC-024

    cur_ok 且 FZC 制动故障时，电流 >1000mA 累计 backup_cutoff_counter，
    达 10-tick 后置位 faulted 并点亮 LED；否则立即清零。

    场景: 备份切断去抖阈值之下不判故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1500, "brakeFault": 1, "repeats": 9 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        backupCutoff: 9
      }
      """

    场景: 备份切断阈值达成后判故障并点亮 LED
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1500, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 1
        backupCutoff: 10
        ledSys: 1
      }
      """

    场景: 无制动故障时高电流不触发备份切断
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1500, "brakeFault": 0, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        backupCutoff: 0
      }
      """

    场景: 备份切断电流恰在阈值边界不累计
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1000, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        backupCutoff: 0
      }
      """

    场景: 备份切断电流超过阈值边界触发故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1001, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 1
        backupCutoff: 10
      }
      """

    场景: 备份切断计数器因电流回落而复位
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1500, "brakeFault": 1, "repeats": 5 },
          { "op": "check", "torque": 0, "current": 500, "brakeFault": 1, "repeats": 1 },
          { "op": "check", "torque": 0, "current": 1500, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[3].state.backupCutoff: 0
        results[4].state.faulted: 1
      }
      """

    场景: 备份切断仅依赖电流邮箱不依赖车辆状态邮箱
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 100, "current": 1500, "vehValid": 0, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 1
        backupCutoff: 10
      }
      """

    场景: 备份切断需要电流邮箱有效
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "check", "torque": 0, "current": 1500, "curValid": 0, "brakeFault": 1, "repeats": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        faulted: 0
        backupCutoff: 0
      }
      """

  规则: 蠕动防护 — SSR-SC-018

    零扭矩且电流 >500mA 时累计 creep_debounce，达 2 周期后置位
    **不可清除**的 creep_faulted 并点亮 LED；torque!=0 或电流不足则清零。

    场景: 蠕动去抖阈值之下不判故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 600, "repeats": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        creepFaulted: 0
        creepDebounce: 1
      }
      """

    场景: 蠕动阈值达成后判故障并点亮 LED
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 600, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        creepFaulted: 1
        ledSys: 1
      }
      """

    场景: 蠕动电流恰在阈值边界不累计
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 500, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        creepFaulted: 0
        creepDebounce: 0
      }
      """

    场景: 蠕动电流超过阈值边界触发故障
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 501, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state.creepFaulted: 1
      """

    场景: 非零扭矩时高电流不触发蠕动防护
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 1, "current": 600, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2].state: {
        creepFaulted: 0
        creepDebounce: 0
      }
      """

    场景: 蠕动防护在 CAN 数据缺失时不触发
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 600, "vehValid": 0, "repeats": 2 },
          { "op": "creep", "torque": 0, "current": 600, "curValid": 0, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[2].state.creepFaulted: 0
        results[3].state: {
          creepFaulted: 0
          creepDebounce: 0
        }
      }
      """

    场景: 宽限期内蠕动防护不触发
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "creep", "torque": 0, "current": 600, "repeats": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1].state.creepFaulted: 0
      """

    场景: 蠕动故障不可清除（仅断电复位）
      当POST "/api/test/asw/sc/plausibility":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "drainGrace", "ticks": 1500 },
          { "op": "creep", "torque": 0, "current": 600, "repeats": 2 },
          { "op": "creep", "torque": 1, "current": 0, "repeats": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        results[2].state.creepFaulted: 1
        results[3].state.creepFaulted: 1
      }
      """
