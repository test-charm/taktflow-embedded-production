# E2E 拒帧行为后果 (bsw_e2ereject_cvc) E2E 测试设计

> 对应 `docs/bsw-test-analysis.md`「扩展优先级与建议顺序」**高优先级 3：E2E 拒帧
> 行为后果**——「总线损坏帧 → 帧不被采信（`sil_009` 同族，feature 层断言帧属性/
> 行为不变）」。
>
> 定稿日期：2026-08-24。覆盖率实测已回填（见「代码路径覆盖 / 关联测试结果」）。

## 被测功能

**E2E 保护层对损坏帧的拒帧行为与总线级后果（真端到端）**

被测系统为 SIL Docker 栈（7 个真实 vECU 共享宿主机 vcan0）上**插桩的真实 ECU
二进制**（`LLVM_COV=1` 构建，CVC 在 /cov 自然产出 `.profraw`）。本 feature 通过
新增的测试专用网关 API `/api/test/bsw/e2ereject/cvc`（`gateway/fault_inject/
bsw_bus_probe.py` + `app.py`）做两类总线断言：

1. **`corrupt`（CVC 收路径，拒帧 + 行为不变）**：向被测 CVC 的一条 **E2E 保护的
   RX 消息**（feature 场景用 RZC→CVC 的 `Motor_Current` / CAN 0x301，dataId=15，
   发送方 cadence ~294ms 稀疏、注入突发无仲裁竞争，能可靠送达 CVC）注入 `count`
   帧损坏帧（四种破坏模式，见下），随后断言 CVC **自己发出的帧属性不变**
   （CVC_Heartbeat / Vehicle_State 的 `found/busUp/e2e/dataIdOk/crcValid` 全部
   保持），即**损坏帧不被采信、不扰乱正常通信**。覆盖链路（真实）：
   `[损坏帧 → vcan0 → Can_Posix → CanIf_Rx → PduR → Com_RxIndication → E2E_Check
   → E2E_SMCheck → 拒帧(影子清零/不 unpack) → CVC 自身 TX 帧不变]`。

2. **`escalate`（RZC 收路径，拒帧的 Dem 升级后果，`sil_009` 同族）**：先 docker
   停止并等待 CVC 完全退出（消除有效 0x100 对 RZC E2E 滑窗的复位），再向
   Vehicle_State（0x100，dataId=5，RZC 侧 `E2eDemEventId=5` → DTC 0xE601）注入
   `count` 帧损坏帧；断言 RZC **拒掉每一帧损坏扭矩**（电机 TorqueEcho/RPM 保持 0，
   行为不变）、RZC_Heartbeat 继续有效，并且**持续损坏升级为 Dem 确认的 DTC 0xE601
   广播**（CAN 0x500，`ECU_Source=3`）。结束后恢复 CVC。

破坏模式（`corrupt.mode`）对应 `E2E_Check` 的四个失败分支：
`dataid`（DataId 错+CRC 错）→ E2E_STATUS_ERROR(dataId 分支)；`crc`（DataId 对、
CRC 错）→ E2E_STATUS_ERROR(CRC 分支)；`replay`（alive 固定在发送方最近值 →
delta=0）→ E2E_STATUS_REPEATED；`seq`（alive 跳 +13 → delta>MaxDelta=12）→
E2E_STATUS_WRONG_SEQ。注意 CRC-8 不覆盖 byte0，replay/seq 在校正 CRC 后仅改
alive 半字节即可单独命中对应分支。

## 被测代码流程图

### corrupt（CVC 收路径）

```text
[当POST /api/test/bsw/e2ereject/cvc {op: corrupt, target: Motor_Status, mode: …}]
  └─ [gateway vcan0 发送窗口] → 注入 count 帧损坏帧（dataid|crc|replay|seq）
      → [RZC 正常 Motor_Status 帧仍在流转，验证注入确实发生]
      → [真实 CVC：CanIf_RxIndication → PduR → Com_RxIndication]
          → [E2E_Check（dataId / CRC / alive 分支）] → 损坏帧 → ERROR/REPEATED/WRONG_SEQ
          → [E2E_SMCheck 窗口] →（突发帧数≥smInvalid 时）INVALID → 拒帧
             → 影子缓冲清零 + Dem FAILED（demEvt=NONE 则不广播）→ return 丢弃
          → [合法帧 → E2E_STATUS_OK → unpack → RTE/SWC]
      → [总线观测] CVC_Heartbeat / Vehicle_State 帧属性不变
```

