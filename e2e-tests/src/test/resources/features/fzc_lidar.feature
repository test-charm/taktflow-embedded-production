# language: zh-CN
功能: FZC 激光雷达障碍物检测 (Swc_Lidar)

  Swc_Lidar_MainFunction 的端到端测试：TFMini-S 9 字节帧解析（帧同步/
  校验和/距离/信号强度提取）、四级渐进区域分类（clear/warning/braking/
  emergency）、范围与信号合理性检查、卡滞检测（50 周期相同读数）、
  帧超时（100ms）与恢复、故障安全默认（0cm + FAULT 区）、
  GetDistance 诊断读取。

  背景:
    假如存在:
      """
      FzcLidarSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与健康帧解析

    这些场景覆盖 Swc_Lidar_Init 的两种守卫（skipInit 未初始化、
    initNull NULL 配置）、健康帧的解析（距离/信号强度）以及
    GetDistance 诊断读取。

    场景: 未初始化时主函数与 GetDistance 空转
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "getDist": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        signal: 0
        zone: 0
        fault: 0
        getDistStatus: 1
        getDist: 0
      }
      """

    场景: NULL 配置初始化后主函数空转
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "initNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        signal: 0
        zone: 0
        fault: 0
      }
      """

    场景: 有效帧解析距离与信号强度 (clear 区)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 500, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 500
        signal: 500
        zone: 0
        fault: 0
        demTimeout: 0
        demChecksum: 0
        demStuck: 0
        demSignalLow: 0
      }
      """

    场景: 有效帧解析到 warning 区 (<=100cm)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 80, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 80
        zone: 1
        fault: 0
      }
      """

    场景: 有效帧解析到 braking 区 (<=50cm)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 40, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 40
        zone: 2
        fault: 0
      }
      """

    场景: 有效帧解析到 emergency 区 (<=20cm)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 15, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 15
        zone: 3
        fault: 0
      }
      """

    场景: 区域边界 100cm 判定为 warning
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 100, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 100
        zone: 1
        fault: 0
      }
      """

    场景: 区域边界 50cm 判定为 braking
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 50, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 50
        zone: 2
        fault: 0
      }
      """

    场景: 区域边界 20cm 判定为 emergency
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 20, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 20
        zone: 3
        fault: 0
      }
      """

    场景: GetDistance 读取当前距离
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 500, "signal": 500, "getDist": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getDistStatus: 0
        getDist: 500
      }
      """

    场景: GetDistance 传入 NULL 指针返回 E_NOT_OK
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 500, "signal": 500, "getDistNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getDistStatus: 1
        getDist: 0
      }
      """

  规则: 范围与信号合理性检查

    有效距离须在 2..1200cm 之间，信号强度须 >= 100；越界即故障
    并输出故障安全默认（0cm + FAULT 区）。

    场景: 距离超上限 1300cm 触发故障
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 1300, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        signal: 0
        zone: 4
        fault: 1
      }
      """

    场景: 距离低于下限 1cm 触发故障
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 1, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 1
      }
      """

    场景: 距离恰好在下限 2cm 有效
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 2, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 2
        zone: 3
        fault: 0
      }
      """

    场景: 距离恰好在上限 1200cm 有效
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 1200, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 1200
        zone: 0
        fault: 0
      }
      """

    场景: 信号强度 99 低于下限触发信号过低故障
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 200, "signal": 99 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 1
        demSignalLow: 1
      }
      """

    场景: 信号强度恰好在下限 100 有效
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 200, "signal": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 200
        fault: 0
        demSignalLow: 0
      }
      """

  规则: 帧错误处理（校验和 / 帧同步 / 不完整帧）

    TFMini-S 帧校验和错误立即置校验和 DTC 故障；帧头不同步或帧不完整
    时丢弃该帧并走超时计数路径。

    场景: 校验和错误触发故障并报告 DTC
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 200, "signal": 500, "badChecksum": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 1
        demChecksum: 1
      }
      """

    场景: 帧头不同步的垃圾字节被丢弃 (走超时路径)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "garbageHeader": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        signal: 0
        zone: 4
        fault: 0
      }
      """

    场景: 不完整帧被丢弃 (走超时路径)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "partialFrame": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 0
      }
      """

    场景: UART 驱动同步扫描读失败 (ret=E_NOT_OK) 走超时路径
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 200, "signal": 500, "uartFailAt": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 0
      }
      """

    场景: UART 驱动 7 字节载荷读失败 (ret=E_NOT_OK) 走超时路径
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 200, "signal": 500, "uartFailAt": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 0
      }
      """

  规则: 卡滞检测 (Stuck Detection)

    相同距离读数持续 50 个周期判定为卡滞；读数变化则重置计数器。
    卡滞期间先建立基线，故第 50 个相同读数触发。

    场景: 49 个相同读数不触发卡滞
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 49, "distCm": 200, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 200
        zone: 0
        fault: 0
        demStuck: 0
      }
      """

    场景: 第 50 个相同读数触发卡滞故障
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 50, "distCm": 200, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 1
        demStuck: 1
      }
      """

    场景: 读数变化重置卡滞计数器
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 40, "distCm": 200, "signal": 500 },
          { "cycles": 1, "distCm": 201, "signal": 500 },
          { "cycles": 48, "distCm": 201, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 201
        zone: 0
        fault: 0
      }
      """

  规则: 帧超时与恢复

    连续 100ms（100 周期）无有效帧触发超时故障并报告 DTC；
    数据恢复后故障清除（DTC PASSED）。

    场景: 99 周期无帧未达超时门限
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 99, "noFrame": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        zone: 4
        fault: 0
      }
      """

    场景: 100 周期无帧触发超时故障
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 100, "noFrame": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 0
        signal: 0
        zone: 4
        fault: 1
        demTimeout: 1
      }
      """

    场景: 超时后数据恢复清除故障 (DTC PASSED)
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 100, "noFrame": true },
          { "cycles": 1, "distCm": 200, "signal": 500 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 200
        zone: 0
        fault: 0
        demTimeout: 0
      }
      """

    场景: 无帧且无故障时保留上一周期输出
      当POST "/api/test/asw/fzc/lidar":
      """
      {
        "phases": [
          { "cycles": 1, "distCm": 500, "signal": 500 },
          { "cycles": 1, "noFrame": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        distance: 500
        signal: 500
        zone: 0
        fault: 0
      }
      """
