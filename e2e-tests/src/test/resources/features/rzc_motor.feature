# language: zh-CN
功能: RZC 电机控制 (Swc_Motor)

  Swc_Motor_MainFunction 的端到端测试：BTS7960 H 桥扭矩→PWM 映射、
  模式扭矩限制（RUN/DEGRADED/LIMP/SAFE_STOP）、热降额、
  急停/INIT/SHUTDOWN 立即关闭、方向切换死区时间、快速反向防直通、
  命令超时（10 周期）与恢复（5 条有效命令）、过流/过温外部故障、
  故障锁存与 DEM DTC 报告。

  背景:
    假如存在:
      """
      RzcMotorSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与健康扭矩→PWM 映射

    这些场景覆盖 Swc_Motor_Init 的未初始化守卫、零扭矩停止、正向/反向
    满扭矩映射（95% 上限）、比例映射与最小非零扭矩边界。

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "skipInit": true, "torqueCmd": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 0
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        dioCh5: 0
        dioCh6: 0
        demTimeout: -1
      }
      """

    场景: 零扭矩停止电机
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: 100% 扭矩正向映射到 95% 占空比
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 100
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 9500
        pwmDir: 0
        dioCh5: 1
        dioCh6: 1
      }
      """

    场景: -100% 扭矩反向映射到 95% 占空比
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": -100, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 100
        motorDir: 1
        motorEnable: 1
        motorFault: 0
        pwmDuty: 9500
        pwmDir: 1
        dioCh5: 1
        dioCh6: 1
      }
      """

    场景: 50% 扭矩比例映射到 47% 占空比
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 4700
        dioCh5: 1
        dioCh6: 1
      }
      """

    场景: 1% 扭矩边界占空比为零且电机不使能
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 1, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 1
        motorDir: 0
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

  规则: 模式扭矩限制

    车辆状态决定扭矩上限：RUN=100%、DEGRADED=75%、LIMP=30%、SAFE_STOP=0%；
    未知状态安全默认 0%。

    场景: DEGRADED 模式将 100% 扭矩限制到 75%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 2, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 75
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 7100
      }
      """

    场景: DEGRADED 模式 76% 扭矩钳位到 75%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 76, "vehicleState": 2, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 75
        motorDir: 0
        pwmDuty: 7100
      }
      """

    场景: DEGRADED 模式 -100% 反向扭矩钳位到 -75%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": -100, "vehicleState": 2, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 75
        motorDir: 1
        motorEnable: 1
        motorFault: 0
        pwmDuty: 7100
        pwmDir: 1
      }
      """

    场景: LIMP 模式将 100% 扭矩限制到 30%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 3, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 30
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 2800
      }
      """

    场景: LIMP 模式 31% 扭矩钳位到 30%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 31, "vehicleState": 3, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 30
        motorDir: 0
        pwmDuty: 2800
      }
      """

    场景: SAFE_STOP 模式强制扭矩为零
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 4, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

    场景: 未知车辆状态安全默认零扭矩
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 200, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

  规则: 热降额

    有效扭矩 = 限制后扭矩 × 降额百分比 / 100；降额 >100% 被钳位到 100%。

    场景: 50% 降额将 100% 扭矩减半
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 1, "derating": 50 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 4700
      }
      """

    场景: 0% 降额强制电机零输出
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 1, "derating": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

    场景: 150% 降额被钳位到 100%
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 1, "derating": 150 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 100
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 9500
      }
      """

  规则: 急停与 INIT/SHUTDOWN 立即关闭

    ESTOP 有效或车辆状态为 INIT/SHUTDOWN 时立即关闭电机输出并写安全输出。

    场景: 急停立即关闭电机
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 1, "derating": 100, "estop": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: INIT 状态关闭电机
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 0, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

    场景: SHUTDOWN 状态关闭电机
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 100, "vehicleState": 5, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

  规则: 方向切换死区时间

    正向↔反向切换时先强制 PWM=0 一个周期（防直通），下一周期应用新方向；
    快速反向（切换期间再次改变方向）会延长死区时间。

    场景: 正向切反向第一周期 PWM 清零
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": -50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: 死区时间后应用反向
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": -50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": -50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 1
        motorEnable: 1
        motorFault: 0
        pwmDuty: 4700
        pwmDir: 1
      }
      """

    场景: 死区时间内快速反向延长死区时间
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": -50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

    场景: 死区时间期间切至停止不触发重复死区时间
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": -50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 1
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
      }
      """

    场景: 同方向连续命令无死区时间
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 60, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 60
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 5700
      }
      """

  规则: 命令超时与恢复

    扭矩命令保持同一非零值 10 周期触发 CMD_TIMEOUT 故障并报告 DTC；
    零扭矩视为有意怠速不触发超时。超时后需 5 条有效（变化）命令恢复。

    场景: 10 周期相同命令未触发超时
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 10, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 4700
        demTimeout: -1
      }
      """

    场景: 11 周期相同命令触发超时故障
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 11, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 2
        pwmDuty: 0
        demTimeout: 1
      }
      """

    场景: 超时后 5 条有效命令恢复
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 11, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 51, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 52, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 53, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 54, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 55, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 55
        motorDir: 0
        motorEnable: 1
        motorFault: 0
        pwmDuty: 5200
        demTimeout: 0
      }
      """

    场景: 超时后仅 4 条有效命令仍保持禁用
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 11, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 51, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 52, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 53, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 54, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 2
        pwmDuty: 0
        demTimeout: 1
      }
      """

    场景: 超时后保持相同命令不产生恢复计数
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 11, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 3, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 2
        pwmDuty: 0
        demTimeout: 1
      }
      """

    场景: 超时计数器在 65535 周期处钳位不溢出
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 65600, "torqueCmd": 50, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 2
        pwmDuty: 0
        demTimeout: 1
      }
      """

    场景: 零扭矩怠速不触发超时
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 30, "torqueCmd": 0, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        demTimeout: -1
      }
      """

    场景: 超时后零扭矩命令亦可恢复
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 11, "torqueCmd": 50, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 },
          { "cycles": 1, "torqueCmd": 0, "vehicleState": 1, "derating": 100 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 0
        motorDir: 2
        motorEnable: 0
        motorFault: 0
        pwmDuty: 0
        demTimeout: 0
      }
      """

  规则: 过流与过温外部故障

    过流/过温标志置位时更新 MOTOR_FAULT 故障码（过流=3、过温=4），
    过温优先；故障码锁存至恢复或重新初始化。

    场景: 过流故障码 3 且故障锁存
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100, "overcurrent": 1 },
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100, "overcurrent": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 3
        pwmDuty: 4700
      }
      """

    场景: 过温故障码 4
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100, "tempFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 4
        pwmDuty: 4700
      }
      """

    场景: 过流与过温同时发生时过温优先
      当POST "/api/test/asw/rzc/motor":
      """
      {
        "phases": [
          { "cycles": 1, "torqueCmd": 50, "vehicleState": 1, "derating": 100, "overcurrent": 1, "tempFault": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        torqueEcho: 50
        motorDir: 0
        motorEnable: 1
        motorFault: 4
        pwmDuty: 4700
      }
      """
