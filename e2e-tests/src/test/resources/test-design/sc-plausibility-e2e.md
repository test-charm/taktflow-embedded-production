# SC 扭矩-电流合理性校验 (sc_plausibility) E2E 测试设计

## 被测功能

**SC 扭矩-电流交叉合理性校验（SWR-SC-007/008/009/024，SSR-SC-018，ASIL D）**

SC 平台对 RZC 电机「扭矩指令 vs 实际电流」的交叉校验模块，输出合理性故障
（触发 relay-kill）与蠕动防护（standstill creep guard）两级安全门：

- **SWR-SC-007 — 扭矩-电流查找表**：`lookup_expected_current` 以 16 项
  LUT（0%..100%）线性插值求期望电流：`torque==0` → 0 mA；`torque>=100` →
  25000 mA；0<torque<100 在相邻表项间线性插值。LUT 静态只读，两表均 16 项。
- **SWR-SC-008 — 合理性比较与去抖**：`is_implausible` 先求
  `|expected - actual|` 绝对差；期望<100 mA 时用绝对阈值
  `SC_PLAUS_ABS_THRESHOLD_MA`（2000 mA）；否则用相对阈值 20%（期望×20/100），
  并以绝对阈值 2000 mA 为下限（floor）。不一致累计 `plaus_debounce`，
  达 `SC_PLAUS_DEBOUNCE_TICKS`（本 harness 编译配置 = 10 tick，100ms）
  后置位 `plaus_faulted` 锁存。
- **SWR-SC-009 — 故障锁存与系统 LED**：`plaus_faulted` 一旦置位即锁存
  （`SC_Plausibility_Check` 前置守卫直接返回），系统故障 LED（SYS）点亮；
  Init 可复位。
- **SWR-SC-024 — 备份切断**：若 `cur_ok && SC_Heartbeat_IsFzcBrakeFault()`，
  且实际电流 > `SC_BACKUP_CUTOFF_CURRENT_MA`（1000 mA）累计
  `backup_cutoff_counter`，达 `SC_BACKUP_CUTOFF_TICKS`（10 tick）后同样
  置位 `plaus_faulted` 并点亮 LED；条件不满足立即清零计数器。
- **SSR-SC-018 — 蠕动防护（creep guard）**：`SC_CreepGuard_Check` 独立检查
  「torque==0 且 电流 > 500 mA」的 BTS7960 FET 短路特征，累计
  `creep_debounce`，达 `SC_CREEP_DEBOUNCE_CYCLES`（本 harness 生产配置 = 2
  周期）后置位 `creep_faulted`（**不可清除**，仅断电复位）。
- **启动宽限期**：`SC_Plausibility_Check` 与 `SC_CreepGuard_Check` 共享
  `plaus_startup_grace`（Init 置 `SC_HB_STARTUP_GRACE_TICKS`，本 harness
  编译配置 = 1500 tick），宽限期内直接返回、不读 CAN 也不累计任何计数。

覆盖链路：

```text
测试 API 注入（op / torque / current / vehValid / curValid / brakeFault /
                 repeats / ticks / expected / actual / skipInit）
  → SC_Plausibility_Init()：
       · plaus_debounce=0 / plaus_faulted=FALSE / backup_cutoff_counter=0
       · plaus_startup_grace = SC_HB_STARTUP_GRACE_TICKS（1500）
       · creep_debounce=0 / creep_faulted=FALSE
  → SC_Plausibility_Check()：
       · {plaus_faulted} → return（锁存守卫）
       · {grace>0} → grace--，return（启动宽限）
       · veh_ok = SC_CAN_GetMessage(VEHICLE_STATE)；cur_ok = SC_CAN_GetMessage(MOTOR_CURRENT)
       · {veh_ok && cur_ok} → torque_pct=veh[4]；actual=cur[3]<<8|cur[2]
            expected=lookup_expected_current(torque_pct)
            {is_implausible(expected,actual)} → debounce++ 否则 debounce=0
            {debounce>=10} → faulted=TRUE，LED=1
       · {cur_ok && FzcBrakeFault} → {actual>1000mA} → backup_cutoff_counter++
            {>=10} → faulted=TRUE，LED=1；否则 counter=0；否则 counter=0
  → SC_CreepGuard_Check()：
       · {creep_faulted} → return（非清除锁存）
       · {grace>0} → return（共享宽限）
       · {!veh_ok || !cur_ok} → return
       · {torque==0 && current>500} → creep_debounce++ {>=2} → creep_faulted=TRUE，LED=1
         否则 creep_debounce=0
  → SC_Plausibility_IsFaulted() / IsCreepFaulted()（公开查询）
  → 观测（harness 输出）：results[] 每操作 state 快照
       · faulted / creepFaulted（公开 API）
       · debounce / backupCutoff / startupGrace / creepDebounce（UNIT_TEST getter）
       · ledSys（gioGetBit mock）
       · lookup: expected；implausible: result
```

