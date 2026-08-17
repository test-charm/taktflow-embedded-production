# language: zh-CN
功能: FZC NVM 存储 (Swc_FzcNvm)

  Swc_FzcNvm NVM 存储 SWC 的端到端测试：DTC 持久化（20 槽、首次空槽写入、
  满槽拒绝、CRC-16 损坏检测）与舵机/制动/激光雷达校准数据（CRC-16 保护、
  NVM 后端重新初始化重载、损坏时回退默认值），以及未初始化守卫与公开 CRC API。

  背景:
    假如存在:
      """
      FzcNvmSetup: {
        phases: []
      }
      """

  规则: 初始化与未初始化守卫 — Swc_FzcNvm_Init

    调用 Swc_FzcNvm_Init 后模块就绪：20 个 DTC 槽清空，校准块为编译期默认值；
    若后端校准 CRC 损坏，Init 内部立即回退默认值。未初始化时 StoreDtc/LoadDtc/
    StoreCal 返回 E_NOT_OK，LoadCal 返回默认值并返回 E_NOT_OK。

    场景: 初始化后默认校准就绪
      当POST "/api/test/asw/fzc/nvm":
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
        occupiedSlots: 0
        results: | op       | ret | steerCenterOffset | steerGain | brakePosOffset | brakeGain | lidarWarnCm | lidarBrakeCm | lidarEmergencyCm |
                 | readCal  | 0   | 0                 | 100       | 0              | 100       | 100         | 50           | 20               |
      }
      """

    场景: 重新初始化后从后端重载自定义校准
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "steerCenterOffset": 5, "steerGain": 110,
            "brakePosOffset": -3, "brakeGain": 95, "lidarWarnCm": 120,
            "lidarBrakeCm": 60, "lidarEmergencyCm": 25 },
          { "op": "init" },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[2]: {
        op: readCal
        ret: 0
        steerCenterOffset: 5
        steerGain: 110
        brakePosOffset: -3
        brakeGain: 95
        lidarWarnCm: 120
        lidarBrakeCm: 60
        lidarEmergencyCm: 25
      }
      """

    场景: 后端校准 CRC 损坏时初始化回退默认值
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "steerCenterOffset": 5, "steerGain": 110,
            "brakePosOffset": -3, "brakeGain": 95, "lidarWarnCm": 120,
            "lidarBrakeCm": 60, "lidarEmergencyCm": 25 },
          { "op": "corruptBackendCalCrc" },
          { "op": "init" },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[3]: {
        op: readCal
        ret: 0
        steerCenterOffset: 0
        steerGain: 100
        brakePosOffset: 0
        brakeGain: 100
        lidarWarnCm: 100
        lidarBrakeCm: 50
        lidarEmergencyCm: 20
      }
      """

    场景: 未初始化时存储 DTC 被拒绝
      当POST "/api/test/asw/fzc/nvm":
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
        occupiedSlots: 0
        results: | op        | ret |
                 | storeDtc  | 1   |
      }
      """

    场景: 未初始化时加载 DTC 被拒绝
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "skipInit": true, "slot": 0 }
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

    场景: 未初始化时读取校准返回默认值并报错
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "readCal", "skipInit": true }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        initialized: 0
        results: | op       | ret | steerCenterOffset | steerGain | brakePosOffset | brakeGain | lidarWarnCm | lidarBrakeCm | lidarEmergencyCm |
                 | readCal  | 1   | 0                 | 100       | 0              | 100       | 100         | 50           | 20               |
      }
      """

    场景: 未初始化时写入校准被拒绝
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "skipInit": true, "steerGain": 123 }
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

  规则: DTC 持久化 — SWR-FZC-031

    StoreDtc 始终写入第一个空槽，状态强制 ACTIVE；写满 20 槽后继续存储必须失败。
    LoadDtc 对空槽、NULL 指针、越界槽位与 CRC 损坏均 fail-closed。

    场景: 存储后重新初始化仍可读回 DTC 冻结帧
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "steerAngle": -20, "brakePos": 50, "lidarDist": 120 },
          { "op": "init" },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        occupiedSlots: 1
        results: | op        | ret | dtcId | status | freezeSteer | freezeBrake | freezeLidar |
                 | storeDtc  | 0   | *     | *      | *           | *           | *           |
                 | init      | *   | *     | *      | *           | *           | *           |
                 | loadDtc   | 0   | 5     | 1      | -20         | 50          | 120         |
      }
      """

    场景: 空槽位加载被拒绝
      当POST "/api/test/asw/fzc/nvm":
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

    场景: 20 个槽位写满后第 21 条 DTC 被拒绝
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 0, "repeats": 20 },
          { "op": "storeDtc", "dtcId": 99, "steerAngle": 1, "brakePos": 2, "lidarDist": 3 },
          { "op": "loadDtc", "slot": 19 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        occupiedSlots: 20
        results: | op        | ret | dtcId | status | freezeSteer | freezeBrake | freezeLidar |
                 | storeDtc  | 0   | *     | *      | *           | *           | *           |
                 | storeDtc  | 1   | *     | *      | *           | *           | *           |
                 | loadDtc   | 0   | 0     | 1      | 0           | 0           | 0           |
      }
      """

    场景: 空记录指针加载被拒绝
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "loadDtc", "slot": 0, "nullRecord": true }
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

    场景: 越界槽位加载被拒绝
      当POST "/api/test/asw/fzc/nvm":
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

  规则: DTC CRC 损坏检测 — SWR-FZC-031

    测试专用钩子翻转 RAM 中已存 DTC 条目的 CRC；LoadDtc 必须检测损坏并拒绝返回。

    场景: DTC CRC 损坏后加载被拒绝
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "steerAngle": 10, "brakePos": 30, "lidarDist": 200 },
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

    场景: 未损坏条目仍可正常加载
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 0, "steerAngle": 20, "brakePos": 40, "lidarDist": 80 },
          { "op": "loadDtc", "slot": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: loadDtc
        ret: 0
        dtcId: 0
        status: 1
        freezeSteer: 20
        freezeBrake: 40
        freezeLidar: 80
      }
      """

    场景: 越界槽位 CRC 损坏请求被忽略且已存条目不受影响
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "steerAngle": 11, "brakePos": 22, "lidarDist": 33 },
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
        freezeSteer: 11
        freezeBrake: 22
        freezeLidar: 33
      }
      """

    场景: 空槽位 CRC 损坏请求被忽略且已存条目不受影响
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "storeDtc", "dtcId": 5, "steerAngle": 12, "brakePos": 23, "lidarDist": 34 },
          { "op": "corruptDtcCrc", "slot": 1 },
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
        freezeSteer: 12
        freezeBrake: 23
        freezeLidar: 34
      }
      """

  规则: 校准读写与 CRC 损坏回退 — SWR-FZC-032

    StoreCal 保存自定义校准并重算 CRC；LoadCal 对 NULL 指针返回 E_NOT_OK；
    RAM 校准 CRC 损坏时返回默认值并 E_NOT_OK。

    场景: 写入校准后可读回自定义值
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "steerCenterOffset": 7, "steerGain": 115,
            "brakePosOffset": -4, "brakeGain": 92, "lidarWarnCm": 130,
            "lidarBrakeCm": 70, "lidarEmergencyCm": 30 },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: readCal
        ret: 0
        steerCenterOffset: 7
        steerGain: 115
        brakePosOffset: -4
        brakeGain: 92
        lidarWarnCm: 130
        lidarBrakeCm: 70
        lidarEmergencyCm: 30
      }
      """

    场景: 空指针读取校准被拒绝
      当POST "/api/test/asw/fzc/nvm":
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
      当POST "/api/test/asw/fzc/nvm":
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

    场景: 校准 CRC 损坏后读取回退默认值
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "steerCenterOffset": 7, "steerGain": 115,
            "brakePosOffset": -4, "brakeGain": 92, "lidarWarnCm": 130,
            "lidarBrakeCm": 70, "lidarEmergencyCm": 30 },
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
        steerCenterOffset: 0
        steerGain: 100
        brakePosOffset: 0
        brakeGain: 100
        lidarWarnCm: 100
        lidarBrakeCm: 50
        lidarEmergencyCm: 20
      }
      """

    场景: 未损坏校准读取始终成功
      当POST "/api/test/asw/fzc/nvm":
      """
      {
        "phases": [
          { "op": "writeCal", "steerGain": 111 },
          { "op": "readCal" }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: readCal
        ret: 0
        steerGain: 111
      }
      """

  规则: CRC-16 计算 — Swc_FzcNvm_Crc16

    CRC-16/CCITT（初值 0xFFFF、多项式 0x1021）对已知数据产生确定性结果；
    NULL 指针返回 0；零长度返回初值 0xFFFF。

    场景: 已知数据 CRC 与参考一致
      当POST "/api/test/asw/fzc/nvm":
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
      当POST "/api/test/asw/fzc/nvm":
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
      当POST "/api/test/asw/fzc/nvm":
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
