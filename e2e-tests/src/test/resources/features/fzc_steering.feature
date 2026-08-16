# language: zh-CN
功能: FZC 转向伺服控制 (Swc_Steering)

  Swc_Steering_MainFunction 的端到端测试：角度→PWM 映射、范围检查、
  速率限制、合理性检查、命令超时与回中（RTC）、SPI 故障、故障锁存与
  3 级 PWM 禁用、GetAngle 诊断读取。

  背景:
    假如存在:
      """
      FzcSteeringSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与健康 PWM 输出

    这些场景覆盖 Swc_Steering_Init 的两种守卫（skipInit 未初始化、
    initNull NULL 配置）以及健康路径的角度→PWM 线性映射。

    场景: 未初始化时主函数与 GetAngle 空转
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "getAngle": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 0
        getAngleStatus: 1
      }
      """

    场景: NULL 配置初始化后主函数空转
      当POST "/api/test/asw/fzc/steering":
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
        currentAngle: 0
        faultStatus: 0
        pwmDuty: 0
      }
      """

    场景: 居中命令输出中性 PWM 1500us
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 0, "actualAngle": 0, "getAngle": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 1500
        demPlaus: 0
        demRange: 0
        demTimeout: 0
        demSpi: 0
        getAngleStatus: 0
        getAngle: 0
      }
      """

    场景: 满右 45 度映射到 2000us
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": 45, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 45
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 2000
      }
      """

    场景: 满左 -45 度映射到 1000us
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": -45, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: -45
        faultStatus: 0
        pwmDuty: 1000
      }
      """

    场景: GetAngle 读取当前转向角
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": 45, "actualTrack": true, "getAngle": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getAngleStatus: 0
        getAngle: 45
      }
      """

    场景: GetAngle 传入 NULL 指针返回 E_NOT_OK
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 0, "actualAngle": 0, "getAngleNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getAngleStatus: 1
        getAngle: 0
      }
      """

  规则: 范围检查与速率限制

    Swc_Steering 对命令角做 -45..+45 范围检查（超限置 OUT_OF_RANGE 故障），
    并以 0.3 度/10ms 限速（增加方向受限、向中心方向不限速）。

    场景: 命令超上限 46 度触发范围故障并锁存
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 2
        pwmDisableLevel: 1
        pwmDuty: 1500
        demRange: 1
      }
      """

    场景: 命令超下限 -46 度触发范围故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": -46, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 2
        pwmDisableLevel: 1
        pwmDuty: 1500
      }
      """

    场景: 速率限制将增加限制为每周期 0.3 度
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 3, "cmdAngle": 45, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 0
        pwmDuty: 1510
      }
      """

    场景: 从满右向中心方向降速不受限
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": 45, "actualTrack": true },
          { "cycles": 1, "cmdAngle": 30, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 30
        faultStatus: 0
        pwmDuty: 1833
      }
      """

    场景: 从满左向中心方向降速不受限
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": -45, "actualTrack": true },
          { "cycles": 1, "cmdAngle": -30, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: -30
        faultStatus: 0
        pwmDuty: 1167
      }
      """

  规则: 合理性检查 (Plausibility)

    |反馈-输出| 超过 5 度并持续 50 个周期后置 PLAUSIBILITY 故障；
    未达门限则不计故障，恢复后消抖计数器清零。

    场景: 反馈偏离 30 度持续 55 周期触发合理性故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 55, "cmdAngle": 0, "actualAngle": 30 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 1
        pwmDisableLevel: 1
        pwmDuty: 1500
        demPlaus: 1
      }
      """

    场景: 反馈偏离 10 周期未达门限不触发故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 10, "cmdAngle": 0, "actualAngle": 30 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 1500
      }
      """

    场景: 合理性消抖计数恢复后清零
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 5, "cmdAngle": 0, "actualAngle": 30 },
          { "cycles": 60, "cmdAngle": 0, "actualAngle": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 1500
      }
      """

  规则: 命令超时与回中 (Command Timeout + RTC)

    命令新鲜度丢失 10 个周期后触发 CMD_TIMEOUT，输出按 30 度/秒向中心
    回中（RTC）；未收到过命令时计数器不触发超时。

    场景: 丢失命令 10 周期触发超时故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 20, "actualTrack": true },
          { "cycles": 10, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 4
        pwmDisableLevel: 0
        pwmDuty: 1530
        demTimeout: 1
      }
      """

    场景: 超时后回中朝中心方向移动
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": 45, "actualTrack": true },
          { "cycles": 10, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 44
        faultStatus: 4
        pwmDisableLevel: 0
        pwmDuty: 1996
      }
      """

    场景: 负向角度超时后回中朝中心方向移动
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 150, "cmdAngle": -45, "actualTrack": true },
          { "cycles": 10, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: -44
        faultStatus: 4
        pwmDisableLevel: 0
        pwmDuty: 1004
      }
      """

    场景: 已居中时超时回中吸附到 0
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 0, "actualTrack": true },
          { "cycles": 10, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 4
        pwmDisableLevel: 0
        pwmDuty: 1500
      }
      """

    场景: 未收到过命令时超时计数器不触发故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 20, "rteReadFail": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 0
        pwmDisableLevel: 0
        pwmDuty: 1500
      }
      """

    场景: 范围故障已锁存时超时计数不覆盖为超时故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 12, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 2
        pwmDisableLevel: 1
        pwmDuty: 1500
        demRange: 1
      }
      """

  规则: SPI 故障

    IoHwAb 转向角读取失败（SPI 故障）时立即置 SPI_FAIL 故障。

    场景: SPI 读取失败立即触发故障
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 0, "actualAngle": 0, "spiFail": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        faultStatus: 5
        pwmDisableLevel: 1
        pwmDuty: 1500
        demSpi: 1
      }
      """

  规则: 故障锁存与 3 级 PWM 禁用

    非超时故障被锁存（输出强制中性 PWM）；故障清除后锁存解除；
    多次故障事件逐级提升 PWM 禁用级别（1=中性、2=Dio、3=双 Dio）。

    场景: 范围故障后锁存仍强制中性输出
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 10, "cmdAngle": 0, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        pwmDisableLevel: 1
        pwmDuty: 1500
        demRange: 1
      }
      """

    场景: 故障消除且锁存清除后恢复 (Dem PASSED)
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 55, "cmdAngle": 0, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        currentAngle: 0
        pwmDisableLevel: 1
        pwmDuty: 1500
        demRange: 0
      }
      """

    场景: 第二次故障事件升级到 2 级禁用 (Dio)
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 55, "cmdAngle": 0, "actualTrack": true },
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 2
        pwmDisableLevel: 2
        pwmDuty: 1500
        dioCh10: 0
      }
      """

    场景: 第三次故障事件升级到 3 级禁用 (双 Dio)
      当POST "/api/test/asw/fzc/steering":
      """
      {
        "phases": [
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 55, "cmdAngle": 0, "actualTrack": true },
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true },
          { "cycles": 55, "cmdAngle": 0, "actualTrack": true },
          { "cycles": 1, "cmdAngle": 46, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        faultStatus: 2
        pwmDisableLevel: 3
        pwmDuty: 1500
        dioCh10: 0
        dioCh11: 0
      }
      """