与既有 ASW E2E 一致，通过测试专用 API 在原生测试框架内执行真实的
`sc_plausibility.c` 生产代码。由于模块内部状态全部为 `static` 文件作用域，
参照 `sc_e2e` / `sc_heartbeat` / `sc_relay` 的既有做法，在
`sc_plausibility.c/.h` 增加 **UNIT_TEST 保护的观测 getter**（仅测试编译，
不影响交付固件）：

- `SC_Plausibility_TestGetDebounce()` / `TestGetBackupCutoffCounter()` /
  `TestGetStartupGrace()` / `TestGetCreepDebounce()` — 观测内部计数；
- `SC_Plausibility_TestLookupExpectedCurrent(torque_pct)` — 直接驱动静态
  `lookup_expected_current`，覆盖 LUT 每个区间/边界插值分支；
- `SC_Plausibility_TestIsImplausible(expected, actual)` — 直接驱动静态
  `is_implausible`，覆盖近零绝对阈值 / 相对阈值+下限 / 全部比较分支。

> **被测代码观测**：生产固件（TMS570）不定义 `UNIT_TEST`，上述 getter 绝不
> 进入交付固件。`SC_CAN_GetMessage`、`SC_Heartbeat_IsFzcBrakeFault`、
> `gioSetBit/GetBit` 均为 harness 内 mock。harness 以生产 TMS570 逻辑编译
> （不定义 `PLATFORM_POSIX` / `PLATFORM_HIL`），`SC_CREEP_DEBOUNCE_CYCLES`
> 取生产 2 周期；宽限/去抖常量经 `Sc_Cfg_Platform.h` 按 include path 选取
> （本 harness 编译配置 = 1500 宽限 / 10 tick 去抖，与 sc_heartbeat harness
> 一致）。

## 被测代码流程图

### SC_Plausibility_Init（L148-L156）

```text
[Init]
  ═══→ [debounce=0 / faulted=F / backup_cutoff=0]
  ═══→ [grace = 1500（编译配置 SC_HB_STARTUP_GRACE_TICKS）]
  ═══→ [creep_debounce=0 / creep_faulted=F]
```

### lookup_expected_current（L76-L110，内部静态）

```text
[lookup(torque_pct)]
  ═══→ {torque == 0?} ─Y→ [return 0]
   ↓ N
  {torque >= 100?} ─Y→ [return 25000]
   ↓ N
  [for i in 1..16)
     {torque <= LUT[i]?} ─N→ continue
      ↓ Y
     [pct_low=LUT[i-1] / pct_high=LUT[i] / cur_low=CUR[i-1] / cur_high=CUR[i]]
     {pct_range == 0?} ─Y→ [return cur_low]（防御，LUT 严格递增不可达）
      ↓ N
     [frac = torque - pct_low]
     [interp = cur_low + ((cur_high-cur_low) * frac) / pct_range]
     [return interp]
  [return 25000]（不可达兜底）
```

### is_implausible（L116-L142，内部静态）

```text
[is_implausible(expected, actual)]
  ═══→ {actual > expected?} ─Y→ [diff = actual - expected]
                            └─N→ [diff = expected - actual]
  {expected < 100?} ─Y→ [return diff > 2000]（绝对阈值）
   ↓ N
  [threshold = expected * 20 / 100]
  {threshold < 2000?} ─Y→ [threshold = 2000]（绝对下限）
   ↓
  [return diff > threshold]
```

### SC_Plausibility_Check（L158-L223）

```text
[Check]
  ═══→ {faulted?} ─Y→ [return]（锁存守卫）
   ↓ N
  {grace > 0?} ─Y→ [grace--] → [return]
   ↓ N
  [veh_ok = GetMessage(VEHICLE_STATE)]（SC_MB_IDX_VEHICLE_STATE）
  [cur_ok = GetMessage(MOTOR_CURRENT)]（SC_MB_IDX_MOTOR_CURRENT）
  {veh_ok && cur_ok?} ─Y→ [torque=veh[4]；actual=cur[2..3] LE]
      ↓                         ↓ [expected=lookup(torque)]
      ↓                    {implausible?} ─Y→ [debounce++]
      ↓                        └─N→ [debounce=0]
      ↓                    {debounce>=10?} ─Y→ [faulted=TRUE；LED=1]
      ↓
  {cur_ok && FzcBrakeFault?} ─Y→ {actual>1000?} ─Y→ [backup_cutoff++]
      │                              └─N→ [backup_cutoff=0]    ↓ {>=10?} ─Y→ [faulted=TRUE；LED=1]
      └─N→ [backup_cutoff=0]                                    └─N→（继续）
```

