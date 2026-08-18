# language: zh-CN
功能: RZC NVM 存储 (Swc_RzcNvm)

  Swc_RzcNvm NVM 存储 SWC 的端到端测试：DTC 持久化（20-slot 循环缓冲、
  写索引回绕、6 字段冻结帧、每条目 CRC-16/CCITT 损坏检测），未初始化守卫
  （全部 API 拒绝），公开写索引观测与静态 CRC-16 计算器（已知向量 / 零长度）。

  背景:
    假如存在:
      """
      RzcNvmSetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_RzcNvm_Init

    调用 Swc_RzcNvm_Init 后模块就绪：20 槽清零、写索引=0、初始化标志置位；
    未初始化时 StoreDtc / LoadDtc 返回 E_NOT_OK（fail-closed）。

    场景: 初始化后写索引归零且空槽加载被拒绝
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        writeIndex: 0
        results: | op       | ret |
                 | init     | *   |
                 | loadDtc  | 1   |
      }
      """

    场景: 未初始化时存储 DTC 被拒绝
      当POST "/api/test/asw/rzc/nvm":
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
        results: | op        | ret | slot | writeIndex |
                 | storeDtc  | 1   | 0    | 0          |
      }
      """

    场景: 未初始化时加载 DTC 被拒绝
      当POST "/api/test/asw/rzc/nvm":
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
        writeIndex: 0
        results: | op       | ret |
                 | loadDtc  | 1   |
      }
      """

  规则: DTC 持久化 — SWR-RZC-030

    StoreDtc 写入当前写索引槽（含 6 字段冻结帧），随后写索引 +1 并在 20 处
    回绕覆盖最旧条目；LoadDtc 对空槽、NULL 指针、越界槽位与 CRC 损坏均
    fail-closed。

    场景: 存储后加载读回 DTC 字段与写索引推进
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1, "timestamp": 1000 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 1
        writeIndex: 1
        results: | op        | ret | slot | writeIndex | dtcId | status | timestamp |
                 | storeDtc  | 0   | 0    | 1          | *     | *      | *         |
                 | loadDtc   | 0   | *    | *          | 5     | 1      | 1000      |
      }
      """

    场景: 冻结帧 6 字段原样存储读回
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 3, "status": 8, "timestamp": 555,
            "motorCurrentMa": 25000, "motorTempDdc": -40, "motorSpeedRpm": 6000,
            "batteryMv": 12000, "torqueCmdPct": -100, "vehicleState": 3 },
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
        status: 8
        timestamp: 555
        motorCurrentMa: 25000
        motorTempDdc: -40
        motorSpeedRpm: 6000
        batteryMv: 12000
        torqueCmdPct: -100
        vehicleState: 3
      }
      """

    场景: 两次存储后写索引递增且各自槽位可读回
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 1 },
          { "op": "storeDtc", "dtcId": 2, "timestamp": 2000 },
          { "op": "loadDtc", "slot": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        writeIndex: 2
        results: | op        | ret | slot | writeIndex | dtcId | timestamp |
                 | storeDtc  | 0   | 0    | 1          | *     | *         |
                 | storeDtc  | 0   | 1    | 2          | *     | *         |
                 | loadDtc   | 0   | *    | *          | 2     | 2000      |
      }
      """

    场景: 20 槽写满后第 21 条回绕覆盖最旧条目
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 0, "repeats": 20 },
          { "op": "storeDtc", "dtcId": 99, "status": 8 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        writeIndex: 1
        results: | op        | ret | slot | writeIndex | dtcId | status |
                 | storeDtc  | 0   | 19   | 0          | *     | *      |
                 | storeDtc  | 0   | 0    | 1          | *     | *      |
                 | loadDtc   | 0   | *    | *          | 99    | 8      |
      }
      """

    场景: 空冻结帧指针存储被拒绝
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "nullFreeze": true }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[0]: {
        op: storeDtc
        ret: 1
      }
      """

    场景: 越界槽位加载被拒绝
      当POST "/api/test/asw/rzc/nvm":
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
      当POST "/api/test/asw/rzc/nvm":
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

    场景: 空槽位加载被拒绝
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "slot": 0 }
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

  规则: DTC CRC 损坏检测 — SWR-RZC-030

    测试专用钩子翻转 RAM 中已存 DTC 条目的 CRC；LoadDtc 必须检测损坏并拒绝
    返回（fail-closed，绝不返回损坏数据）。越界损坏请求被忽略。

    场景: 存储条目 CRC 损坏后加载被拒绝
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1, "timestamp": 1000 },
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
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1, "timestamp": 1000 },
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
        status: 1
      }
      """

    场景: 越界槽位 CRC 损坏请求被忽略且已存条目不受影响
      当POST "/api/test/asw/rzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "status": 1, "timestamp": 1000 },
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
        status: 1
      }
      """

  规则: CRC-16 计算 — RzcNvm_Crc16

    CRC-16/CCITT（初值 0xFFFF、多项式 0x1021）对已知数据产生确定性结果；
    零长度返回初值 0xFFFF。

    场景: 已知数据 CRC 与参考一致
      当POST "/api/test/asw/rzc/nvm":
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

    场景: 零长度数据 CRC 返回初值 0xFFFF
      当POST "/api/test/asw/rzc/nvm":
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