### escalate（RZC 收路径 + Dem 升级，SIL-009 同族）

```text
[当POST … {op: escalate}]
  └─ [docker stop cvc → 轮询容器 exited → 总线静默]（隔离有效 0x100）
      └─ [raw-socket 观察线程启动]（纯净环境，主线程预建 socket）
          ├─ 注入 count 帧损坏 Vehicle_State(0x100, dataId 错 + CRC 错)
          │   → [RZC Com_RxIndication → E2E_Check ERROR ×N]
          │   → [E2E_SMCheck 窗口 =10 → INVALID 锁存]
          │   → [Dem_ReportErrorStatus(ev=5, FAILED) ×3 → DTC 0xE601 确认]
          │   → [Dem_MainFunction → PduR → 0x500 DTC_Broadcast(数=0xE601, 源=3)]
          ├─ 观测 Motor_Status: TorqueEcho/RPM 不因损坏扭矩而变（拒帧后果）
          ├─ 观测 RZC_Heartbeat: E2E 仍有效（行为不变/未被击垮）
          └─ [docker restart cvc → 恢复 RUN]
```

## 输入和输出

### 输入因子（corrupt）

| 因子 | 含义 | 默认 | 等价类 | 边界值 |
|---|---|---|---|---|
| `op` | 执行控制 | `corrupt` | — | — |
| `target` | 被损坏的 CVC E2E-RX 消息名（DBC） | `Motor_Current` | `Motor_Current`/`Motor_Status`/`FZC_Heartbeat`/`Lidar_Distance`/`RZC_Heartbeat`（均 CVC 有 E2E RX；选**发送方 cadence 稀疏**者以保证注入帧无仲裁竞争送达；`FZC_Heartbeat` `E2eDemEventId=3`、`RZC_Heartbeat` `E2eDemEventId=4` 用于驱动 Dem 上报/恢复分支） | 不存在/无 E2E 的消息（fail-closed 返回 reason） |
| `mode` | 损坏方式 | `dataid` | `dataid`/`crc`/`replay`/`seq`（四个 E2E_Check 分支各一）<br/>`dlc`（长度错帧，见「无法覆盖」——Com 结构性不可达） | — |
| `count` | 注入帧数（≥smInvalid 才可能触发滑窗锁存） | `12` | 单帧（1）、突发（≥smInvalid=3）、Dem 上报场景（120，配 intervalMs=5，双心跳各 600ms 窗口覆盖 ≥4 心跳周期） | 0（不注入） |
| `intervalMs` | 注入间隔（ms） | `10` | 慢速（>发送方 cadence）/快速（<发送方 cadence，形成连续坏帧） | — |
| `settleMs` | 注入后稳定窗口（ms） | `1500` | — | — |

### 输入因子（escalate）

| 因子 | 含义 | 默认 | 等价类 | 边界值 |
|---|---|---|---|---|
| `op` | 执行控制 | `escalate` | — | — |
| `count` | 注入损坏帧数（需 ≥ smInvalid(10)+Dem 去抖(3)） | `16` | 不足锁存（<10）/足以锁存（≥13） | 10（恰好锁存）、16（留裕量） |
| `intervalMs` | 注入间隔（ms） | `100` | — | — |
| `observeMs` | DTC 观测窗口（ms） | `5000` | — | — |
| `restartCvc` | 结束后恢复 CVC | `true` | — | `false`（隔离模式） |

### 输出因子

| 因子 | 含义 | 期望值 |
|---|---|---|
| `found` / `busUp` | 端点可用 / 总线可用 | `true` / `true` |
| `target` / `canId` / `mode` / `count` | 回显请求参数 | 与请求一致 |
| `injectedOnBus` | 观测到损坏帧数（dataid/crc 为静态可检出；replay/seq 因 DataId/CRC 仍正确仅静态不可检，观测=0 属正常） | dataid/crc ≥ 1 |
| `cvCheartbeatValid` / `vehicleStateValid` | CVC 自身帧 E2E 有效（`crcValid=true`） | `true` / `true` |
| `behaviourUnchanged` | 全部探针 `found∧busUp∧e2e∧dataIdOk∧crcValid` | `true` |
| `corruptOnBus`（escalate） | 总线上观测到的损坏 0x100 帧数 | ≥ 1 |
| `dtcBroadcast` / `dtcCode` / `dtcEcuSource` | RZC Dem 升级广播 | `true` / `0xE601`(=58881) / `3` |
| `dtcFrames` | 观测到的 0xE601 广播帧数 | ≥ 1 |
| `rzcHeartbeatValid` | RZC_Heartbeat E2E 仍有效（`rzc_ok>0 ∧ rzc_fail=0`） | `true` |
| `motorUnchanged` / `motorTorqueMax` / `motorSpeedMax` | 电机不被损坏扭矩驱动 | `true` / `0` / `0` |