### SC_CreepGuard_Check（L230-L271）

```text
[CreepGuard]
  ═══→ {creep_faulted?} ─Y→ [return]（非清除锁存）
   ↓ N
  {grace > 0?} ─Y→ [return]（共享宽限）
   ↓ N
  [veh_ok / cur_ok]
  {!veh_ok || !cur_ok?} ─Y→ [return]
   ↓ N
  [torque=veh[4]；current=cur[2..3] LE]
  {torque==0 && current>500?} ─Y→ [creep_debounce++]
      │                              ↓ {>=2?} ─Y→ [creep_faulted=TRUE；LED=1]
      └─N→ [creep_debounce=0]        └─N→（继续）
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `op` | 本阶段执行动作 | `init` / `check` / `creep` / `drainGrace` / `lookup` / `implausible` | When — 执行控制 |
| `skipInit` | 首阶段是否跳过自动 Init | `false`、`true` | When — 执行控制 |
| `torque` | Vehicle_State byte4 扭矩 % / lookup 输入 | `0`（零边界）、`1`/`7`/`50`/`99`（插值区间）、`100`（上限边界）、`255`（钳位） | When — 载荷 |
| `current` | Motor_Current 电流 mA | `0`、`500`（creep 边界）、`501`、`1000`（backup 边界）、`1001`、`12000`（合理）、`1500`（backup 触发）、`2000`（abs 阈值边界） | When — 载荷 |
| `vehValid` | Vehicle_State 邮箱有效 | `0`、`1` | When — 载荷 |
| `curValid` | Motor_Current 邮箱有效 | `0`、`1` | When — 载荷 |
| `brakeFault` | FZC 制动故障（backup cutoff） | `0`、`1` | When — 载荷 |
| `repeats` | check/creep 调用次数 | `1`、`2`、`3`、`5`、`9`、`10`、`11`（去抖/backup/creep 阈值） | When — 执行控制 |
| `ticks` | drainGrace 调用次数 | `1499`、`1500`（宽限边界） | When — 执行控制 |
| `expected` | implausible 期望电流 | `0`、`100`（近零边界）、`25000` | When — 载荷 |
| `actual` | implausible 实测电流 | `0`、`2000`/`2001`（abs 边界）、`2100`/`2101`（floor 边界）、`19999`/`20000`（rel 边界）、`25000`、`30000` | When — 载荷 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `results[i].state.faulted` | 公开 `SC_Plausibility_IsFaulted()` | 0/1 |
| `results[i].state.creepFaulted` | 公开 `SC_Plausibility_IsCreepFaulted()` | 0/1 |
| `results[i].state.debounce` | 合理性去抖计数 | 0..10 |
| `results[i].state.backupCutoff` | 备份切断计数 | 0..10 |
| `results[i].state.startupGrace` | 启动宽限计数 | 1500→0 |
| `results[i].state.creepDebounce` | 蠕动去抖计数 | 0..2 |
| `results[i].state.ledSys` | 系统故障 LED（gioGetBit mock） | 0/1 |
| `results[i].expected` | `lookup_expected_current` 结果 | 0..25000 |
| `results[i].result` | `is_implausible` 结果 | 0/1 |

## 测试用例

> 用例按「最短路径优先」逐步导出；名称突出区别于前一用例的因子取值。
> 主检查/蠕动用例统一先 `drainGrace ticks=1500` 消费启动宽限（宽限本身
> 也是被测功能，见宽限规则）。

### 规则: 初始化 — SC_Plausibility_Init

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `init_resets_all_state` | P0: init | faulted=0；creepFaulted=0；debounce=0；backupCutoff=0；startupGrace=1500；creepDebounce=0；ledSys=0 |

### 规则: 扭矩-电流查找表 — SWR-SC-007（lookup）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `lookup_zero_torque` | P0: lookup(torque=0) | expected=0 |
| `lookup_max_torque` | P0: lookup(torque=100) | expected=25000（上限边界） |
| `lookup_overflow_clamped` | P0: lookup(torque=255) | expected=25000（>=100 钳位） |
| `lookup_exact_lut_entry` | P0: lookup(torque=7) | expected=1750（LUT 表项精确命中） |
| `lookup_interp_mid` | P0: lookup(torque=50) | expected=12500（47..53 插值） |
| `lookup_interp_low` | P0: lookup(torque=1) | expected=250（0..7 插值首区间） |
| `lookup_interp_high` | P0: lookup(torque=99) | expected=24750（93..100 插值末区间） |

### 规则: 合理性比较 — SWR-SC-008（is_implausible）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `impl_near_zero_above_abs` | P0: implausible(expected=0, actual=2500) | result=1（近零绝对阈值 2000 之上） |
| `impl_near_zero_at_abs_boundary` | P0: implausible(expected=0, actual=2000) | result=0（diff==2000，不 >） |
| `impl_floor_above` | P0: implausible(expected=100, actual=2101) | result=1（threshold=20→floor 2000；diff=2001>2000） |
| `impl_floor_boundary` | P0: implausible(expected=100, actual=2100) | result=0（diff==2000==floor） |
| `impl_rel_above` | P0: implausible(expected=25000, actual=0) | result=1（diff=25000>5000=20%） |
| `impl_rel_boundary` | P0: implausible(expected=25000, actual=20000) | result=0（diff==5000==20%） |
| `impl_rel_just_above` | P0: implausible(expected=25000, actual=19999) | result=1（diff=5001>5000） |
| `impl_actual_gt_expected` | P0: implausible(expected=25000, actual=30000) | result=0（diff 取 actual-expected 侧=5000） |

### 规则: 启动宽限期

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `grace_suppresses_implausible` | P0: init; P1: check(torque=100, current=0, repeats=10) | 宽限期内不判合理性：faulted=0；debounce=0；startupGrace=1490 |
| `grace_drains_to_zero` | P0: init; P1: drainGrace(1500) | startupGrace=0；faulted=0 |

### 规则: 合理性去抖与故障锁存 — SWR-SC-008/009

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `mismatch_under_debounce` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=0, repeats=9) | faulted=0；debounce=9（阈值 10 之下） |
| `fault_declared_at_debounce` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=0, repeats=10) | faulted=1；ledSys=1 |
| `debounce_resets_on_plausible` | P0: init; P1: drainGrace(1500); P2: check(100,0)×3; P3: check(100,25000)×1; P4: check(100,0)×3 | P4 后 debounce=3（复位后重新累计）；faulted=0 |
| `fault_latched` | P0: init; P1: drainGrace(1500); P2: check(100,0)×10; P3: check(100,25000)×10 | P2 faulted=1；P3 保持 faulted=1（锁存） |
| `check_early_return_when_faulted` | P0: init; P1: drainGrace(1500); P2: check(100,0)×10; P3: check(100,0)×10 | P3 debounce 仍 10（faulted 守卫直接返回，不再累计） |
| `reinit_clears_fault` | P0: init; P1: drainGrace(1500); P2: check(100,0)×10; P3: init | P3 faulted=0；startupGrace=1500（Init 复位） |

### 规则: CAN 数据缺失（fail-safe）

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `missing_vehicle_state` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=0, vehValid=0, repeats=10) | faulted=0；debounce=0（veh_ok=FALSE 跳过主检查） |
| `missing_motor_current` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=0, curValid=0, repeats=10) | faulted=0；backupCutoff=0（cur_ok=FALSE 跳过 backup） |
| `missing_both_can` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=0, vehValid=0, curValid=0, repeats=10) | faulted=0；debounce=0；backupCutoff=0 |

### 规则: 备份切断 — SWR-SC-024

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `backup_under_threshold` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1500, brakeFault=1, repeats=9) | faulted=0；backupCutoff=9 |
| `backup_declares_fault` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1500, brakeFault=1, repeats=10) | faulted=1；ledSys=1 |
| `backup_requires_brake_fault` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1500, brakeFault=0, repeats=10) | faulted=0；backupCutoff=0 |
| `backup_current_boundary` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1000, brakeFault=1, repeats=10) | faulted=0；backupCutoff=0（1000 不 > 1000） |
| `backup_current_above_boundary` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1001, brakeFault=1, repeats=10) | faulted=1（1001>1000） |
| `backup_counter_resets_on_low_current` | P0: init; P1: drainGrace(1500); P2: check(0,1500,bf=1)×5; P3: check(0,500,bf=1)×1; P4: check(0,1500,bf=1)×10 | P3 后 backupCutoff=0（复位）；P4 从 0 重新累计 10 → faulted=1 |
| `backup_independent_of_veh` | P0: init; P1: drainGrace(1500); P2: check(torque=100, current=1500, vehValid=0, brakeFault=1, repeats=10) | faulted=1（backup 仅需 cur_ok） |
| `backup_needs_cur_mailbox` | P0: init; P1: drainGrace(1500); P2: check(torque=0, current=1500, curValid=0, brakeFault=1, repeats=10) | faulted=0；backupCutoff=0 |

### 规则: 蠕动防护 — SSR-SC-018

| 用例 | 阶段序列 | 关键断言 |
|---|---|---|
| `creep_under_debounce` | P0: init; P1: drainGrace(1500); P2: creep(torque=0, current=600, repeats=1) | creepFaulted=0；creepDebounce=1 |
| `creep_declares_fault` | P0: init; P1: drainGrace(1500); P2: creep(torque=0, current=600, repeats=2) | creepFaulted=1；ledSys=1 |
| `creep_current_boundary` | P0: init; P1: drainGrace(1500); P2: creep(torque=0, current=500, repeats=2) | creepFaulted=0；creepDebounce=0（500 不 > 500） |
| `creep_current_above_boundary` | P0: init; P1: drainGrace(1500); P2: creep(torque=0, current=501, repeats=2) | creepFaulted=1 |
| `creep_requires_zero_torque` | P0: init; P1: drainGrace(1500); P2: creep(torque=1, current=600, repeats=2) | creepFaulted=0；creepDebounce=0（torque!=0 → 清零分支） |
| `creep_missing_can` | P0: init; P1: drainGrace(1500); P2: creep(0,600,vehValid=0)×2; P3: creep(0,600,curValid=0)×2 | creepFaulted=0；creepDebounce=0 |
| `creep_during_grace` | P0: init; P1: creep(torque=0, current=600, repeats=2) | creepFaulted=0（宽限内不检查） |
| `creep_latch_non_clearable` | P0: init; P1: drainGrace(1500); P2: creep(0,600)×2; P3: creep(torque=1, current=0, repeats=5) | P2 creepFaulted=1；P3 保持 1（非清除锁存） |

## 代码路径覆盖

- `SC_Plausibility_Init`：全部 6 个静态字段清零 + 宽限置位路径覆盖。
- `lookup_expected_current`：零 / ≥100 上限 / 区间扫描循环（含 L89 命中与
  跳过两侧，经 torque=1/7/50/99 及精确表项驱动首/中/末区间插值）覆盖；
  `pct_range==0` 防御分支为编译期不可达（LUT 严格递增），兜底
  `return 25000` 不可达（0..255 全输入被前置分支捕获）。
- `is_implausible`：diff 两方向（actual>expected / actual<=expected）、近零
  绝对阈值两分支、rel 阈值两分支、绝对下限 floor 两分支、最终比较两分支
  全覆盖。
- `SC_Plausibility_Check`：锁存守卫、宽限守卫、veh_ok&&cur_ok 全组合
  （TT/FT/TF/FF）、implausible 两分支、去抖阈值两分支、backup 四条件
  （cur_ok×brakeFault 两子条件各自 true/false）、电流边界、backup 阈值
  两分支全覆盖。
- `SC_CreepGuard_Check`：非清除锁存守卫、宽限守卫、CAN 缺失守卫、
  torque==0 && current>500 全组合、creep 去抖阈值两分支全覆盖。
- 公开查询 `IsFaulted` / `IsCreepFaulted`：每阶段快照覆盖。
- UNIT_TEST getter：每阶段快照 + lookup/implausible 钩子全覆盖。

## 覆盖率报告实测

全量运行 `./gradlew cucumber`（2026-08-18）后，`sc_plausibility.c` 的覆盖率
报告为：

| 指标 | 数值 |
|---|---:|
| 行覆盖 | **98.1%（153 / 156）** |
| 分支覆盖 | **96.3%（52 / 54）** |
| 函数覆盖 | **100%（13 / 13）** |

关联测试结果：

| 命令 | 结果 |
|---|---|
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber -Pfile=src/test/resources/features/sc_plausibility.feature` | **43 scenarios / 258 steps passed** |
| `TESTCHARM_DAL_DUMPINPUT=false ./gradlew cucumber` | **725 scenarios / 4379 steps passed** |

