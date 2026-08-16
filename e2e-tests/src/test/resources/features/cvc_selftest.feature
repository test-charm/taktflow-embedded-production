# language: zh-CN
功能: CVC 启动自检 (Swc_SelfTest)

  Swc_SelfTest 启动自检序列 SWC 的端到端测试：7 项诊断检查（SPI 传感器
  回环 / CAN 控制器回环 / NVM 双区 CRC / OLED I2C ACK / MPU 区域校验 /
  栈金丝雀 / RAM 模式测试）。关键检查失败立即终止并上报 DTC
  （CVC_DTC_SELF_TEST_FAIL / CVC_DTC_NVM_CRC_FAIL），OLED 失败为非关键
  （QM）仅上报 CVC_DTC_DISPLAY_COMM 不阻断自检；每次运行步骤结果位
  掩码重置并可通过 Swc_SelfTest_GetResults 读取。

  背景:
    假如存在:
      """
      CvcSelfTestSetup: {
        phases: []
      }
      """

  规则: 启动自检序列 — Swc_SelfTest_Startup

    Swc_SelfTest_Startup 依次执行 7 项硬件检查。每个关键检查（SPI/CAN/NVM/
    MPU/CANARY/RAM）失败即上报 DTC 并返回 SELF_TEST_FAILED；OLED 失败仅
    上报 CVC_DTC_DISPLAY_COMM 继续执行；全部通过返回 SELF_TEST_PASSED。
    已通过步骤的位被累积到步骤结果位掩码中，可通过 GetResults 读取。

    场景: 所有硬件检查通过时自检成功
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "spi": true, "can": true, "nvm": true, "oled": true,
            "mpu": true, "canary": true, "ram": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 1
        stepResults: 127
        preResults: 0
        demTotal: 0
        demSelfTestFail: 0
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: SPI 传感器回环失败立即终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "spi": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 0
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: CAN 控制器回环失败在 SPI 通过后终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "spi": true, "can": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 1
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: NVM 完整性失败上报 CRC DTC 并终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "nvm": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 3
        demTotal: 1
        demSelfTestFail: 0
        demNvmCrcFail: 1
        demDisplayComm: 0
      }
      """

    场景: OLED 失败为非关键项不阻断自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "oled": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 1
        stepResults: 119
        demTotal: 1
        demSelfTestFail: 0
        demNvmCrcFail: 0
        demDisplayComm: 1
      }
      """

    场景: MPU 区域校验失败终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "mpu": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 15
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: 栈金丝雀校验失败终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "canary": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 31
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: RAM 模式测试失败终止自检
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "ram": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 63
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """

    场景: OLED 与关键检查同时失败时上报两个 DTC
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "oled": false, "mpu": false }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        stepResults: 7
        demTotal: 2
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 1
      }
      """

    场景: 失败运行后再次运行自检步骤结果位掩码重置
      假如存在:
        """
        CvcSelfTestSetup: {
          phases: [
            { spi: false }
          ]
        }
        """
      当POST "/api/test/asw/cvc/selftest":
      """
      {
        "phases": [
          { "spi": true, "can": true, "nvm": true, "oled": true,
            "mpu": true, "canary": true, "ram": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 1
        stepResults: 127
        demTotal: 1
        demSelfTestFail: 1
        demNvmCrcFail: 0
        demDisplayComm: 0
      }
      """
