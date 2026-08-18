# language: zh-CN
功能: SC 运行时状态机 (sc_state)

  sc_state.c 权威运行时状态机（GAP-SC-006 / SWR-SC-025，ASIL D）的端到端
  测试：合法迁移 INIT→MONITORING→FAULT/KILL、FAULT→KILL 被接受；非法迁移
  拒绝且状态不变（fail-closed）；KILL 为终态无迁出边；未知内部状态强制
  KILL（fail-closed，经 UNIT_TEST setRaw 注入钩子驱动）。

  背景:
    假如存在:
      """
      ScStateSetup: {
        phases: []
      }
      """

  规则: 初始化与读取 — SC_State_Init / SC_State_Get

    SC_State_Init 将状态置为 SC_STATE_INIT(0)；SC_State_Get 返回当前状态。

    场景: 初始化后状态为 INIT
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 0
        results[0]: {
          op: init
          state: 0
        }
      }
      """

  规则: 合法迁移 — SC_State_Transition

    INIT→MONITORING、MONITORING→FAULT、MONITORING→KILL、FAULT→KILL 四条
    合法边被接受（ret=1），状态更新为目标状态。

    场景: INIT 迁移到 MONITORING 被接受
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json.results[1]: {
        op: transition
        ret: 1
        state: 1
      }
      """

    场景: MONITORING 迁移到 FAULT 被接受
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 2
        results[1]: {
          op: transition
          ret: 1
          state: 1
        }
        results[2]: {
          op: transition
          ret: 1
          state: 2
        }
      }
      """

    场景: MONITORING 迁移到 KILL 被接受
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 3
        results[1]: {
          op: transition
          ret: 1
          state: 1
        }
        results[2]: {
          op: transition
          ret: 1
          state: 3
        }
      }
      """

    场景: FAULT 迁移到 KILL 被接受
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 },
          { "op": "transition", "newState": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 3
        results[3]: {
          op: transition
          ret: 1
          state: 3
        }
      }
      """

  规则: 非法迁移 — 拒绝且状态不变（fail-closed）

    任一状态下尝试非法目标状态：Transition 返回 0（FALSE），状态保持不变。

    场景: INIT 状态迁移到 FAULT 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 0
        results[1]: {
          op: transition
          ret: 0
          state: 0
        }
      }
      """

    场景: INIT 状态迁移到 KILL 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 0
        results[1]: {
          op: transition
          ret: 0
          state: 0
        }
      }
      """

    场景: INIT 状态迁移到 INIT 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 0
        results[1]: {
          op: transition
          ret: 0
          state: 0
        }
      }
      """

    场景: MONITORING 状态迁移到 INIT 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 1
        results[2]: {
          op: transition
          ret: 0
          state: 1
        }
      }
      """

    场景: MONITORING 状态迁移到 MONITORING 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 1
        results[2]: {
          op: transition
          ret: 0
          state: 1
        }
      }
      """

    场景: FAULT 状态迁移到 INIT 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 },
          { "op": "transition", "newState": 0 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 2
        results[3]: {
          op: transition
          ret: 0
          state: 2
        }
      }
      """

    场景: FAULT 状态迁移到 MONITORING 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 },
          { "op": "transition", "newState": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 2
        results[3]: {
          op: transition
          ret: 0
          state: 2
        }
      }
      """

    场景: FAULT 状态迁移到 FAULT 被拒绝
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 },
          { "op": "transition", "newState": 2 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 2
        results[3]: {
          op: transition
          ret: 0
          state: 2
        }
      }
      """

    场景: KILL 终态拒绝所有迁出边
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 3 },
          { "op": "transition", "newState": 0 },
          { "op": "transition", "newState": 1 },
          { "op": "transition", "newState": 2 },
          { "op": "transition", "newState": 3 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 3
        results[3]: {
          op: transition
          ret: 0
          state: 3
        }
        results[4]: {
          op: transition
          ret: 0
          state: 3
        }
        results[5]: {
          op: transition
          ret: 0
          state: 3
        }
        results[6]: {
          op: transition
          ret: 0
          state: 3
        }
      }
      """

  规则: 未知状态 fail-closed — default 分支强制 KILL

    内部状态被置为非法值（0xFF，仅内存损坏可达）后，任何 Transition 都落入
    default 分支：强制置 KILL(3) 并返回 FALSE（fail-closed，绝不留在未知态）。

    场景: 未知内部状态强制 KILL
      当POST "/api/test/asw/sc/state":
      """
      {
        "phases": [
          { "op": "init" },
          { "op": "setRaw", "state": 255 },
          { "op": "transition", "newState": 1 }
        ]
      }
      """
      那么response should be:
      """
      body.json: {
        state: 3
        results[2]: {
          op: transition
          ret: 0
          state: 3
        }
      }
      """