函数命中次数（`sc_plausibility.c.func.html`）：

| 函数 | 命中 |
|---|---:|
| `SC_Plausibility_Init` | 176 |
| `SC_Plausibility_Check` | 75402 |
| `SC_Plausibility_IsFaulted` | 325 |
| `SC_CreepGuard_Check` | 44 |
| `SC_Plausibility_IsCreepFaulted` | 325 |
| `lookup_expected_current`（内部静态） | 257 |
| `is_implausible`（内部静态） | 258 |
| `SC_Plausibility_TestGetDebounce` | 325 |
| `SC_Plausibility_TestGetBackupCutoffCounter` | 325 |
| `SC_Plausibility_TestGetStartupGrace` | 325 |
| `SC_Plausibility_TestGetCreepDebounce` | 325 |
| `SC_Plausibility_TestLookupExpectedCurrent` | 15 |
| `SC_Plausibility_TestIsImplausible` | 16 |

### 逐行代码覆盖映射

> 下表直接依据
> `e2e-tests/build/coverage/firmware/ecu/sc/src/sc_plausibility.c.gcov.html`
> 的逐行 hit count 回填。156 个可执行行中 153 行被命中，仅 3 行（L98/L99/
> L109）未命中——均为 LUT 查找函数内**编译期不可达**的防御代码（详见
> 「无法覆盖的代码说明」）。

