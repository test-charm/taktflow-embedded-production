# FZC 激光雷达障碍物检测 (Swc_Lidar) E2E 测试设计

## 被测功能

**FZC ASW TFMini-S 激光雷达 SWC — 9 字节 UART 帧解析（帧同步 / 校验和 / 距离
与信号强度提取）、四级渐进区域分类（clear/warning/braking/emergency）、
范围与信号合理性检查、卡滞检测、帧超时与恢复、故障安全默认（0cm + FAULT 区）、
GetDistance 诊断读取**

覆盖链路：

```text
TFMini-S UART 帧（9 字节：0x59 0x59 + 距离(LE) + 信号强度(LE) + 保留 + 校验和）
  → Uart_ReadRxData（UART MCAL 循环缓冲）
  → Swc_Lidar_MainFunction（10ms 周期）
  → 帧同步扫描（最多 32 字节找 0x59 0x59 头）
  → 校验和验证（低 8 位求和，失败 → CHECKSUM DTC）
  → 距离 / 信号强度提取（小端 16 位）
  → 超时检测（≥100 周期无有效帧 → TIMEOUT DTC）
  → 范围合理性（2..1200cm 之外 → 故障）
  → 信号合理性（<100 → SIGNAL_LOW DTC）
  → 卡滞检测（相同读数 50 周期 → STUCK DTC）
  → 区域分类（≤20 emergency / ≤50 braking / ≤100 warning / 其余 clear）
  → 故障安全输出（故障时距离 0cm、区域 FAULT）
  → Rte_Write（距离 / 信号 / 区域 / 故障 4 信号）
  → Dem DTC 报告（超时/校验和/卡滞/信号过低 四类）
```

与既有 ASW E2E（CVC `Swc_Pedal`/`Swc_VehicleState`/`Swc_EStop`/`Swc_CvcCom`、
FZC `Swc_Steering`/`Swc_Brake`）一致，本测试通过测试专用 API 在原生测试框架内
执行真实的 `Swc_Lidar.c` 生产代码。TFMini-S 帧经 UART 注入，输出经 RTE/DEM
观察。

## 被测代码流程图

```
                    ┌─────────────────────┐
                    │ Swc_Lidar_Init       │
                    │ ConfigPtr==NULL?     │
                    │   Y→ 保持未初始化    │
                    │   N→ 状态全清零      │
                    └─────────┬───────────┘
                              │
               ┌──────────────▼──────────────┐
               │ Swc_Lidar_MainFunction()     │
               └──────────────┬──────────────┘
                              │
         Step1: Initialized!=TRUE? ──Y──→ return（未初始化空转）
                              │N
         Step2: CfgPtr==NULL? ──Y──→ return（NULL 配置守卫）
                              │N
         Step3: Lidar_ParseFrame()
                · 帧同步：逐字节扫描 ≤32 字节找 0x59 0x59
                  · UART 无数据/读失败 → E_NOT_OK
                  · 头未找到 → E_NOT_OK
                · 读剩余 7 字节（不足 7 字节 → E_NOT_OK）
                · 校验和：bytes0-7 求和低 8 位 == byte8？
                  · 失败 → ChecksumError=TRUE、CHECKSUM DTC FAILED
                · 提取距离(bytes2-3 LE) 与信号强度(bytes4-5 LE)
                              │
         Step4: parse==E_OK?
                · Y → frame_received=TRUE、TimeoutCounter=0
                · N → TimeoutCounter++
                      · ChecksumError → new_fault=1（立即故障）
                              │
         Step5: TimeoutCounter ≥ timeoutMs(100)?
                · Y → new_fault=1、TIMEOUT DTC FAILED、计数钳位
                              │
         Step6: frame_received && !fault（合理性检查）
                · 距离 ∉ [2,1200] → new_fault=1
                · 信号 < 100 → new_fault=1、SIGNAL_LOW DTC FAILED
                · 无故障 → 卡滞检测
                  · dist == PrevDistance → StuckCounter++
                    · ≥49 → new_fault=1、STUCK DTC FAILED
                  · 否则 StuckCounter=0
                  · PrevDistance = dist
                              │
         Step7: 输出
                · 故障 → 距离=0、信号=0、区域=FAULT、持久故障计数++
                · 有效帧 → 距离=raw、信号=raw、区域=ClassifyZone、
                  · 持久故障计数=0、4 个 DTC 全部 PASSED
                · 无帧无故障 → 保留上一周期输出
                              │
         Step8: Rte_Write（距离/信号/区域/故障 4 信号）
```

