# language: zh-CN
功能: FZC 刹车伺服控制 (Swc_Brake)

  Swc_Brake_MainFunction 的端到端测试：命令钳位、E-stop 立即全刹、
  命令超时与自动刹车、命令振荡检测、PWM 偏差反馈验证、故障锁存、
  电机切断序列、GetPosition 诊断读取。

  背景:
    假如存在:
      """
      FzcBrakeSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与健康 PWM 输出

    这些场景覆盖 Swc_Brake_Init 的两种守卫（skipInit 未初始化、
    initNull NULL 配置）以及健康路径的命令→PWM 线性映射与命令钳位。

    场景: 未初始化时主函数与 GetPosition 空转
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "getPos": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 0
        faultStatus: 0
        pwmDuty: 0
        motorCutoff: 0
        pwmDisable: 0
        getPosStatus: 1
        getPos: 0
      }
      """

    场景: NULL 配置初始化后主函数空转
      当POST "/api/test/asw/fzc/brake":
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
        brakePosition: 0
        faultStatus: 0
        pwmDuty: 0
        pwmDisable: 0
      }
      """

    场景: 无刹车命令输出 0% PWM
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 0, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 0
        faultStatus: 0
        pwmDuty: 0
        motorCutoff: 0
        pwmDisable: 0
        demPwmFail: 0
        demTimeout: 0
        demOsc: 0
        demBrakeFault: 0
      }
      """

    场景: 中间刹车命令 60% 映射到 PWM 60
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 60, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 60
        faultStatus: 0
        pwmDuty: 60
        motorCutoff: 0
        pwmDisable: 0
      }
      """

    场景: 满刹命令 100% 映射到 PWM 100
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 100, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 0
        pwmDuty: 100
        motorCutoff: 0
        pwmDisable: 0
      }
      """

    场景: 超限命令 150 钳位到 100
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 150, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 0
        pwmDuty: 100
      }
      """

    场景: GetPosition 读取当前刹车位置
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 50, "actualPos": 500, "getPos": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getPosStatus: 0
        getPos: 50
      }
      """

    场景: GetPosition 传入 NULL 指针返回 E_NOT_OK
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "getPosNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        getPosStatus: 1
        getPos: 0
      }
      """

  规则: E-stop 立即全刹

    E-stop 激活时立即强制 100% 刹车并锁存（故障码 LATCHED），
    且锁存保持期间新命令无法清除。

    场景: E-stop 激活强制 100% 刹车并锁存
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 20, "estop": 1, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 3
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
      }
      """

    场景: E-stop 锁存保持期间新命令无法清除
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 20, "estop": 1, "actualTrack": true },
          { "cycles": 10, "cmdBrake": 20, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 3
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
      }
      """

  规则: 命令超时与自动刹车

    命令新鲜度丢失 9 周期后触发 CMD_TIMEOUT，自动刹车锁存并强制 100%；
    未收到过命令时超时计数器不触发；已锁存故障可被超时覆盖。

    场景: 丢失命令 9 周期触发超时故障并自动刹车
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 20, "actualTrack": true },
          { "cycles": 9, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 2
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demTimeout: 1
        demBrakeFault: 1
      }
      """

    场景: 未收到过命令时超时计数器不触发故障
      当POST "/api/test/asw/fzc/brake":
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
        brakePosition: 0
        faultStatus: 0
        pwmDuty: 0
        pwmDisable: 0
      }
      """

    场景: 已锁存 PWM 偏差故障时超时覆盖为超时故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 50, "cmdBrake": 80, "actualPos": 0 },
          { "cycles": 12, "rteReadFail": true, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 2
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demPwmFail: 1
        demTimeout: 1
        demBrakeFault: 1
      }
      """

  规则: 命令振荡检测

    相邻周期命令变化超过 30% 并持续 4 周期触发 CMD_OSCILLATION；
    稳态命令复位振荡计数器，不触发故障。

    场景: 命令振荡 4 周期触发振荡故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 0, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 50, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 0, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 50, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 0, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 4
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demOsc: 1
        demBrakeFault: 1
      }
      """

    场景: 稳态命令复位振荡计数器不触发故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 0, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 50, "actualTrack": true },
          { "cycles": 1, "cmdBrake": 50, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 50
        faultStatus: 0
        pwmDuty: 50
        pwmDisable: 0
      }
      """

  规则: PWM 偏差反馈验证

    |命令-实际位置| 超过 2% 并持续 50 周期后置 PWM_DEVIATION 故障；
    未达门限不计故障，恢复后消抖计数器清零。

    场景: 反馈偏差持续 50 周期触发 PWM 偏差故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 50, "cmdBrake": 80, "actualPos": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 1
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demPwmFail: 1
        demBrakeFault: 1
      }
      """

    场景: 反馈位置高于命令持续 50 周期触发 PWM 偏差故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 50, "cmdBrake": 50, "actualPos": 600 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 1
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demPwmFail: 1
        demBrakeFault: 1
      }
      """

    场景: 反馈偏差 10 周期未达门限不触发故障
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 10, "cmdBrake": 80, "actualPos": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 0
        faultStatus: 0
        pwmDuty: 80
        pwmDisable: 0
      }
      """

    场景: 偏差消抖计数恢复后清零
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 5, "cmdBrake": 80, "actualPos": 0 },
          { "cycles": 60, "cmdBrake": 80, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 80
        faultStatus: 0
        pwmDuty: 80
        pwmDisable: 0
      }
      """

  规则: 故障锁存与电机切断

    非超时故障被锁存（强制 100% 刹车 + PWM 禁用）；故障清除且锁存解除
    后恢复；电机切断序列在故障期间持续发送。

    场景: 范围故障后锁存仍强制全刹
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 50, "cmdBrake": 80, "actualPos": 0 },
          { "cycles": 10, "cmdBrake": 80, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 100
        faultStatus: 1
        pwmDuty: 100
        motorCutoff: 1
        pwmDisable: 1
        demPwmFail: 1
        demBrakeFault: 1
      }
      """

    场景: 故障消除且锁存清除后恢复 (Dem PASSED)
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 50, "cmdBrake": 80, "actualPos": 0 },
          { "cycles": 55, "cmdBrake": 80, "actualTrack": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 80
        faultStatus: 0
        pwmDuty: 80
        motorCutoff: 0
        pwmDisable: 0
        demPwmFail: 0
        demTimeout: 0
        demOsc: 0
        demBrakeFault: 0
      }
      """

    场景: 位置读取失败时保留旧位置
      当POST "/api/test/asw/fzc/brake":
      """
      {
        "phases": [
          { "cycles": 1, "cmdBrake": 60, "actualPos": 600 },
          { "cycles": 1, "cmdBrake": 60, "posReadFail": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        brakePosition: 60
        faultStatus: 0
        pwmDuty: 60
      }
      """