#### lookup_expected_current（L76-L110，内部静态）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L77-L78 | 函数入口 + 声明 | 257 | 全部 `check` 用例（主检查每帧经 L191 调用）+ 全部 `lookup` 用例 |
| L80 | `if (torque_pct == 0u)` | 257 | true 侧 `lookup_zero_torque`（132 次）；false 侧其余 |
| L81 | `return 0u;` | 132 | `lookup_zero_torque` / 主检查零扭矩场景 |
| L83 | `if (torque_pct >= 100u)` | 125 | true 侧 `lookup_max_torque`/`lookup_overflow_clamped`（116 次）；false 侧 1..99 |
| L84 | `return 25000u;` | 116 | `lookup_max_torque`（100）/`lookup_overflow_clamped`（255） |
| L88 | `for (i = 1u; i < SC_TORQUE_LUT_SIZE; i++)` | 58 | true 侧扭矩 1..99；false 侧循环耗尽退出 |
| L89 | `if (torque_pct <= torque_pct_lut[i])` | 58 | true 侧命中区间（9 次插值）；false 侧 47/49 次扫描跳过 |
| L91-L95 | 取 LUT 邻项（pct_low/high、cur_low/high、pct_range） | 9 | `lookup_interp_low`/`lookup_exact_lut_entry`/`lookup_interp_mid`/`lookup_interp_high` 及主检查插值 |
| L97 | `if (pct_range == 0u)` | 9 | **恒 false（LUT 严格递增，pct_range≥6）——防御分支不可达** |
| L98-L99 | `return cur_low;` / `}` | **0** | 不可达（见「无法覆盖的代码说明」①） |
| L101-L104 | `frac` 与线性插值计算 | 9 | 全部插值用例（首/中/末区间，含精确表项 7→1750 亦走插值公式） |
| L105 | `return (uint16)interp;` | 9 | 全部插值用例（1→250、7→1750、50→12500、99→24750 等） |
| L109 | `return 25000u;  /* Should not reach here */` | **0** | 不可达（见「无法覆盖的代码说明」②） |
| L110 | `}` | 9 | 函数收尾 |