区域分类（`Lidar_ClassifyZone`）：

```
             ┌────────────┐
             │ distance   │
             └─────┬──────┘
        dist ≤ 20? ──Y──→ EMERGENCY(3)
             │N
        dist ≤ 50? ──Y──→ BRAKING(2)
             │N
        dist ≤ 100? ──Y──→ WARNING(1)
             │N
             └─────→ CLEAR(0)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Lidar_Init()` | `false`（先 Init）、`true`（未初始化守卫） | When — 执行控制 |
| `initNull` | 是否调用 `Swc_Lidar_Init(NULL)` | `false`、`true`（NULL 配置守卫） | When — 执行控制 |
| `cycles` | MainFunction 调用次数 | `1`、`40`/`48`/`49`/`50`（卡滞边界）、`99`/`100`（超时边界） | When — 执行控制 |
| `distCm` | 注入帧的距离（cm） | `1`/`2`（下界）、`15`/`20`/`21`（emergency 边界）、`50`/`51`（braking 边界）、`80`/`100`/`101`（warning 边界）、`200`/`500`（clear）、`1200`/`1300`（上界） | When — 数据注入 |
| `signal` | 注入帧的信号强度 | `99`/`100`（信号下限边界）、`500`（健康） | When — 数据注入 |
| `noFrame` | UART 无数据（超时路径） | `false`（有帧）、`true`（无帧） | When — 故障注入 |
| `badChecksum` | 损坏帧校验和字节 | `false`、`true`（校验和故障） | When — 故障注入 |
| `garbageHeader` | 注入 32 个非帧头字节 | `false`、`true`（帧同步失败） | When — 故障注入 |
| `partialFrame` | 只注入帧头 + 3 字节 | `false`、`true`（不完整帧） | When — 故障注入 |
| `uartFailAt` | UART 驱动读失败调用序号 | `0`（不失败）、`1`（同步扫描读失败）、`3`（7 字节载荷读失败） | When — 故障注入 |
| `getDist` | 末尾调用 `Swc_Lidar_GetDistance` | `false`、`true` | When — 执行控制 |
| `getDistNull` | 末尾调用 `Swc_Lidar_GetDistance(NULL)` | `false`、`true`（NULL 指针守卫） | When — 执行控制 |

> 输出因子完全由输入因子确定，故不做等价类/边界值分析；每个用例只记录
> 期望输出值。

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `distance` | RTE `FZC_SIG_LIDAR_DIST`（距离 cm） | `0..1200`、`0`（故障/未初始化） |
| `signal` | RTE `FZC_SIG_LIDAR_SIGNAL`（信号强度） | `0..65535`、`0`（故障/未初始化） |
| `zone` | RTE `FZC_SIG_LIDAR_ZONE`（区域） | `0`=CLEAR、`1`=WARNING、`2`=BRAKING、`3`=EMERGENCY、`4`=FAULT |
| `fault` | RTE `FZC_SIG_LIDAR_FAULT` | `0`=无、`1`=故障 |
| `demTimeout`/`demChecksum`/`demStuck`/`demSignalLow` | `Dem_ReportErrorStatus` 每 DTC 最近状态 | `0`=PASSED、`1`=FAILED、`-1`=未报告 |
| `getDistStatus` / `getDist` | `Swc_Lidar_GetDistance` 返回值/距离 | `0`=E_OK、`1`=E_NOT_OK |

## 测试用例

