# language: zh-CN
功能: RZC 电流监控 (Swc_CurrentMonitor)

  Swc_CurrentMonitor_MainFunction 的端到端测试：64 样本零点校准窗口、
  4 样本移动平均、>25A 过流 10 周期去抖、过流后电机关闭与 DEM 上报、
  已激活过流的持续上报，以及 500ms 低于阈值恢复/尖峰复位恢复计数。

  背景:
    假如存在:
      """
      RzcCurrentMonitorSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与零点校准

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "skipInit": true, "currentMa": 5000, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 0
        overcurrent: 0
        dioWrites: 0
        demOvercurrent: -1
        demZeroCal: -1
      }
      """

    场景: 标称 2048mA 零点校准通过并写出当前值
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 2048
        overcurrent: 0
        dioWrites: 0
        demOvercurrent: -1
        demZeroCal: -1
      }
      """

    场景: 零点校准低边界 1848mA 仍接受
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 1848, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 1848
        overcurrent: 0
        demZeroCal: -1
        demZeroCalCount: 0
      }
      """

    场景: 零点校准高边界 2248mA 仍接受
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2248, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 2248
        overcurrent: 0
        demZeroCal: -1
        demZeroCalCount: 0
      }
      """

    场景: 零点校准高越界 2249mA 报告 ZERO_CAL DTC
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2249, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 2249
        overcurrent: 0
        demZeroCal: 1
        demZeroCalCount: 1
      }
      """

    场景: 零点校准越界 1800mA 报告 ZERO_CAL DTC
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 1800, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 1800
        overcurrent: 0
        demZeroCal: 1
        demZeroCalCount: 1
      }
      """

  规则: 4 样本移动平均

    场景: 四次相同采样得到稳定均值
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 5000, "cycles": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 5000
        overcurrent: 0
      }
      """

    场景: 1 2 3 4A 采样均值为 2500mA
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 1000, "cycles": 1 },
          { "currentMa": 2000, "cycles": 1 },
          { "currentMa": 3000, "cycles": 1 },
          { "currentMa": 4000, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 2500
        overcurrent: 0
      }
      """

    场景: 第五个样本到来时滑动窗口淘汰最旧值
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 1000, "cycles": 1 },
          { "currentMa": 2000, "cycles": 1 },
          { "currentMa": 3000, "cycles": 1 },
          { "currentMa": 4000, "cycles": 1 },
          { "currentMa": 5000, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 3500
        overcurrent: 0
      }
      """

  规则: 过流检测与去抖

    场景: 24999mA 长时间运行不触发过流
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 24999, "cycles": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 24999
        overcurrent: 0
        dioWrites: 0
        demOvercurrent: -1
        demOvercurrentCount: 0
      }
      """

    场景: 恰好 25000mA 因严格大于比较而不触发过流
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 25000, "cycles": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 25000
        overcurrent: 0
        dioWrites: 0
        demOvercurrent: -1
      }
      """

    场景: 25001mA 连续 10 周期触发过流并关闭电机
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 25001, "cycles": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 25001
        overcurrent: 1
        dioWrites: 2
        dioCh5: 0
        dioCh6: 0
        demOvercurrent: 1
        demOvercurrentCount: 1
      }
      """

    场景: 去抖到 9 次后插入正常样本会清零计数
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 26000, "cycles": 9 },
          { "currentMa": 1000, "cycles": 1 },
          { "currentMa": 26000, "cycles": 9 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        overcurrent: 0
        dioWrites: 0
        demOvercurrent: -1
        demOvercurrentCount: 0
      }
      """

    场景: 过流已激活后每个高电流周期继续上报 DEM
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 26000, "cycles": 11 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 26000
        overcurrent: 1
        dioWrites: 2
        demOvercurrent: 1
        demOvercurrentCount: 2
      }
      """

  规则: 500ms 恢复窗口

    场景: 连续 500 个低电流周期清除过流
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 26000, "cycles": 10 },
          { "currentMa": 1000, "cycles": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 1000
        overcurrent: 0
        demOvercurrent: 1
      }
      """

    场景: 仅 499 个低电流周期仍保持过流
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 26000, "cycles": 10 },
          { "currentMa": 1000, "cycles": 499 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 1000
        overcurrent: 1
        demOvercurrent: 1
      }
      """

    场景: 恢复期间单次高原始采样会把恢复计数清零
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 26000, "cycles": 10 },
          { "currentMa": 1000, "cycles": 250 },
          { "currentMa": 26000, "cycles": 1 },
          { "currentMa": 1000, "cycles": 499 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 1000
        overcurrent: 1
        demOvercurrent: 1
      }
      """

    场景: 65535mA 原始采样不会导致平均计算溢出
      当POST "/api/test/asw/rzc/currentmonitor":
      """
      {
        "phases": [
          { "currentMa": 2048, "cycles": 0 },
          { "currentMa": 65535, "cycles": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentMa: 65535
        overcurrent: 0
      }
      """