## 测试用例

> 最短路径：先验证明端点/总线可用并把**单帧损坏 → 行为不变**作为最小输出
> （不动要锁存的事件），再逐步加长损坏持续（`count`/`intervalMs`）到触发滑窗与
> Dem 升级（escalate）。每个用例一个独立 POST；断言 target 恒为 results[0]。
> 前置条件：`docker compose -f docker/docker-compose.dev.yml up`，vcan0 就绪，
> 被测 ECU（含插桩 CVC）健康。

### 规则: 真端到端 — CVC 收路径拒帧（行为不变）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `e2e_reject_cvc_dataid` | corrupt: target=`Motor_Current`, mode=`dataid`, count=12, intervalMs=10 | injectedOnBus≥1；cvCheartbeatValid=true；vehicleStateValid=true；behaviourUnchanged=true |
| `e2e_reject_cvc_crc` | corrupt: mode=`crc`, count=12, intervalMs=10 | 同上（CRC 分支） |
| `e2e_reject_cvc_replay` | corrupt: mode=`replay`, count=12, intervalMs=10 | behaviourUnchanged=true（REPEATED 分支；静态不可检故不断言 injectedOnBus） |
| `e2e_reject_cvc_seq` | corrupt: mode=`seq`, count=12, intervalMs=10 | behaviourUnchanged=true（WRONG_SEQ 分支） |
| `e2e_reject_cvc_fzc_heartbeat_dem` | corrupt: target=`FZC_Heartbeat` + `RZC_Heartbeat`, mode=`dataid`, 各 count=120, intervalMs=5（两路双 600ms 窗口，覆盖 ≥4 个心跳周期） | behaviourUnchanged=true（损坏被拒；**Dem 上报/恢复分支**：demEvt=3/4 → Com 的 Dem FAILED/PASSED + Dem 事件去抖/确认/广播；双心跳并行注入提高与合法帧周期的相遇概率，实测最终报告 Com L427=4422 次 FAILED、L461=35 次恢复 PASSED，L426-430/L460-464 全部命中） |
| `e2e_reject_cvc_lidar_sm10` | corrupt: target=`Lidar_Distance`(0x220, dataId=13), mode=`dataid`, count=12, intervalMs=10 | behaviourUnchanged=true（**不同 SM 窗口配置**：`E2eSmWindowInvalid=10`（DBC 30ms 周期）需连续 ≥10 坏帧才锁存 INVALID——补充 `test_E2E_*`/`test_E2E_Sm_*` 状态机语义的真实运行面；实测注入 11 帧稳定拒绝、CVC 心跳/VS 有效） |

### 规则: 真端到端 — RZC 收路径持续损坏升级（SIL-009 同族）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `e2e_reject_rzc_escalation` | escalate: count=16, intervalMs=100, observeMs=5000 | corruptOnBus≥1；dtcBroadcast=true；dtcCode=58881(=0xE601)；dtcEcuSource=3；rzcHeartbeatValid=true；motorUnchanged=true |

## 代码路径覆盖

> 覆盖率数据源：插桩真实 CVC SIL ECU 在 feature 运行期间周期 flush `.profraw`
> 到共享 `/cov`，网关 `_collect_ecu_coverage_info` 合并 + `llvm-cov export`
> （对 `/app/firmware/bsw` 不 ignore）→ lcov 报告（`e2e-tests/build/coverage/`）。
> **`./gradlew cucumber` 全量回归后由 build.gradle 的 doLast 自动拉取报告**。

覆盖率口径说明：报告中的可执行行覆盖来自 **插桩 CVC** 在整个运行周期内的真实
执行（`Com_Cfg_Cvc.c` 等为纯数据表、无可插桩行，不进报告）。CVC 的 E2E 拒帧
链路（`Com_RxIndication` → `E2E_Check` → `E2E_SMCheck` → 拒帧/恢复）由本 feature
的 `corrupt` 场景驱动：RZC→CVC 的 `Motor_Current`（0x301，dataId=15，发送 cadence
~294ms 稀疏）注入突发损坏帧，无仲裁竞争地到达 CVC。

