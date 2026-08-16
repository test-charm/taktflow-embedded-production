# language: zh-CN
功能: RZC 温度监控 (Swc_TempMonitor)

  Swc_TempMonitor_MainFunction 的端到端测试：NTC 温度读取、合理范围门控
  （-30..150 degC）、双 NTC 交叉校验（fail-hot，GAP-OT-001）、阶梯降额
  曲线（100/75/50/0%）、滞回恢复（10 degC）、0% 过温 DEM DTC 报告、
  RTE 信号广播（温度 1/2 + 降额 + 故障标志）。

  背景:
    假如存在:
      """
      RzcTempMonitorSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与 NTC 读取

    这些场景覆盖 Swc_TempMonitor_Init 的未初始化守卫、标称温度读取与
    IoHwAb 读取失败时的故障上报路径。

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "tempDc": 1000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 0
        temp2Dc: 0
        deratingPct: 0
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 标称 25.0 degC 读取并广播 RTE 信号
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 250 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 250
        temp2Dc: 250
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: IoHwAb 读取失败报告故障
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 250, "ioFault": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 0
        temp2Dc: 0
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

  规则: 合理范围门控

    温度必须在 -30.0..150.0 degC（-300..1500 ddc）范围内；越界时置位故障、
    报告 RZC_DTC_OVERTEMP FAILED 并返回（不广播温度）。边界值本身可接受。

    场景: 低于 -30.0 degC 触发范围故障
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": -310 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 0
        temp2Dc: 0
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 高于 150.0 degC 触发范围故障
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1510 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 0
        temp2Dc: 0
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 恰好 -30.0 degC 为可接受边界
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": -300 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: -300
        temp2Dc: -300
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 恰好 150.0 degC 为可接受边界并进入 0% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 1500
        temp2Dc: 1500
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

  规则: 阶梯降额曲线

    按整度温度分档：<60 -> 100%、60-79 -> 75%、80-99 -> 50%、>=100 -> 0%。
    每个等价类取一个代表值验证。

    场景: 25.0 degC 降额 100%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 250 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 250
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 70.0 degC 降额 75%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 700 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 700
        deratingPct: 75
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 90.0 degC 降额 50%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 900 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 900
        deratingPct: 50
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 100.0 degC 降额 0% 并报告过温 DTC
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 1000
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

  规则: 降额曲线边界值

    60/80/100 degC 三个分档阈值两侧（59/60、79/80、99/100 degC）均验证。

    场景: 59.0 degC 仍为 100% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 590 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 590
        deratingPct: 100
      }
      """

    场景: 恰好 60.0 degC 降额 75%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 600
        deratingPct: 75
      }
      """

    场景: 79.0 degC 仍为 75% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 790 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 790
        deratingPct: 75
      }
      """

    场景: 恰好 80.0 degC 降额 50%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 800 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 800
        deratingPct: 50
      }
      """

    场景: 99.0 degC 仍为 50% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 990 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 990
        deratingPct: 50
      }
      """

    场景: 恰好 100.0 degC 降额 0%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 1000
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 0.0 degC 降额 100%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 0
        temp2Dc: 0
        deratingPct: 100
        tempFault: 0
      }
      """

  规则: 滞回恢复

    降额可随温度升高自由下降；恢复（升高）需要温度降至下一档阈值减
    10 degC（0%->50% 需 <=90、50%->75% 需 <=70、75%->100% 需 <=50）。
    多阶段脚本中阶段顺序执行、模块状态（当前降额/DEM）跨阶段保留。

    场景: 0% 时 91.0 degC 仍保持 0% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 },
          { "cycles": 1, "tempDc": 910 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 910
        deratingPct: 0
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 0% 时降至 90.0 degC 恢复至 50%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 },
          { "cycles": 1, "tempDc": 900 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 900
        deratingPct: 50
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 50% 时 71.0 degC 仍保持 50% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 900 },
          { "cycles": 1, "tempDc": 710 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 710
        deratingPct: 50
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 50% 时降至 70.0 degC 恢复至 75%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 900 },
          { "cycles": 1, "tempDc": 700 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 700
        deratingPct: 75
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 75% 时 51.0 degC 仍保持 75% 降额
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 700 },
          { "cycles": 1, "tempDc": 510 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 510
        deratingPct: 75
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 75% 时降至 50.0 degC 恢复至 100%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 700 },
          { "cycles": 1, "tempDc": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 500
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 0% 时骤冷至 20.0 degC 仅恢复一档到 50%
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 },
          { "cycles": 1, "tempDc": 200 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 200
        deratingPct: 50
        tempFault: 1
        demOvertemp: 1
      }
      """

  规则: 双 NTC 交叉校验 (GAP-OT-001)

    |NTC1 - NTC2| > 300 ddc（30.0 degC）时判定传感器故障并 fail-hot：
    取较高读数驱动降额曲线与 RTE 温度；恰好等于阈值视为可信；
    NTC2 读取失败时降级为单传感器（NTC1）运行，不报温度故障。

    场景: NTC2 显著偏高时 fail-hot 采用较高读数
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 500, "temp2Dc": 900 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 900
        temp2Dc: 900
        deratingPct: 50
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: NTC1 偏高且差异超过阈值时保持较高读数
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 900, "temp2Dc": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 900
        temp2Dc: 500
        deratingPct: 50
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: 差异恰好 300 ddc 视为可信
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 500, "temp2Dc": 800 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 500
        temp2Dc: 800
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

    场景: NTC2 读取失败降级为单传感器运行
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 500, "temp2Fail": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 500
        temp2Dc: 0
        deratingPct: 100
        tempFault: 0
        demOvertemp: -1
      }
      """

  规则: DTC 报告与锁存

    降额达到 0% 或范围/读取故障时报告 RZC_DTC_OVERTEMP FAILED；生产代码
    从不报告 PASSED，DTC 与温度故障标志保持锁存直至复位/重新初始化。

    场景: 过温恢复后 DTC 与故障标志保持锁存
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 1000 },
          { "cycles": 1, "tempDc": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 500
        deratingPct: 50
        tempFault: 1
        demOvertemp: 1
      }
      """

    场景: 健康区间运行不报告 DTC
      当POST "/api/test/asw/rzc/temponitor":
      """
      {
        "phases": [
          { "cycles": 1, "tempDc": 250 },
          { "cycles": 1, "tempDc": 700 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        temp1Dc: 700
        deratingPct: 75
        tempFault: 0
        demOvertemp: -1
      }
      """
