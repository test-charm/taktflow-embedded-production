# language: zh-CN
功能: CVC NVM 存储 (Swc_Nvm)

  Swc_Nvm NVM 存储 SWC 的端到端测试：DTC 持久化（20-slot 循环缓冲 + 每条目
  CRC-16 损坏检测 + 32 字节冻结帧）与校准数据（踏板阈值/扭矩 LUT，CRC-16 保护，
  损坏时回退编译期默认值），以及未初始化守卫（全部 API 拒绝）。

  背景:
    假如存在:
      """
      CvcNvmSetup: {
        phases: []
      }
      """

  规则: 初始化 — Swc_Nvm_Init

    调用 Swc_Nvm_Init 后内部就绪（Initialized=TRUE、写索引=0、计数=0），
    校准数据为编译期默认值；跳过初始化时所有 API 均返回 E_NOT_OK。

    场景: 初始化后内部状态就绪且校准为默认值
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        writeIndex: 0
        dtcCount: 0
        results: | op       | ret | plausThreshold | plausDebounce | stuckThreshold | stuckCycles | lut0 | lut15 |
                 | readCal  | 0   | 819            | 2             | 10             | 100         | 0    | 1000  |
      }
      """

    场景: 未初始化时存储 DTC 被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "skipInit": true, "dtcId": 5 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        writeIndex: 0
        dtcCount: 0
        results: | op        | ret |
                 | storeDtc  | 1   |
      }
      """

    场景: 未初始化时加载 DTC 被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "skipInit": true, "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        results: | op       | ret |
                 | loadDtc  | 1   |
      }
      """

    场景: 未初始化时读写校准被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "readCal", "skipInit": true },
          { "op": "writeCal", "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        results: | op        | ret |
                 | readCal   | 1   |
                 | writeCal  | 1   |
      }
      """

  规则: DTC 持久化 — SWR-CVC-030

    存储条目写入当前写索引，加载时校验 CRC-16；冻结帧（32 字节）在传入时
    原样保存、NULL 时清零；循环缓冲满 20 条后回绕覆盖最旧条目。

    场景: 存储后加载读回条目且计数递增
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        writeIndex: 1
        dtcCount: 1
        results: | op        | ret | slot | dtcId | status | occurrenceCount |
                 | storeDtc  | 0   | 0    | *     | *      | *               |
                 | loadDtc   | 0   | *    | 5     | 1      | 1               |
      }
      """

    场景: 冻结帧按 0xA0+i 模式原样存储
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 3, "status": 1, "ffMode": 1 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: loadDtc
        ret: 0
        dtcId: 3
        ffHex: 'a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf'
      }
      """

    场景: 冻结帧为 NULL 时存储全零
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 7, "status": 1 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: loadDtc
        ret: 0
        dtcId: 7
        ffHex: '0000000000000000000000000000000000000000000000000000000000000000'
      }
      """

    场景: 存储 20 条后循环缓冲回绕覆盖最旧条目
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 0, "repeats": 20 },
          { "op": "loadDtc", "slot": 0 },
          { "op": "storeDtc", "dtcId": 99, "status": 8 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        writeIndex: 1
        dtcCount: 20
        results: | op        | ret | slot | dtcId | status | occurrenceCount |
                 | storeDtc  | 0   | 19   | *     | *      | *               |
                 | loadDtc   | 0   | *    | 0     | *      | 1               |
                 | storeDtc  | 0   | 0    | *     | *      | *               |
                 | loadDtc   | 0   | *    | 99    | 8      | 21              |
      }
      """

    场景: 越界槽位加载被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "slot": 20 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: loadDtc
        ret: 1
      }
      """

    场景: 空条目指针加载被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "slot": 0, "nullEntry": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: loadDtc
        ret: 1
      }
      """

  规则: DTC CRC 损坏检测 — SWR-CVC-030

    测试专用钩子将已存储条目的 CRC 翻转后，加载必须检测到损坏并返回 E_NOT_OK
    （fail-closed，绝不返回损坏数据）。

    场景: 存储条目 CRC 损坏后加载被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1 },
          { "op": "corruptDtcCrc", "slot": 0 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        op: loadDtc
        ret: 1
      }
      """

    场景: 未损坏条目加载始终成功
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: loadDtc
        ret: 0
        dtcId: 5
      }
      """

    场景: 越界槽位 CRC 损坏请求被忽略且条目仍可加载
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1 },
          { "op": "corruptDtcCrc", "slot": 20 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        op: loadDtc
        ret: 0
        dtcId: 5
      }
      """

  规则: 校准数据读写 — SWR-CVC-031

    写入自定义校准后内部重算 CRC，读取返回原值；NULL 指针拒绝；损坏时
    ReadCal 回退编译期默认值并返回 E_NOT_OK。

    场景: 写入校准后读回自定义值
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "pThreshold": 500, "pDebounce": 5,
            "stuckThreshold": 20, "stuckCycles": 200, "lut0": 10 },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: readCal
        ret: 0
        plausThreshold: 500
        plausDebounce: 5
        stuckThreshold: 20
        stuckCycles: 200
        lut0: 10
      }
      """

    场景: 空指针读取校准被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "readCal", "nullCal": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: readCal
        ret: 1
      }
      """

    场景: 空指针写入校准被拒绝
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "nullCal": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: writeCal
        ret: 1
      }
      """

  规则: 校准 CRC 损坏回退默认值 — SWR-CVC-031

    测试专用钩子翻转校准块 CRC 后，ReadCal 必须返回 E_NOT_OK 且将输出填为
    编译期默认值（fail-closed 回退，绝不返回损坏数据）。

    场景: 校准 CRC 损坏后读取回退默认值
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "pThreshold": 500 },
          { "op": "corruptCalCrc" },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        op: readCal
        ret: 1
        plausThreshold: 819
        plausDebounce: 2
        stuckThreshold: 10
        stuckCycles: 100
        lut15: 1000
      }
      """

    场景: 未损坏校准读取始终成功
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "pThreshold": 500 },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: readCal
        ret: 0
        plausThreshold: 500
      }
      """

  规则: CRC-16 计算 — Swc_Nvm_CalcCrc16

    CRC-16/CCITT（初值 0xFFFF、多项式 0x1021）对已知数据产生确定性结果；
    NULL 指针返回 0；长度 0 返回初值 0xFFFF。

    场景: 已知数据 CRC 与参考一致
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "calcCrc", "dataLen": 4 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: calcCrc
        crc: 35267
      }
      """

    场景: NULL 数据指针 CRC 返回 0
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "calcCrc", "nullCrc": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: calcCrc
        crc: 0
      }
      """

    场景: 零长度数据 CRC 返回初值 0xFFFF
      当POST "/api/test/asw/cvc/nvm":
      """
      {
        "phases": [
          { "op": "calcCrc", "dataLen": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: calcCrc
        crc: 65535
      }
      """