⚠️ **快照口径**：每个 `./gradlew cucumber` 调用的 doLast 都会重新生成报告；CVC
进程把计数累积在 `cvc_%p.profraw`。**若套件中靠后的 feature（如 `bsw_rtetaskbodies`
的 ftti）重启了 CVC 容器，进程级计数会从零重新累积**，最终快照只反映最后一次
重启之后的执行。因此这里给出的拒帧分支逐行数据，取自**全量回归后单独再跑一次
定向 feature** 的报告快照（该次运行以 corrupt 场景结尾，是最后触发 CVC E2E 拒帧
的活动）。该做法可复现：`./gradlew cucumber`（全量）→ `./gradlew cucumber
-Pfile=.../bsw_e2ereject_cvc.feature`。

### 覆盖率实测：逐行覆盖映射（CVC 插桩，定向运行后快照）

> 命中计数为本小节所附报告的实测值。列"覆盖"中的语义：
> **常态** = 全量/定向运行期间 CVC 对 vcan0 真实帧的收发（含合法帧观察）；
> **corrupt→`dataid`/`crc`/`replay`/`seq`** = `e2e_reject_cvc_*` 对应的损坏注入；
> **escalate** = `e2e_reject_rzc_escalation`（RZC 行为层）；**ASW** = CVC 应用类
> feature 场景。`MISS(-1)` 行是注释/声明/宏/预处理等 clang 不插桩的不可执行行，
> 不计入可执行行总数；`MISS(0)` 才是覆盖报告口径下的可执行未覆盖行。

#### 1. `firmware/bsw/services/E2E/src/E2E.c`（可执行 155 行，命中 **109**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 69-80 | `E2E_ComputePduCrc`：CRC-8 表累加 + DataId 混入 | 5347 | 常态（Protect/Check 每帧都算 CRC） |
| 86-88 | `E2E_Init` | 1 | ECU 启动 |
| 107-149 | `E2E_Protect`：alive 递增、写 byte0(dataId)、CRC 写 byte1 | 2334 | 常态：CVC 每个 E2E 保护 TX 帧（EStop/心跳/VS/Torque/…） |
| 110-134 | Protect 防御性 NULL/长度校验 | **0** | 不可达：真实链不传 NULL/错长度（见「无法覆盖」） |
| 157-161 | `E2E_Check` 局部量声明 | 3025 | 常态 RX |
| 192-193 | 提取 `rx_counter/rx_data_id/rx_crc` | 3025 | 常态 RX |
| **196-198** | `if (rx_data_id != DataId) → E2E_STATUS_ERROR` | **12** | **corrupt→`dataid`**（Motor_Current DataId 错+CRC 错） |
| **201-204** | CRC 计算与 `if (rx_crc != computed_crc) → ERROR` | **12** | **corrupt→`crc`** |
| **213-215** | `delta==0 → E2E_STATUS_REPEATED` | 67 | **corrupt→`replay`**（+常态 alive 重复） |
| 218-220 | `delta==1 → E2E_STATUS_OK` | 1953 | 常态合法帧 |
| **231-233** | `delta > MaxDeltaCounter → E2E_STATUS_WRONG_SEQ` | 781 | **corrupt→`seq`**（+常态 alive 跳变） |
| 236-237 | `return E2E_STATUS_OK`（尾部） | 981 | 常态合法帧 |
| 164-188 | Check 防御性 NULL/长度校验 | **0** | 不可达（见「无法覆盖」） |
| 227-229 | `PLATFORM_HIL` alive 容差分支 | **0** | SIL 编译未定义 PLATFORM_HIL（HIL 专属） |
| 299-302 | `E2E_SMCheck` NODATA→INIT | 12 | ECU 启动后各保护 RX 首帧 |
| 305-312 | INIT→VALID（OkCount≥1）/INIT 保持 | 12 | 常态/损坏交错 |
| **315-317** | `E2E_SM_VALID → INVALID`（ErrCount≥WindowSizeInvalid=3） | **3** | **corrupt→`*` 突发**（连续坏帧滑窗锁存） |
| **320-324** | `E2E_SM_INVALID → VALID`（恢复≥WindowSizeValid=3） | **3** | corrupt 突发后合法帧恢复 |
| 326-328 | `default` 防御分支 | **0** | 状态枚举完备，不可达 |
| 270-271, 257-259 | SM 空指针/NULL 守卫 | **0** | 内部调用不可达（见「无法覆盖」） |

