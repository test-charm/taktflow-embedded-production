# language: zh-CN
功能: RZC 编码器 (Swc_Encoder)

  Swc_Encoder_MainFunction 的端到端测试：RPM 计算、方向输出、uint32 计数回绕、
  PWM>10% 且零位移 50 周期卡滞检测、卡滞计数复位、正反转切换后的 200ms/100ms
  宽限期，以及 5 周期方向不一致故障与锁存。

  背景:
    假如存在:
      """
      RzcEncoderSetup: {
        phases: []
      }
      """

  规则: 初始化守卫与速度计算

    场景: 未初始化时主函数空转
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "skipInit": true, "count": 123, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 0
        encoderStall: 0
        dioWrites: 0
        demStall: -1
        demDirection: -1
      }
      """

    场景: 36 计数增量计算为 600RPM 且输出正向
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 36, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 600
        encoderDir: 0
        encoderStall: 0
      }
      """

    场景: 单个计数边界得到 16RPM
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 1, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 16
        encoderDir: 0
      }
      """

    场景: 计数器从 UINT32_MAX 附近回绕后仍正确计算速度
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 4294967280, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "count": 16, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 533
        encoderDir: 0
      }
      """

    场景: 反向位移时写出反向方向
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 1, "commandedDir": 1, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 1, "commandedDir": 1, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 166
        encoderDir: 1
        demDirection: -1
      }
      """

  规则: 卡滞检测

    场景: PWM 恰好 10% 且无位移时不触发卡滞
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 100, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 10, "cycles": 1 },
          { "encoderDir": 0, "commandedDir": 0, "torqueEcho": 10, "cycles": 50 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        encoderStall: 0
        dioWrites: 0
        demStallCount: 0
      }
      """

    场景: 第 50 个零位移周期前恢复运动会清零卡滞计数
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 100, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 49 },
          { "deltaPerCycle": 5, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 83
        encoderStall: 0
        demStallCount: 0
      }
      """

    场景: 第 50 个零位移周期触发卡滞并在下一周期保持锁存
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 100, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 51 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        encoderStall: 1
        dioCh5: 0
        dioCh6: 0
        dioWrites: 2
        demStall: 1
        demStallCount: 1
      }
      """

    场景: 正反转切换后的 20 周期静止宽限期内不触发卡滞
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "encoderDir": 1, "commandedDir": 1, "torqueEcho": 50, "cycles": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 0
        encoderStall: 0
        demStallCount: 0
      }
      """

  规则: 方向合理性

    场景: 指令切到 STOP 时即使仍在前进也不报方向故障
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 0, "commandedDir": 2, "torqueEcho": 0, "cycles": 6 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        encoderDir: 0
        demDirection: -1
        demDirectionCount: 0
      }
      """

    场景: 方向不一致但无位移时不累计故障计数
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 200, "encoderDir": 1, "commandedDir": 0, "torqueEcho": 0, "cycles": 1 },
          { "encoderDir": 1, "commandedDir": 0, "torqueEcho": 0, "cycles": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 0
        demDirection: -1
        demDirectionCount: 0
      }
      """

    场景: 连续 4 个方向不一致周期仍不触发故障
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 1, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 1, "commandedDir": 0, "torqueEcho": 50, "cycles": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        encoderStall: 0
        demDirection: -1
        demDirectionCount: 0
      }
      """

    场景: 第 5 个方向不一致周期触发故障并在下一周期保持锁存
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 1, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 1, "commandedDir": 0, "torqueEcho": 50, "cycles": 6 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        dioCh5: 0
        dioCh6: 0
        dioWrites: 2
        demDirection: 1
        demDirectionCount: 1
      }
      """

    场景: 正反转切换后的 10 周期方向宽限期内不触发故障
      当POST "/api/test/asw/rzc/encoder":
      """
      {
        "phases": [
          { "count": 0, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 0, "commandedDir": 0, "torqueEcho": 50, "cycles": 1 },
          { "deltaPerCycle": 10, "encoderDir": 0, "commandedDir": 1, "torqueEcho": 50, "cycles": 10 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        speedRpm: 166
        demDirection: -1
        demDirectionCount: 0
      }
      """

