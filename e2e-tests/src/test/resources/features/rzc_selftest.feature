# language: zh-CN
功能: RZC 启动自检 (Swc_RzcSelfTest)

  Swc_RzcSelfTest 启动自检 SWC 的端到端测试：8 项注入式硬件诊断检查
  （BTS7960 使能引脚切换 / ACS723 基线校准 / NTC 温度范围 / 编码器连通性 /
  CAN 回环 / MPU 区域校验 / 栈金丝雀 / RAM 模式测试）。任一检查失败立即
  终止序列、禁用电机（R_EN/L_EN 拉低 + PWM STOP）并按项目上报 DTC
  （RZC_DTC_SELF_TEST_FAIL / RZC_DTC_ZERO_CAL / RZC_DTC_ENCODER /
  RZC_DTC_CAN_BUS_OFF）；未初始化守卫直接返回 FAIL；NULL 配置 Init 保持
  未初始化；每次运行结果位掩码重置并可通过 Swc_RzcSelfTest_GetResultMask
  读取。

  背景:
    假如存在:
      """
      RzcSelfTestSetup: {
        phases: []
      }
      """

  规则: 启动自检序列 — Swc_RzcSelfTest_Startup

    Swc_RzcSelfTest_Startup 依次执行 8 项硬件检查。任一检查失败即禁用电机
    并上报对应 DTC 后返回 SELF_TEST_FAIL；全部通过返回 SELF_TEST_PASS。
    已通过项目的位被累积到结果位掩码中，可通过 GetResultMask 读取。
    检查值 0=E_NOT_OK（失败）、1=E_OK（通过）、2=NULL 回调指针（走
    pfn != NULL 守卫的 false 侧，同样视为失败）。

    场景: 所有硬件检查通过时自检成功
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 1, "ram": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 1
        resultMask: 255
        preMask: 0
        demTotal: 0
        demSelfTestFail: 0
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioWrites: 0
      }
      """

    场景: 未初始化时自检直接返回失败
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 0
        preMask: 0
        demTotal: 0
        demSelfTestFail: 0
        dioWrites: 0
      }
      """

    场景: NULL 配置初始化后自检保持失败
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "initNull": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 0
        demTotal: 0
        demSelfTestFail: 0
        dioWrites: 0
      }
      """

    场景: BTS7960 使能引脚切换失败立即终止并禁用电机
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 0
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
        dioWrites: 2
        pwmDir: 2
        pwmDuty: 0
      }
      """

    场景: ACS723 基线校准失败上报 ZERO_CAL DTC
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 1
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 1
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NTC 温度范围检查失败终止自检
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 3
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: 编码器连通性失败上报 ENCODER DTC
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 7
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 0
        demEncoder: 1
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: CAN 回环失败上报 CAN_BUS_OFF DTC
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1, "can": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 15
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 1
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: MPU 区域校验失败终止自检
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 31
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: 栈金丝雀校验失败终止自检
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 63
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: RAM 模式测试失败终止自检
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 1, "ram": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 127
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 BTS7960 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 0
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 ACS723 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 1
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 1
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 NTC 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 3
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发编码器失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 7
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 0
        demEncoder: 1
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 CAN 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1, "can": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 15
        demTotal: 1
        demSelfTestFail: 0
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 1
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 MPU 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 31
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发栈金丝雀失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 63
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: NULL 回调指针触发 RAM 失败分支
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 1, "ram": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 0
        resultMask: 127
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioCh5: 0
        dioCh6: 0
      }
      """

    场景: 失败运行后再次运行自检结果位掩码重置
      假如存在:
        """
        RzcSelfTestSetup: {
          phases: [
            { bts7960: 0 }
          ]
        }
        """
      当POST "/api/test/asw/rzc/selftest":
      """
      {
        "phases": [
          { "bts7960": 1, "acs723": 1, "ntc": 1, "encoder": 1,
            "can": 1, "mpu": 1, "canary": 1, "ram": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        result: 1
        resultMask: 255
        preMask: 0
        demTotal: 1
        demSelfTestFail: 1
        demZeroCal: 0
        demEncoder: 0
        demCanBusOff: 0
        dioWrites: 2
      }
      """