#### 2. `firmware/bsw/services/E2E/src/E2E_Sm.c`（可执行 76 行，命中 **13**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 27-42 | `E2E_Sm_Init`（窗口字母初始化为 ERROR） | 2 | 仅链接它的测试路径（`test_E2E_SM_asild.c`） |
| 44-124 | `E2E_Sm_Check` 滑窗/状态机主体 | **0** | 生产收路径**不调用**本文件（Com 用 E2E.c 的 `E2E_SMCheck`）；滑窗细节由单元层固化 |
| 29-31 | `E2E_Sm_Init` NULL 守卫 | **0** | 单元层负向用例覆盖 |

#### 3. `firmware/bsw/ecual/CanIf/src/CanIf.c`（可执行 67 行，命中 **37**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 25-34 | `CanIf_Init` | 1 | ECU 启动 |
| 38-71 | `CanIf_Transmit`（PDU→CAN ID 映射、`Can_Write`） | 2380 | 常态：CVC 全部周期 TX |
| 75-108 | `CanIf_RxIndication`（CAN ID 查表→`PduR_CanIfRxIndication`） | 6433 | 常态 RX：**每一条保护/非保护收帧**都经此进入拒帧链 |
| 90-98 | RX 表内可选 `e2eRxCheck` 回调 | **0** | 生成配置中 `e2eRxCheck` 为空（E2E 在 Com 层做），不可达 |
| 110-111 | 未知 CAN ID 静默丢弃 | — | 分支命中（返回前无语句） |
| 113-118 | `CanIf_ControllerBusOff` | **0** | 未触发 bus-off（优先级 5 未落地） |
| 40-53 | Transmit/Init 防御分支 | **0** | 不可达（正常调用路径） |

#### 4. `firmware/bsw/ecual/PduR/src/PduR.c`（可执行 60 行，命中 **26**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 24-33 | `PduR_Init` | 1 | ECU 启动 |
| 37-77 | `PduR_CanIfRxIndication`（RxPduId 路由表→`Com_RxIndication`） | 3951 | 常态 RX：**拒帧链必经路由** |
| 51-57 | `PDUR_DEST_COM` 分支 | 3951 | 本测试（Com 目标） |
| 59-73 | `DCM / CanTp / XCP / default` 分支 | **0** | CVC SIL 走 UDS 场景未覆盖其它目标（HIL/ASW 面） |
| 84-96 | `PduR_Transmit`→`CanIf_Transmit` | 2380 | 常态 TX 路径 |
| 98-108 | `PduR_DcmTransmit`/`PduR_CanTpTransmit` | **0** | 诊断栈（Dcm/CanTp）由 HIL/单元覆盖 |

#### 5. `firmware/bsw/rte/src/Rte.c`（可执行 120 行，命中 **88**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 42-105 | `Rte_DispatchRunnables`（优先级排序分派 + WdgM checkpoint） | 13590 | 常态：10ms/50ms 任务驱动（fzc/rzc/cvc cadence 场景） |
| 118-159 | `Rte_Init`（清零 + 初始值 + 越界守卫） | 1 | ECU 启动；L128-140 越界守卫 **0**（配置合法不可达） |
| **167-186** | `Rte_Write` | 41170 | 常态：com RX unpack 推送 + **拒帧分支 `Rte_Write(0)`（L449 命中 397）** |
| 169-172 | Rte_Write 未初始化守卫 | 2 | 启动窗口内写入（时序边界） |
| 194-218 | `Rte_Read` | 28564 | 常态：SWC 读信号 + Com TX 自动拉取 |
| 225-235 | `Rte_MainFunction`（1ms tick + 分派） | 4999 | 常态主循环 |
| 66-69, 120-140, 201-211 | NULL/越界防御守卫 | **0** | 不可达（合法调用；由 Det/单元覆盖） |