> Feature 使用 Gherkin `规则`（Rule）将用例按被测行为分组：
> - **规则: 初始化守卫与健康帧解析**：Init 双守卫 / 健康帧解析 / 区域分类 /
>   GetDistance，共 11 场景。
> - **规则: 范围与信号合理性检查**：距离越界 / 信号过低 / 边界值，共 6 场景。
> - **规则: 帧错误处理**：校验和 / 帧头不同步 / 不完整帧 / UART 驱动读失败，
>   共 5 场景。
> - **规则: 卡滞检测**：49 周期不触发 / 50 周期触发 / 读数变化重置，共 3 场景。
> - **规则: 帧超时与恢复**：99 周期未达门限 / 100 周期触发 / 恢复 /
>   无帧保留上一周期输出，共 4 场景。
>
> 每个用例经 `POST /api/test/asw/fzc/lidar` 一次运行驱动真实
> `Swc_Lidar.c`；多阶段脚本中阶段顺序执行、模块状态跨阶段保留
> （如先建立健康帧基线再注入故障）。

### 规则: 初始化守卫与健康帧解析

| 用例 | 阶段序列 | 期望 distance | 期望 zone | 期望 fault | 期望 getDistStatus |
|---|---|---|---|---|---|
| uninitialized_guard | P0: skipInit=true,getDist=true | 0 | 0 | 0 | 1（E_NOT_OK） |
| init_null_guard | P0: initNull=true | 0 | 0 | 0 | — |
| healthy_clear_500 | P0: distCm=500,signal=500 | 500 | 0（CLEAR） | 0 | — |
| warning_zone_80 | P0: distCm=80,signal=500 | 80 | 1（WARNING） | 0 | — |
| braking_zone_40 | P0: distCm=40,signal=500 | 40 | 2（BRAKING） | 0 | — |
| emergency_zone_15 | P0: distCm=15,signal=500 | 15 | 3（EMERGENCY） | 0 | — |
| zone_boundary_warning_100 | P0: distCm=100,signal=500 | 100 | 1（WARNING） | 0 | — |
| zone_boundary_braking_50 | P0: distCm=50,signal=500 | 50 | 2（BRAKING） | 0 | — |
| zone_boundary_emergency_20 | P0: distCm=20,signal=500 | 20 | 3（EMERGENCY） | 0 | — |
| get_dist_reads_current | P0: distCm=500,signal=500,getDist=true | 500 | — | — | 0（E_OK） |
| get_dist_null_pointer | P0: distCm=500,signal=500,getDistNull=true | — | — | — | 1（E_NOT_OK） |

### 规则: 范围与信号合理性检查

| 用例 | 阶段序列 | 期望 distance | 期望 zone | 期望 fault | 期望 demSignalLow |
|---|---|---|---|---|---|
| range_over_max_1300 | P0: distCm=1300,signal=500 | 0 | 4（FAULT） | 1 | — |
| range_under_min_1 | P0: distCm=1,signal=500 | 0 | 4（FAULT） | 1 | — |
| range_boundary_min_2 | P0: distCm=2,signal=500 | 2 | 3（EMERGENCY） | 0 | — |
| range_boundary_max_1200 | P0: distCm=1200,signal=500 | 1200 | 0（CLEAR） | 0 | — |
| signal_low_99 | P0: distCm=200,signal=99 | 0 | 4（FAULT） | 1 | 1（FAILED） |
| signal_boundary_100 | P0: distCm=200,signal=100 | 200 | — | 0 | 0（PASSED） |

### 规则: 帧错误处理（校验和 / 帧同步 / 不完整帧 / UART 驱动错误）

| 用例 | 阶段序列 | 期望 distance | 期望 zone | 期望 fault | 期望 demChecksum |
|---|---|---|---|---|---|
| checksum_error | P0: distCm=200,signal=500,badChecksum=true | 0 | 4（FAULT） | 1 | 1（FAILED） |
| garbage_header | P0: garbageHeader=true | 0 | 4（初始 FAULT） | 0 | — |
| partial_frame | P0: partialFrame=true | 0 | 4（初始 FAULT） | 0 | — |
| uart_fail_sync_read | P0: distCm=200,signal=500,uartFailAt=1 | 0 | 4（初始 FAULT） | 0 | — |
| uart_fail_payload_read | P0: distCm=200,signal=500,uartFailAt=3 | 0 | 4（初始 FAULT） | 0 | — |