#### is_implausible（L116-L142，内部静态）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L117-L119 | 函数入口 + 声明 | 258 | 全部 `implausible` 用例 + 全部主检查比较 |
| L122 | `if (actual_ma > expected_ma)` | 258 | true 侧 `impl_actual_gt_expected`/`impl_near_zero_above_abs`（140 次）；false 侧 `impl_rel_*`（118 次） |
| L123 | `diff = actual - expected` | 140 | actual>expected 用例（近零/正差） |
| L125 | `diff = expected - actual` | 118 | actual≤expected 用例（负差/零差） |
| L129 | `if (expected_ma < 100u)` | 258 | true 侧 `impl_near_zero_above_abs`/`impl_near_zero_at_abs_boundary`（134 次）；false 侧 `impl_floor_*`/`impl_rel_*`（124 次） |
| L130 | `return diff > ABS_THRESHOLD` | 134 | true 侧 `impl_near_zero_above_abs`（diff 2500>2000）；false 侧 `impl_near_zero_at_abs_boundary`（diff==2000 不 >） |
| L134 | `threshold = expected*20/100` | 124 | `impl_floor_*`/`impl_rel_*`（相对阈值计算） |
| L137 | `if (threshold < ABS_THRESHOLD)` | 124 | true 侧 `impl_floor_above`/`impl_floor_boundary`（expected=100，4 次）；false 侧 `impl_rel_*`（expected=25000，120 次） |
| L138 | `threshold = ABS_THRESHOLD` | 4 | `impl_floor_above`/`impl_floor_boundary`（floor 下限生效） |
| L141 | `return diff > threshold` | 124 | true 侧 `impl_floor_above`/`impl_rel_above`/`impl_rel_just_above`；false 侧 `impl_floor_boundary`/`impl_rel_boundary`/`impl_actual_gt_expected` |

#### SC_Plausibility_Init（L148-L156）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L149-L155 | 6 个静态字段清零 + 宽限置位 | 176 | 全部用例（harness 启动自动 init + 显式 `init` 阶段） |
| L153 | `plaus_startup_grace = SC_HB_STARTUP_GRACE_TICKS;` | 176 | 全部用例（生产 1500） |