#### 6. `firmware/bsw/services/Dem/src/Dem.c`（可执行 181 行，命中 **140**）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 79-107 | `Dem_Init`（清零 + NvM 恢复 occurrence） | 1 | ECU 启动 |
| 112-170 | `Dem_ReportErrorStatus`（去抖、STATUS 位、确认 DTC） | 2000 | **常态**：各 ASW 场景上报（FAILED 132-151 / PASSED 152-167 见下） |
| 130-151 | FAILED 分支（去抖递增、`CONFIRMED_DTC`、occurrence++） | 4+307 | ASW DTC 场景 + **`e2e_reject_cvc_fzc_heartbeat_dem`**（事件3 损坏→确认）；escalate 的 RZC 拒帧→Dem 链条在 RZC（行为层）断言 |
| 152-167 | PASSED/恢复分支（去抖递减、清 TEST_FAILED） | 1996 | CVC 侧 DTC 恢复场景（含 FZC 心跳损坏恢复后的 Dem PASSED） |
| 172-189 | `Dem_GetEventStatus` | 426 | 常态 SWC 查询 |
| 232-233 | `Dem_SetEcuId` | 1 | 启动 |
| 237-248 | `Dem_SetDtcCode`/`Dem_SetBroadcastPduId` | 6/1 | 启动配置 |
| 265-350 | `Dem_MainFunction`（0x500 DTC 广播打包 + PduR_Transmit + NvM 持久化） | 142 | CVC 侧 DTC 场景广播（2 次实际发送）；escalate 场景的 RZC DTC 0xE601 由总线断言验证 |
| 191-224 | `Dem_GetOccurrenceCounter`/`Dem_ClearAllDTCs` | **0** | UDS 清 DTC 由 HIL/单元覆盖 |
| 113-115, 174-182, 193-204, 238-240 | 防御守卫 / 越界 | **0** | 不可达 |

#### 7. `firmware/bsw/services/Com/src/Com.c`（可执行 570 行，命中 **410**）

##### 7a. `Com_RxIndication`（★拒帧编排点，L362-527）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 362-363, 386-390 | 入口 + 拷贝 PDU 到 `com_rx_pdu_buf` + 复位超时计数 | 3951 | 常态：每一条 RX 帧 |
| 364-382 | NULL/越界防御守卫 | **0** | 不可达（CanIf 保证参数合法） |
| 395-401, 404-405, 408-409, 413-416, 419-421 | 查 `rxPduConfig`→E2E 保护→组 `e2e_cfg`/`sm_cfg`→`E2E_Check`/`E2E_SMCheck` | 3025 | 常态：每条 E2E 保护 RX 帧 |
| **423-425** | `sm_state==E2E_SM_INVALID` → `E2E_FAIL` + 失败计数++ | **53** | **corrupt→`*` 突发**（Motor_Current 连续坏帧锁存 INVALID） |
| **426-430** | `E2eDemEventId != NONE` → Dem FAILED（demEvt=3/4） | **4422** | **`e2e_reject_cvc_fzc_heartbeat_dem`**（CVC 侧 `FZC_Heartbeat` demEvt=3、`RZC_Heartbeat` demEvt=4；`Motor_Current` 为 demEvt=NONE）|
| **431-453** | 影子缓冲逐信号清零 + `Rte_Write(0)`（fail-safe 默认值）+ `return` 丢弃帧 | 53/397 | corrupt 突发（**拒帧后果：SWC 读到安全默认值、损坏帧不 unpack**） |
| **458-466** | INVALID→VALID 恢复：Dem PASSED（460-464）、quality=FRESH | **35** | corrupt 突发后合法帧恢复（FZC_Heartbeat 场景同样触发 Dem PASSED） |
| 467-470 | 记录 prev_sm、结束 E2E 区 | 2972/3951 | 常态 + 恢复 |
| 477-481, 484, 487-488, 491-496, 498-511, 517-521, 527 | 正常 unpack：`Rte_Write` 推送 SWC 信号 + quality=FRESH | 25945/3898 | 常态（E2E 校验通过帧） |
| 486, 490, 512-513, 515-516 | unpack 分支判断 / `>16` 不支持分支 | 部分 | `>16` 分支 **0**（配置不支持，见「无法覆盖」） |

