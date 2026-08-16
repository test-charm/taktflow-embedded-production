# language: zh-CN
功能: RZC 电池电压监控 (Swc_Battery)

  Swc_Battery_MainFunction 的端到端测试：4 样本滑动平均、5 段阈值状态
  （DISABLE_LOW/WARN_LOW/NORMAL/WARN_HIGH/DISABLE_HIGH）、滞回恢复
  （±500mV）、DISABLE 状态 DEM DTC 报告（RZC_DTC_BATTERY）、
  SOC 单调递减守卫、RTE 信号广播（平均电压 + 状态码）。

  背景:
    假如存在:
      """
      RzcBatterySetup: {
        phases: []
      }
      """

  规则: 初始化守卫与滑动平均

    这些场景覆盖 Swc_Battery_Init 的未初始化守卫、标称电压初始化、
    4 样本滑动平均的收敛行为（1/2/4 周期）。

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "voltageMv": 7000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 0
        status: 0
        demBattery: -1
      }
      """

    场景: 初始化后首个周期保持标称平均电压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 1, "voltageMv": 12600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 12600
        status: 2
        demBattery: -1
      }
      """

    场景: 4 周期后滑动平均收敛到注入电压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 7000
        status: 0
        demBattery: 1
      }
      """

    场景: 首个周期平均电压部分收敛（混入 3 个标称样本）
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 1, "voltageMv": 7000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 11200
        status: 2
        demBattery: -1
      }
      """

    场景: 两个周期后平均电压进一步收敛
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 2, "voltageMv": 7000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 9800
        status: 1
        demBattery: -1
      }
      """

  规则: 状态阈值映射

    5 个状态等价类（禁用低压/警告低压/正常/警告高压/禁用高压）及
    8 个边界值（8000/10500/15000/17000 两侧）均按阈值链精确分档。

    场景: 低压禁用 DISABLE_LOW
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 7000
        status: 0
        demBattery: 1
      }
      """

    场景: 低压警告 WARN_LOW
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 9000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 9000
        status: 1
        demBattery: -1
      }
      """

    场景: 正常工作范围 NORMAL
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 12600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 12600
        status: 2
        demBattery: -1
      }
      """

    场景: 高压警告 WARN_HIGH
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 16000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 16000
        status: 3
        demBattery: -1
      }
      """

    场景: 高压禁用 DISABLE_HIGH
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 17500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 17500
        status: 4
        demBattery: 1
      }
      """

    场景: 7999mV 低于禁用低压阈值触发 DISABLE_LOW
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7999 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 7999
        status: 0
        demBattery: 1
      }
      """

    场景: 恰好 8000mV 属于警告低压而非禁用
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 8000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 8000
        status: 1
        demBattery: -1
      }
      """

    场景: 10499mV 属于警告低压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 10499 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 10499
        status: 1
        demBattery: -1
      }
      """

    场景: 恰好 10500mV 属于正常工作范围
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 10500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 10500
        status: 2
        demBattery: -1
      }
      """

    场景: 14999mV 属于正常工作范围
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 14999 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 14999
        status: 2
        demBattery: -1
      }
      """

    场景: 恰好 15000mV 属于警告高压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 15000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 15000
        status: 3
        demBattery: -1
      }
      """

    场景: 16999mV 属于警告高压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 16999 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 16999
        status: 3
        demBattery: -1
      }
      """

    场景: 恰好 17000mV 属于禁用高压
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 17000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 17000
        status: 4
        demBattery: 1
      }
      """

  规则: 滞回恢复

    从 DISABLE/WARN 状态恢复时，电压必须越过阈值 ±500mV 滞回带：
    低于 8500mV 保持 DISABLE_LOW、低于 11000mV 保持 WARN_LOW、
    高于 16500mV 保持 DISABLE_HIGH、高于 14500mV 保持 WARN_HIGH。

    场景: 8200mV 处于滞回带内保持 DISABLE_LOW
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7000 },
          { "cycles": 4, "voltageMv": 8200 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 8200
        status: 0
        demBattery: 1
      }
      """

    场景: 超过 8500mV 后从 DISABLE_LOW 恢复
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7000 },
          { "cycles": 4, "voltageMv": 9000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 9000
        status: 1
        demBattery: 1
      }
      """

    场景: 10600mV 处于滞回带内保持 WARN_LOW
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 9000 },
          { "cycles": 4, "voltageMv": 10600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 10600
        status: 1
        demBattery: -1
      }
      """

    场景: 超过 11000mV 后从 WARN_LOW 恢复
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 9000 },
          { "cycles": 4, "voltageMv": 11500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 11500
        status: 2
        demBattery: -1
      }
      """

    场景: 16800mV 处于滞回带内保持 DISABLE_HIGH
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 17500 },
          { "cycles": 4, "voltageMv": 16800 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 16800
        status: 4
        demBattery: 1
      }
      """

    场景: 低于 16500mV 后从 DISABLE_HIGH 恢复
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 17500 },
          { "cycles": 4, "voltageMv": 16000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 16000
        status: 3
        demBattery: 1
      }
      """

    场景: 14600mV 处于滞回带内保持 WARN_HIGH
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 16000 },
          { "cycles": 4, "voltageMv": 14600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 14600
        status: 3
        demBattery: -1
      }
      """

    场景: 低于 14500mV 后从 WARN_HIGH 恢复
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 16000 },
          { "cycles": 4, "voltageMv": 14000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 14000
        status: 2
        demBattery: -1
      }
      """

  规则: DTC 报告

    仅 DISABLE 状态（DISABLE_LOW/DISABLE_HIGH）报告 RZC_DTC_BATTERY FAILED；
    报告后保持锁存（生产代码不报告 PASSED，需 UDS/复位清除）。

    场景: 离开 DISABLE_LOW 后 DTC 保持 FAILED 锁存
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 7000 },
          { "cycles": 4, "voltageMv": 9000 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 9000
        status: 1
        demBattery: 1
      }
      """

    场景: 仅经历警告状态时不报告 DTC
      当POST "/api/test/asw/rzc/battery":
      """
      {
        "phases": [
          { "cycles": 4, "voltageMv": 16000 },
          { "cycles": 4, "voltageMv": 12600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        voltageMv: 12600
        status: 2
        demBattery: -1
      }
      """