#### SC_Plausibility_Check（L158-L223）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L159-L167 | 函数入口 + 声明 | 75402 | 全部 `check`/`drainGrace` 用例（含宽限消费） |
| L170 | `if (plaus_faulted == TRUE)` | 75402 | true 侧 `fault_latched`/`check_early_return_when_faulted`/`creep_latch_non_clearable` 后续阶段（40 次）；false 侧其余 |
| L171-L172 | `return;`（锁存守卫） | 40 | 锁存后再次 Check 的场景 |
| L175 | `if (plaus_startup_grace > 0u)` | 75362 | true 侧宽限内（75020 次，含 drainGrace）；false 侧宽限耗尽后（342 次） |
| L176-L178 | `grace--` + return | 75020 | `grace_suppresses_implausible`/`grace_drains_to_zero` 及全部 drainGrace 用例 |
| L181 | `veh_ok = GetMessage(VEHICLE_STATE)` | 342 | 宽限耗尽后的全部 `check` |
| L184 | `cur_ok = GetMessage(MOTOR_CURRENT)` | 342 | 同上 |
| L186 | `if (veh_ok && cur_ok)` | 342 | 四组合：TT `fault_declared_at_debounce` 等（242 次）；FT `missing_vehicle_state`/`backup_independent_of_veh`；TF `missing_motor_current`/`backup_needs_cur_mailbox`；FF `missing_both_can`（共 100 次 false 侧） |
| L187 | `torque_pct = veh_data[4]` | 242 | 双邮箱有效的主检查用例 |
| L188-L189 | `actual = cur[3]<<8\|cur[2]`（LE） | 242 | 同上（1500mA = 0x05DC → byte2=0xDC byte3=0x05） |
| L191 | `expected = lookup_expected_current(torque)` | 242 | 全部双邮箱有效用例 |
| L193 | `if (is_implausible(...) == TRUE)` | 242 | true 侧 `fault_declared_at_debounce`/`backup_current_above_boundary`（110 次）；false 侧 `backup_cutoff_*` 等合理组合（132 次） |
| L194 | `plaus_debounce++;` | 110 | 不一致组合（去抖累计） |
| L196 | `plaus_debounce = 0u;` | 132 | 合理组合（去抖复位） |
| L199 | `if (plaus_debounce >= SC_PLAUS_DEBOUNCE_TICKS)` | 242 | true 侧第 10 tick（`fault_declared_at_debounce`，8 次）；false 侧 1..9 tick（`mismatch_under_debounce`/`debounce_resets_on_plausible`） |
| L200-L201 | `faulted=TRUE` + `gioSetBit(LED)` | 8 | `fault_declared_at_debounce`（主检查置位） |
| L207 | `if (cur_ok && FzcBrakeFault)` | 342 | 四组合：TT 备份用例（130 次）；TF `missing_motor_current`/`backup_needs_cur_mailbox`；FT `no_backup_without_brake_fault`；FF `missing_both_can`/`missing_vehicle_state`（212 次 false 侧） |
| L208-L209 | `actual = cur[2..3]` | 130 | 备份切断用例（brakeFault=1 且 cur 有效） |
| L211 | `if (actual > SC_BACKUP_CUTOFF_CURRENT_MA)` | 130 | true 侧 1500/1001mA（108 次）；false 侧 1000mA/500mA 边界（22 次） |
| L212 | `backup_cutoff_counter++;` | 108 | `backup_under_threshold`/`backup_declares_fault`/`backup_counter_resets_on_low_current` |
| L213 | `if (counter >= SC_BACKUP_CUTOFF_TICKS)` | 108 | true 侧第 10 tick（8 次）；false 侧 1..9 |
| L214-L215 | `faulted=TRUE` + `gioSetBit(LED)` | 8 | `backup_declares_fault`/`backup_current_above_boundary`/`backup_independent_of_veh`（备份置位） |
| L218 | `backup_cutoff_counter = 0u;`（电流回落） | 22 | `backup_current_boundary`（1000mA）/`backup_counter_resets_on_low_current`（500mA） |
| L220-L221 | `backup_cutoff_counter = 0u;`（无制动故障/cur 无效） | 212 | `no_backup_without_brake_fault`/`missing_*` 等 |

#### SC_Plausibility_IsFaulted / IsCreepFaulted（L225-L228 / L273-L276）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L226-L228 | `return plaus_faulted;` | 325 | 全部用例（harness 每阶段快照） |
| L274-L276 | `return creep_faulted;` | 325 | 全部用例（每阶段快照） |