##### 7b. `Com_MainFunction_Tx`（L606-754，周期 TX 调度）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 608-622 | 入口守卫 + 启动延时（50 次） | 499 | 常态主循环（10ms 任务） |
| 627-638 | 遍历 TX PDU；DIRECT/NONE 跳过 | 5388 | 常态 |
| 641-666 | 周期计数器；PERIODIC 到期置 should_send | 2378 | 常态：EStop/心跳/VS/Torque 等周期帧 |
| 647-649, 658-666 | 事件型（cycle=0）/MIXED 分支 | **0** | 生成配置全为 PERIODIC（Body_Control_Cmd 也是 PERIODIC） |
| 669-686 | TRIGGERED_ON_CHANGE 变更检测 | 2371 | 常态 |
| 689-736 | TX 信号自动拉取、`E2E_Protect`、`PduR_Transmit`、快照 | 2378/2334 | 常态：**E2E 保护 TX 帧真实上总线**（bsw_comcfg_bus-probe 校验属性） |
| 737-744 | PduR TX 失败 → stuck 计数 | **0** | 总线/SIL 环境未出现 TX 失败（失败注入面） |
| 745-751 | 未到期跳过路径 | 765 | 常态 |

##### 7c. `Com_MainFunction_Rx`（L756-839，RX 超时监控)

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 758-769 | 入口 + 启动延时跳过 | 499/50 | 常态 |
| 776-788 | 遍历 RX PDU、超时计数++ | 14817 | 常态 |
| 790-818 | 超时：shadow 清零 + quality=TIMED_OUT | 3480 | 常态：peer 帧间隙（非实时节拍下正常） |
| 820-824 | 超时写 COMM_TIMEOUT | **0** | CVC 该 PDU 无 CommStatus 绑定（CommStatus 写 OK 在 827-831 命中 898） |
| 825-831 | FRESH → 写 COMM_OK | 898 | 常态 |
| 832-835 | E2E_FAIL quality 保持 | 83 | corrupt 突发期间的收帧间隙 |
| 803-807 | SINT16 清零分支 | **0** | 配置无 SINT16 RX 信号 |

##### 7d. `Com_SendSignal`/`Com_ReceiveSignal`/`Com_GetRxPduQuality`（L210-358、529-535）

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 212-313 | `Com_SendSignal`（UINT8/16 打包 + pending） | 18195 | 常态 ASW 发送（pedal/estop/…）；SINT16/UINT32/default **0**（配置无） |
| 307 | DIRECT/MIXED 立即触发 | **0** | 生成配置无 DIRECT TX（DTC 广播走 Dem 直发） |
| 317-358 | `Com_ReceiveSignal` | 10917 | 常态 ASW 接收；UINT16/SINT16/UINT32/default **0**（配置主要 UINT8） |
| 531-535 | `Com_GetRxPduQuality`（越界→TIMED_OUT） | 499 | 常态 SWC 查询；越界 **0** |

##### 7e. Com.c 内部状态/辅助

| 行 | 代码 | 命中 | 覆盖来源 |
|---|---|---|---|
| 99-100 | `com_get_byte_offset` | 52260 | 常态（打包/解包每信号） |
| 109-136 | `com_pack_signal_to_pdu`（8/16 位打包） | 8120 | 常态 TX；BitSize>16 分支 **0**（配置无） |
| 142-208 | `Com_Init` | 1 | ECU 启动；防御分支 **0** |
| 539-604 | `Com_FlushTxPdu`/`Com_TriggerIPDUSend`（DIRECT 直发） | **0** | 生成配置全 PERIODIC，无 DIRECT TX 触发（DTC 广播由 Dem 内部直发） |

#### 汇总（较 `bsw_comcfg_cvc` 基线）