> `garbage_header` / `partial_frame` / `uart_fail_*` 为「无有效帧、暂无故障」
> 路径：帧被丢弃，超时计数 +1（未达 100 门限），输出保留初始状态
> （Init 后区域为 FAULT）。`uartFailAt=1` 使 UART 驱动在帧同步扫描第一次
> 读时返回 E_NOT_OK（覆盖 `ret != E_OK` 分支）；`uartFailAt=3` 使驱动在
> 找到帧头后的 7 字节载荷读时返回 E_NOT_OK（覆盖第二条 `ret != E_OK`
> 分支）。

### 规则: 卡滞检测 (Stuck Detection)

| 用例 | 阶段序列 | 期望 distance | 期望 zone | 期望 fault | 期望 demStuck |
|---|---|---|---|---|---|
| stuck_below_threshold | P0: cycles=49,distCm=200,signal=500 | 200 | 0（CLEAR） | 0 | 0（PASSED） |
| stuck_triggered_50 | P0: cycles=50,distCm=200,signal=500 | 0 | 4（FAULT） | 1 | 1（FAILED） |
| stuck_reset_on_change | P0: 40×distCm=200; P1: 1×distCm=201; P2: 48×distCm=201 | 201 | 0（CLEAR） | 0 | — |

### 规则: 帧超时与恢复

| 用例 | 阶段序列 | 期望 distance | 期望 zone | 期望 fault | 期望 demTimeout |
|---|---|---|---|---|---|
| timeout_below_threshold | P0: cycles=99,noFrame=true | 0 | 4（初始 FAULT） | 0 | — |
| timeout_triggered_100 | P0: cycles=100,noFrame=true | 0 | 4（FAULT） | 1 | 1（FAILED） |
| timeout_recovers | P0: 100×noFrame; P1: 1×distCm=200,signal=500 | 200 | 0（CLEAR） | 0 | 0（PASSED） |
| keep_previous_no_frame | P0: 1×distCm=500,signal=500; P1: 1×noFrame | 500 | 0（CLEAR） | 0 | — |

> 阶段序列中未列出的因子取默认值（`cycles=1`、`distCm=0`、`signal=0`、
> 各故障注入=0）。「P0..Pn」为刺激阶段；服务端按存储前置 + 刺激顺序执行。
> 本特性全部用例无存储前置（`FzcLidarSetup: { phases: [] }`），
> 阶段序列即完整脚本。

## 代码路径覆盖

- `Swc_Lidar_Init` 全部可执行行 ✅
  - NULL 配置守卫（`ConfigPtr==NULL` → 保持未初始化）✅ `init_null_guard`
  - 正常初始化（全部状态清零 + `Initialized=TRUE`）✅ 全部已初始化场景
- `Swc_Lidar_MainFunction` 全部可执行行 ✅
  - 未初始化守卫 ✅ `uninitialized_guard`
  - `CfgPtr==NULL` 守卫 — **不可达**（`Init` 保证一致性）
  - 帧解析（E_OK / E_NOT_OK 双路径 + ChecksumError 立即故障）✅
  - 超时检测（≥100 触发 / <100 未触发 / 计数钳位 / 恢复清零）✅
  - 范围合理性（<2 与 >1200 双侧）✅
  - 信号合理性（<100 触发 DTC / =100 边界）✅
  - 卡滞检测（<49 不触发 / ≥49 触发 / 读数变化重置 / Prev 更新）✅
  - 故障安全输出（距离 0 / 信号 0 / 区域 FAULT / 持久故障计数++）✅
  - 有效帧输出（距离/信号/区域 + 4 DTC PASSED + 持久计数清零）✅
  - 无帧无故障保留上一周期输出 ✅ `keep_previous_no_frame`
  - Rte_Write 4 信号 ✅