#### SC_CreepGuard_Check（L230-L271）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L231-L238 | 函数入口 + 声明 | 44 | 全部 `creep` 用例 |
| L241 | `if (creep_faulted == TRUE)` | 44 | true 侧 `creep_latch_non_clearable` 后续阶段（10 次）；false 侧其余 |
| L242-L243 | `return;`（非清除锁存守卫） | 10 | `creep_latch_non_clearable` P3 |
| L246 | `if (plaus_startup_grace > 0u)` | 34 | true 侧 `creep_during_grace`（4 次）；false 侧宽限耗尽后（30 次） |
| L247-L248 | `return;`（共享宽限） | 4 | `creep_during_grace` |
| L250-L251 | 读两个邮箱 | 30 | 宽限耗尽后的 `creep` 用例 |
| L253 | `if (!veh_ok \|\| !cur_ok)` | 30 | true 侧 `creep_missing_can`（8 次）；false 侧双邮箱有效（22 次） |
| L255-L256 | `return;`（CAN 缺失） | 8 | `creep_missing_can` |
| L258 | `torque_pct = veh_data[4]` | 22 | 双邮箱有效用例 |
| L259 | `motor_current = cur[2..3]` | 22 | 同上 |
| L262 | `if (torque==0 && current>500)` | 22 | 全组合：TT `creep_declares_fault`/`creep_current_above_boundary`（14 次）；TF `creep_current_boundary`（500mA）；FT `creep_requires_zero_torque`（torque=1）；FF 共 8 次清零 |
| L263 | `creep_debounce++;` | 14 | TT 组合（600/501mA 零扭矩） |
| L264 | `if (creep_debounce >= SC_CREEP_DEBOUNCE_CYCLES)` | 14 | true 侧第 2 周期（6 次）；false 侧第 1 周期（8 次） |
| L265-L266 | `creep_faulted=TRUE` + `gioSetBit(LED)` | 6 | `creep_declares_fault`/`creep_current_above_boundary` |
| L269 | `creep_debounce = 0u;` | 8 | TF/FT/FF 组合（500mA 边界 / 非零扭矩） |

#### UNIT_TEST getter / 钩子（L285-L313，仅测试编译，生产固件不含）

| 行号 | 代码 | 实测命中 | 覆盖场景 |
|---|---|---|---:|
| L286-L288 | `TestGetDebounce` | 325 | 全部用例（每阶段快照） |
| L291-L293 | `TestGetBackupCutoffCounter` | 325 | 全部用例 |
| L296-L298 | `TestGetStartupGrace` | 325 | 全部用例 |
| L301-L303 | `TestGetCreepDebounce` | 325 | 全部用例 |
| L306-L308 | `TestLookupExpectedCurrent` | 15 | 全部 7 个 `lookup` 用例 |
| L311-L313 | `TestIsImplausible` | 16 | 全部 8 个 `implausible` 用例 |

### 分支覆盖分析

- `lookup_expected_current`：L80（torque==0）、L83（>=100）两分支两侧覆盖；
  L89 区间命中两侧覆盖；L88 循环进入/退出两侧覆盖。仅 L97 `pct_range==0`
  true 侧（1 分支）+ L109 兜底 return 路径（1 分支）**未命中**——均为
  编译期不可达防御分支（见下）。
- `is_implausible`：L122 diff 方向、L129 近零分支、L130 abs 比较、L137
  floor、L141 最终比较全部两侧覆盖（0 豁免）。
- `SC_Plausibility_Check`：L170 锁存守卫、L175 宽限守卫、L186 双邮箱四
  组合、L193 合理性两分支、L199 去抖阈值、L207 备份四组合、L211 电流
  边界、L213 备份阈值全部两侧覆盖。
- `SC_CreepGuard_Check`：L241 锁存守卫、L246 宽限守卫、L253 CAN 缺失、
  L262 四组合、L264 去抖阈值全部两侧覆盖。
- 全模块 54 分支中 52 个两侧命中，**仅 2 个不可达防御分支豁免**。

## 无法覆盖的代码说明

> **编译期不可达**（不计入可覆盖范围，见上表逐行数据）：
>
> ① `lookup_expected_current` L97-99 `if (pct_range == 0u) return cur_low;`
>    — `pct_range = pct_high - pct_low`，两 LUT 均为**静态只读且严格递增**
>    （0,7,13,…,100，相邻差≥6），运行时恒 >0。true 侧（L98 return + L99
>    `}`）经 `TestLookupExpectedCurrent` 钩子也无法构造输入触发，属
>    编译期不可达防御分支。与 `Swc_RzcScheduler` 只读表防御分支同理，
>    豁免。
>
> ② `lookup_expected_current` L109 `return 25000u;  /* Should not reach here */`
>    — 所有 uint8 输入（0..255）均被前置分支捕获：`torque==0`（L80）→ 0；
>    `torque>=100`（L83，含 101..255）→ 25000；`torque`∈[1,99] 在 L89 循环
>    内必然命中某 LUT 区间并返回插值（末项 LUT[15]=100 兜底，循环不会
>    耗尽退出）。该行对所有输入均不可达，属兜底防御代码，豁免。
>
> 对应 2 个未命中分支（`pct_range==0` true 侧 + L109 兜底路径）与 3 行
> （L98/L99/L109）共同构成本模块全部覆盖缺口；除上述编译期不可达代码外，
> **不存在**经公开 API 或 UNIT_TEST 钩子可构造输入而未覆盖的分支。