| 文件 | 行命中/总 | 变化 |
|---|---:|---|
| `E2E.c` | **109 / 155** | 基线 98 → **+11**（dataId/CRC 错误返回 + SM INVALID 转移） |
| `Com.c` | **410 / 570** | 基线 374 → **+36**（拒帧：影子清零/丢弃 + Dem FAILED/PASSED + INVALID→VALID 恢复） |
| `E2E_Sm.c` | 13 / 76 | 单元层实现，收路径不用（见「无法覆盖」） |
| `CanIf.c` | 37 / 67 | 拒帧链路由经由此层（RX 6433 次） |
| `PduR.c` | 26 / 60 | 拒帧链路由（RX 3951 次） |
| `Rte.c` | 88 / 120 | 拒帧分支 `Rte_Write(0)` 397 次 |
| `Dem.c` | 140 / 181 | CVC 侧 DTC 去抖/广播；escalate 的 RZC Dem 在行为层 |
| 报告总计 | 7346 / 14867（49.4%） | 含 ASW harness + CVC ECU |
## 关联测试结果（实测）

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/bsw_e2ereject_cvc.feature` | **8 scenarios / 53 steps passed**（escalate + 4 种损坏模式 + FZC/RZC Dem 上报恢复 + Lidar SM10 拒帧 + fail-closed；真实总线） |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **781 scenarios / 4719 steps passed**（含本 feature，无回归） |

## 无法覆盖的代码说明

- **NULL / 指针防御分支**（Protect/Check 的 L110-123、L164-177、Com/Rte/PduR/CanIf
  各处守卫）：真实 CAN 链路不可能传入 NULL 指针；这些分支由 Det 报告 + 单元负向
  测试固化，不属于总线可观测行为。
- **E2E_Check 长度变异分支（L185-188）结构性不可达**：`Com_RxIndication` 把
  `e2e_cfg.DataLength` 设为**收到帧的实际长度**（Com.c L405），故 `Length !=
  DataLength` 恒为假；`dlc` 损坏模式已实现（验证过行为不变）但不会触发该分支。
  只有单元测试以固定 `DataLength=8` 直接调用 `E2E_Check`（`test_E2E_asild.c`）
  才能覆盖长度不匹配。
- **`PLATFORM_HIL` alive 容差分支**（L227-229）：仅在 HIL（RT Linux/CAN 桥）编译
  激活；SIL 走 `#else` 的正常 MaxDelta 判定。
- **`E2E_SMCheck` default 防御分支**（L326-328）：状态枚举完备、不可达。
- **INIT→INVALID（L307-308）**：本测试在 RUN 状态下注入突发，常态帧使 SM 先进入
  VALID、再由突发从 VALID→INVALID；启动即遇损坏窗口的 INIT→INVALID 由单元层覆盖。
- **Dem_MainFunction `dtc_code==0` 跳过分支（L283-285）**：CVC 的 `dem_dtc_codes`
  默认表对其相关事件（0-17）均为**非零** UDS code，未映射事件（如 ev21/22）在
  当前场景下不会被确认，故该分支由单元/ASW 去抖场景覆盖。
- **`Motor_Current` 目标的 Dem 上报分支**：`Motor_Current` 的 `E2eDemEventId=
  COM_DEM_EVENT_NONE`，故 dataid/crc/replay/seq 场景不触发 Dem 上报；**Dem
  FAILED/PASSED 已由 `e2e_reject_cvc_fzc_heartbeat_dem`（demEvt=3/4，
  FZC/RZC 双心跳）覆盖**，实测最终报告 Com L426-430 FAILED=4422 次、L460-464
  恢复 PASSED=35 次全部命中；escalate 场景的 `DTC 0xE601` 由 **RZC** 行为层断言
  （RZC 容器未插桩、不进报告）。
- **`E2E_Sm.c` 的 63 个未命中行**：该文件是独立于生产收路径的滑窗实现（链接者仅
  单元测试 `test_E2E_Sm_asild.c`），滑窗细节属单元固化面。
- **RZC 拒帧路径不进本报告**：报告只回收插桩 CVC 的 `.profraw`；RZC 拒帧/锁存/
  DTC 由行为断言覆盖。
- **CVC 拒帧时的行为不变量**：损坏帧被拒 → CVC 自身 TX 帧属性不变
  （`behaviourUnchanged=true`）；无帧/突发/重复发射仍可由总线断言抓到。

### 生产代码与报告口径

- **被测系统是真实 ECU 二进制**：`Com_Cfg_Cvc.c` 等生成配置为纯数据表（编译期
  常量、零可执行行），clang 插桩不产生区间 → 不进 lcov 报告；其一致性由 bus
  观测到的真实帧属性（DLC/周期/E2E 头）间接保证。
- **RZC 拒帧路径的覆盖率不进入本报告**：报告仅回收插桩 CVC 的 `.profraw`
  （fzc/rzc 等容器未设置 `LLVM_PROFILE_FILE`）；RZC 拒帧 + Dem 升级属**行为
  层断言**（`e2e_reject_rzc_escalation` 的 DTC/心跳/电机断言），与 CVC 逐行覆盖
  分开口径。
- **SIL 插桩基础设施**：`Makefile.posix LLVM_COV=1`、`Sil_Coverage.c`、
  `Dockerfile.vecu`、compose `/cov` 卷 —— 均只存在于 SIL 构建/平台 POSIX 层。