- `Lidar_ParseFrame` 全部可执行行 ✅
  - NULL 指针守卫 — **不可达**（仅 MainFunction 内部以非 NULL 实参调用）
  - 帧同步扫描（头找到 / 无数据 / 头未找到）✅
  - 剩余 7 字节读取（完整 / 不足 7 字节 / UART 驱动读失败）✅
  - 校验和验证（通过 / 失败→CHECKSUM DTC）✅
  - 距离与信号提取（小端）✅
- `Lidar_ClassifyZone` 全部可执行行 ✅
  - `CfgPtr==NULL` 守卫 — **不可达**（MainFunction 前置判断）
  - EMERGENCY（≤20）/ BRAKING（≤50）/ WARNING（≤100）/ CLEAR ✅
- `Swc_Lidar_GetDistance` 全部可执行行 ✅
  - 未初始化 → E_NOT_OK ✅ `uninitialized_guard`
  - NULL 指针 → E_NOT_OK ✅ `get_dist_null_pointer`
  - 正常读取 → E_OK ✅ `get_dist_reads_current`

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Lidar.c.gcov.html` 实测（2026-08-16 全量套件 160 场景运行后）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **96.6%**（168 / 174 行） |
| **分支覆盖** | **92.6%**（63 / 68 分支） |
| **函数覆盖** | **100%**（5 / 5） |

覆盖到的函数（实测命中次数）：
`Lidar_ClassifyZone`（201）、`Lidar_ParseFrame`（511）、
`Swc_Lidar_Init`（29）、`Swc_Lidar_MainFunction`（513）、
`Swc_Lidar_GetDistance`（3）。

> 下表「实测命中」为完整套件（160 个场景）运行后的累积值；每次运行因
> 容器重启会重新累积，具体数字可能不同，但覆盖关系不变。

---

## 行覆盖分析（96.6%，168/174）

行覆盖反映**每一行是否被执行**。6 行未覆盖，全部为**结构性不可达的防御性
NULL 指针守卫**（见下方「未覆盖行说明」）。其余 168 行全部覆盖，逐行映射
如下。

### 逐函数代码行覆盖映射

#### Swc_Lidar_Init（L179-199）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L181-185 | `if (ConfigPtr == NULL)` → 保持未初始化 | `init_null_guard`（initNull=true） | 2 |
| L187-198 | 正常初始化（状态清零 + `Initialized=TRUE`） | 全部已初始化场景 | 51 |

#### Lidar_ParseFrame（L75-148）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L82-84 | `if ((dist_out==NULL)\|\|(strength_out==NULL))` → E_NOT_OK | **未覆盖** — 仅 MainFunction 以 `&raw_dist`/`&raw_signal` 实参调用，恒非 NULL | 0 |
| L96-98 | `while (max_scan>0)` + 读 1 字节 | 全部有 UART 数据场景 | 1496 |
| L99-101 | `(ret != E_OK) \|\| (bytes_read == 0)` → E_NOT_OK（无数据/驱动错误） | `noFrame` 系列、`uart_fail_sync_read`（uartFailAt=1） | 600 |
| L102 | `max_scan--` | 有字节但非完整帧头场景 | 894 |
| L103-107 | 逐字节匹配 0x59 头，连续 2 字节则 break | 全部有效帧场景 | 415 |
| L108-110 | 非头字节 → `sync_count=0` | `garbage_header` | 64 |
| L113-115 | 扫描耗尽未找到头 → E_NOT_OK | `garbage_header` | 2 |
| L118-121 | 头已找到，读剩余 7 字节 | 全部有效帧场景 | 415 |
| L122-124 | `(ret != E_OK) \|\| (bytes_read < 7)` → E_NOT_OK（不完整/驱动错误） | `partial_frame`、`uart_fail_payload_read`（uartFailAt=3） | 2 |
| L128-131 | 校验和求和（bytes0-7） | 全部帧通过同步场景 | 3304 |
| L133-137 | 校验和不匹配 → ChecksumError + CHECKSUM DTC FAILED | `checksum_error`（badChecksum） | 2 |
| L140-145 | 提取距离与信号（小端） | 全部有效帧场景 | 411 |
| L147 | 返回 E_OK | 全部有效帧场景 | 411 |

#### Lidar_ClassifyZone（L154-173）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L156-158 | `if (CfgPtr==NULL)` → ZONE_FAULT | **未覆盖** — MainFunction 已先判 CfgPtr 非空，不可达 | 0 |
| L160-162 | `distance <= emergencyDistCm` → EMERGENCY | `emergency_zone_15`、`zone_boundary_emergency_20`、`range_boundary_min_2` | 6 |
| L164-166 | `distance <= brakeDistCm` → BRAKING | `braking_zone_40`、`zone_boundary_braking_50` | 4 |
| L168-170 | `distance <= warnDistCm` → WARNING | `warning_zone_80`、`zone_boundary_warning_100` | 4 |
| L172 | 其余 → CLEAR | `healthy_clear_500`、`range_boundary_max_1200`、卡滞/超时恢复系列 | 389 |

#### Swc_Lidar_MainFunction（L205-343）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L213-215 | 未初始化守卫 | `uninitialized_guard`（skipInit=true） | 4 |
| L217-219 | `if (CfgPtr==NULL)` 返回 | **未覆盖** — `Init` 保证 `Initialized=TRUE` 时 `CfgPtr` 非空 | 0 |
| L221-223 | `new_fault=0`、`frame_received=FALSE`、`ChecksumError=FALSE` | 全部已初始化场景 | 1017 |
| L228-230 | `raw_dist=0`、`raw_signal=0`、调用 ParseFrame | 全部已初始化场景 | 1017 |
| L232-234 | `parse==E_OK` → 置帧已收、超时计数清零 | 全部有效帧场景 | 411 |
| L254 | 解析失败 → 超时计数++ | `noFrame`/`garbage_header`/`partial_frame`/`uart_fail_*` 系列 | 606 |
| L257-259 | `ChecksumError` → 立即置故障 | `checksum_error` | 2 |
| L266-270 | `TimeoutCounter >= timeoutMs` → TIMEOUT DTC FAILED + 钳位 | `timeout_triggered_100`（另 `timeout_recovers` 前置） | 4 |
| L275 | 合理性门控 `frame_received && !new_fault` | 全部帧收且无故障场景 | 1017 |
| L277-280 | 范围检查（<rangeMin \|\| >rangeMax）→ 故障 | `range_over_max_1300`、`range_under_min_1`（双侧） | 4 |
| L283-286 | 信号 < signalMin → 故障 + SIGNAL_LOW DTC FAILED | `signal_low_99` | 2 |
| L291-297 | 卡滞：相同读数 → 计数++，≥49 触发 STUCK DTC FAILED | `stuck_triggered_50`（第 50 周期） | 2 |
| L298-300 | 卡滞：读数变化 → 计数清零 | `stuck_reset_on_change`、`stuck_below_threshold` 首周期 | 37 |
| L301 | `PrevDistance = raw_dist` | 全部有效帧无故障场景 | 405 |
| L308 | `Lidar_Fault = new_fault` | 全部 | 1017 |
| L310-317 | 故障 → 距离 0 / 信号 0 / 区域 FAULT / 持久故障计数++ | 全部故障场景（范围/信号/校验/卡滞/超时/组合） | 14 |
| L318-322 | 有效帧 → 距离/信号/区域 = ClassifyZone | 全部有效帧无故障场景 | 403 |
| L325 | 有效帧 → 持久故障计数清零 | 同上 | 403 |
| L328-331 | 有效帧 → 4 个 DTC 全部 PASSED | 全部有效帧无故障场景（含 `timeout_recovers`/卡滞恢复） | 403 |
| L332-334 | 无帧无故障 → 保留上一周期输出 | `keep_previous_no_frame`、`timeout_below_threshold` | 600 |
| L339-342 | Rte_Write 距离/信号/区域/故障 | 全部已初始化场景 | 1017 |

#### Swc_Lidar_GetDistance（L349-361）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L351-353 | 未初始化 → E_NOT_OK | `uninitialized_guard`（skipInit+getDist） | 2 |
| L355-357 | NULL 指针 → E_NOT_OK | `get_dist_null_pointer` | 2 |
| L359-360 | 正常读取当前距离 → E_OK | `get_dist_reads_current` | 2 |

> 未列出的行号为声明、注释、空行或不可达分支占位行（llvm-cov/lcov 计入
> 非可执行行，见下方说明）。

---

## 未覆盖行说明（6 行）

| 行号 | 代码 | 不可覆盖原因 |
|---|---|---|
| L82-84 | `Lidar_ParseFrame` 中 `if ((dist_out==NULL) \|\| (strength_out==NULL))` → E_NOT_OK | **结构性不可达**。`Lidar_ParseFrame` 为静态函数，仅由 `Swc_Lidar_MainFunction`（L230）以 `&raw_dist`/`&raw_signal`（栈上局部变量地址）调用，实参恒非 NULL。防御性空指针守卫，任何合法输入下都不可能触发 |
| L156-158 | `Lidar_ClassifyZone` 中 `if (Lidar_CfgPtr == NULL)` → ZONE_FAULT | **结构性不可达**。`MainFunction` 在 L217-219 已前置判断 `CfgPtr` 非空，且 `Init` 仅在传入非 NULL 配置时置 `Initialized=TRUE`，故进入 `ClassifyZone` 时 `CfgPtr` 恒非空 |
| L217-219 | `MainFunction` 中 `if (Lidar_CfgPtr == NULL)` 返回 | **结构性不可达**。`Swc_Lidar_Init` 仅在传入非 NULL 配置时置 `Initialized=TRUE`；NULL 配置时 `Initialized=FALSE`，已在 L213-215 返回。因此 `Initialized==TRUE ⇔ CfgPtr≠NULL`，此守卫不可能进入 |

> 以上 6 行均为 ISO 26262 编码规范要求的防御性代码（空指针守卫），在
> **任何合法生产输入下都不可能触发**，属「结构不可达」而非「测试遗漏」。
> 单元测试中同样无法通过生产路径覆盖这些分支。

---

## 分支覆盖分析（92.6%，63/68）

未命中（not taken）的 5 个分支：

| 分支 | 位置 | 未命中原因 |
|---|---|---|
| `dist_out == NULL_PTR`（true 侧） | L82 | 防御性空指针守卫，MainFunction 恒传非 NULL 实参，不可达 |
| `strength_out == NULL_PTR`（true 侧） | L82 | 同上 |
| `Lidar_CfgPtr == NULL_PTR`（ClassifyZone，true 侧） | L156 | 前置守卫（L217-219）已排除，不可达 |
| `Lidar_CfgPtr == NULL_PTR`（MainFunction，true 侧） | L217 | Init 保证一致状态，不可达 |
| `new_fault == 0u`（false 侧） | L275 | 当 `frame_received==TRUE` 时（本周期解析成功），`new_fault` 必为 0（校验和错误路径必然解析失败）；反之短路跳过。该分支与语义互斥，结构上不可达 |

> 全部 63 个命中的分支两侧均已覆盖；未命中分支全部为防御性/结构不可达。
> `ret != E_OK` 的 UART 驱动错误分支已由 `uart_fail_sync_read`（L99）与
> `uart_fail_payload_read`（L122）专门覆盖。

---

## 覆盖总结

| 维度 | 覆盖 | 未覆盖 | 未覆盖原因 |
|---|---|---|---:|
| 行 | 96.6%（168/174） | 6 行 | 全部为防御性空指针守卫（结构不可达） |
| 分支 | 92.6%（63/68） | 5 个 | 全部为防御性/语义互斥的不可达分支 |
| 函数 | 100%（5/5） | — | — |

**结论**：`Swc_Lidar` 的全部可执行逻辑（含 5 个函数、4 类 DTC、帧同步与
校验和、四级区域分类、范围/信号合理性、卡滞检测、超时与恢复、故障安全
输出、GetDistance）均由 E2E 测试覆盖。6 行未覆盖代码全部为主机厂级防御性
空指针守卫，通过生产输入无法触发，符合预期。
