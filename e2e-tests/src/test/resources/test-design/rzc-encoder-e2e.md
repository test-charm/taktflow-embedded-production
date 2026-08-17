# RZC 编码器 (Swc_Encoder) E2E 测试设计

## 被测功能

**RZC ASW 编码器 SWC — 10ms 周期读取正交编码器计数与方向，按 `delta*6000/PPR`
计算 RPM，检测 `PWM>10% && delta==0` 持续 50 周期卡滞，检测命令方向与编码器方向
不一致且有位移持续 5 周期的方向合理性故障，并在真实正反转切换后分别提供 200ms
卡滞宽限期与 100ms 方向宽限期。**

覆盖链路：

```text
IoHwAb_ReadEncoderCount / IoHwAb_ReadEncoderDirection（测试 API 注入）
  → Swc_Encoder_Init
    → 状态清零 / Fault 复位 / 宽限计数归零
  → Swc_Encoder_MainFunction（10ms 周期）
    → 未初始化守卫
    → 读取 count / direction
    → delta 正常路径 or uint32 回绕路径
    → RPM = delta * 6000 / 360
    → Rte_Read(commandedDir, torqueEcho)
    → 方向变化检测：仅 FORWARD↔REVERSE 才设置宽限
    → 卡滞检测：宽限期 / PWM 阈值 / 50 周期确认 / 电机关闭 / DEM STALL
    → 方向合理性：宽限期 / STOP 旁路 / 无位移旁路 / 5 周期确认 / 电机关闭 / DEM DIRECTION
    → Rte_Write(speed, dir, stall)
```

与既有 `rzc_currentmonitor.feature` / `rzc_motor.feature` 一致，本测试通过
`/api/test/asw/rzc/encoder` 调用原生 harness，执行真实 `Swc_Encoder.c`
生产代码。输入的编码器计数、每周期增量、硬件方向、命令方向、扭矩回显，以及输出的
RPM、编码器方向、卡滞标志、DEM 最近状态/计数、DIO 使能脚状态，全部由测试专用 API
观测，**生产代码零改动**。

## 被测代码流程图

```text
                ┌───────────────────────────┐
                │ Swc_Encoder_Init          │
                └─────────────┬─────────────┘
                              │
            状态清零 / fault=0 / grace=0 / prevDir=STOP
                              │
                ┌─────────────▼─────────────┐
                │ MainFunction (10ms)       │
                └─────────────┬─────────────┘
                              │
         Initialized != TRUE? ─┬─ Y → return
                              └─ N
                              │
        读取 count / encoderDir / commandedDir / torqueEcho
                              │
      current_count >= prev? ─┬─ Y → delta = current - prev
                              └─ N → delta = wrap-around delta
                              │
                   rpm = delta * 6000 / 360
                              │
      commandedDir != prevDir? ─┬─ N → keep grace
                                └─ Y
                                     prev!=STOP && cmd!=STOP ?
                                       ├─ Y → stallGrace=20, dirGrace=10
                                       └─ N → only update prevDir
                              │
             stallFault == 0? ─┬─ N → skip stall logic
                               └─ Y
                                    stallGrace > 0 ?
                                      ├─ Y → decrement grace
                                      └─ N
                                           torqueEcho > 10 && delta == 0 ?
                                             ├─ Y → stallCounter++ → >=50 ?
                                             │         ├─ Y → stallFault=1, DisableMotor, DEM STALL
                                             │         └─ N → wait
                                             ├─ N, delta > 0 → stallCounter=0
                                             └─ N, delta == 0 → intentional stop
                              │
               dirFault == 0? ─┬─ N → skip dir logic
                                └─ Y
                                     dirGrace > 0 ?
                                       ├─ Y → decrement grace
                                       └─ N
                                            cmd!=enc && cmd!=STOP && delta>0 ?
                                              ├─ Y → mismatchCounter++ → >=5 ?
                                              │         ├─ Y → dirFault=1, DisableMotor, DEM DIRECTION
                                              │         └─ N → wait
                                              └─ N → mismatchCounter=0
                              │
                  Rte_Write(speed, dir, stall)
```

## 输入和输出

### 输入因子

| 因子 | 含义 | 取值（等价类/边界） | 分类 |
|---|---|---|---|
| `skipInit` | 是否跳过 `Swc_Encoder_Init()` | `false`、`true` | When — 执行控制 |
| `count` | 进入 phase 前的绝对编码器计数 | `0`、`100`、`200`、`4294967280`、`16` | When — 数据注入 |
| `deltaPerCycle` | 每个 10ms 周期前增加的计数 | `0`、`1`、`5`、`10`、`36` | When — 数据注入 |
| `encoderDir` | 硬件反馈方向 | `FORWARD(0)`、`REVERSE(1)`、`STOP(2)` | When — 数据注入 |
| `commandedDir` | 电机命令方向 | `FORWARD(0)`、`REVERSE(1)`、`STOP(2)` | When — 数据注入 |
| `torqueEcho` | 电机扭矩回显（%） | `0`、`10`、`50` | When — 数据注入 |
| `cycles` | 当前 phase 连续执行的 MainFunction 次数 | `1`、`4`、`5`、`6`、`10`、`20`、`49`、`50`、`51` | When — 执行控制 |

### 输出因子

| 因子 | 含义 | 关注期望值 |
|---|---|---|
| `speedRpm` | RTE `RZC_SIG_ENCODER_SPEED` | `0`、`16`、`83`、`166`、`533`、`600` |
| `encoderDir` | RTE `RZC_SIG_ENCODER_DIR` | `0`/`1` |
| `encoderStall` | RTE `RZC_SIG_ENCODER_STALL` | `0`/`1` |
| `dioCh5`/`dioCh6` | 电机使能 GPIO 电平 | `0` |
| `dioWrites` | `Dio_WriteChannel` 总调用次数 | `0` 或 `2` |
| `demStall` / `demStallCount` | `RZC_DTC_STALL` 最近状态 / 上报次数 | `-1`/`1`、`0..1` |
| `demDirection` / `demDirectionCount` | `RZC_DTC_DIRECTION` 最近状态 / 上报次数 | `-1`/`1`、`0..1` |

## 测试用例

### 规则: 初始化守卫与速度计算

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `uninitialized_guard` | `[skipInit=true,count=123,dir=FWD,cmd=FWD,torque=50,cycles=1]` | `speedRpm=0`, `dioWrites=0`, `dem*= -1` |
| `rpm_36_counts_forward` | `[count=0] → [delta=36,dir=FWD,cmd=FWD,torque=50]` | `speedRpm=600`, `encoderDir=0` |
| `rpm_single_count_boundary` | `[count=0] → [delta=1,dir=FWD,cmd=FWD,torque=50]` | `speedRpm=16` |
| `rpm_wraparound_uint32` | `[count=4294967280] → [count=16]` | `speedRpm=533` |
| `reverse_direction_output` | `[count=0,dir=REV,cmd=REV] → [delta=10,dir=REV,cmd=REV]` | `speedRpm=166`, `encoderDir=1` |

### 规则: 卡滞检测

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `stall_pwm_threshold_10_not_triggered` | `[count=100,torque=10] → [delta=0,torque=10,cycles=50]` | `encoderStall=0`, `demStallCount=0` |
| `stall_counter_resets_on_movement` | `[count=100,torque=50] → [delta=0,torque=50,cycles=49] → [delta=5,torque=50]` | `encoderStall=0`, `speedRpm=83` |
| `stall_trips_at_50_and_latches` | `[count=100,torque=50] → [delta=0,torque=50,cycles=51]` | `encoderStall=1`, `dioWrites=2`, `demStallCount=1` |
| `stall_grace_after_true_reversal` | `[count=0,delta=0,dir=FWD,cmd=FWD] → [delta=10,dir=FWD,cmd=FWD] → [delta=0,dir=REV,cmd=REV,cycles=20]` | `encoderStall=0` |

### 规则: 方向合理性

| 用例 | phase 序列 | 期望输出 |
|---|---|---|
| `stop_command_bypasses_direction_fault` | `[count=0,dir=FWD,cmd=FWD] → [delta=10,dir=FWD,cmd=FWD] → [delta=10,dir=FWD,cmd=STOP,cycles=6]` | `demDirectionCount=0` |
| `mismatch_without_movement_does_not_count` | `[count=200,dir=REV,cmd=FWD,torque=0] → [delta=0,dir=REV,cmd=FWD,cycles=5]` | `speedRpm=0`, `demDirectionCount=0` |
| `dir_mismatch_four_cycles_no_fault` | `[count=0,dir=REV,cmd=FWD,torque=50] → [delta=10,dir=REV,cmd=FWD,cycles=4]` | `demDirectionCount=0` |
| `dir_mismatch_five_cycles_trips_and_latches` | `[count=0,dir=REV,cmd=FWD,torque=50] → [delta=10,dir=REV,cmd=FWD,cycles=6]` | `dioWrites=2`, `demDirectionCount=1` |
| `dir_grace_after_true_reversal` | `[count=0,dir=FWD,cmd=FWD] → [delta=10,dir=FWD,cmd=FWD] → [delta=10,dir=FWD,cmd=REV,cycles=10]` | `demDirectionCount=0` |

## 覆盖目标与充分性判断

1. **所有输入取值均至少出现一次**：未初始化守卫、RPM 边界、uint32 回绕、
   PWM 阈值、卡滞/方向故障阈值、STOP 旁路、真实反转宽限都已覆盖。
2. **所有条件分支的判断点均有双侧用例**：
   - `Enc_Initialized != TRUE`
   - `current_count >= Enc_PrevCount`
   - `commandedDir != Enc_PrevDirection`
   - `Enc_PrevDirection != STOP && commandedDir != STOP`
   - `Enc_StallFault == 0`
   - `Enc_StallGraceCounter > 0`
   - `torqueEcho > 10 && delta == 0`
   - `Enc_StallCounter >= 50`
   - `delta > 0`（卡滞计数复位分支）
   - `Enc_DirFault == 0`
   - `Enc_DirGraceCounter > 0`
   - `commandedDir != encoderDir && commandedDir != STOP && delta > 0`
   - `Enc_DirMismatchCounter >= 5`
3. **流程图中所有公开 API 路径均被至少一个用例命中**。

## 覆盖率报告实测（`./gradlew cucumber` 后 `e2e-tests/build/coverage/`）

`Swc_Encoder.c.gcov.html` 实测（2026-08-17，全量套件 **496 场景 / 3001 步全部通过**）：

| 指标 | 数值 |
|---|---:|
| **行覆盖** | **100.0%**（96 / 96 行） |
| **分支覆盖** | **100.0%**（34 / 34 分支） |
| **函数覆盖** | **100.0%**（3 / 3 函数） |

覆盖到的函数（实测命中次数）：
`Enc_DisableMotor`（4）、`Swc_Encoder_Init`（27）、
`Swc_Encoder_MainFunction`（448）。

> 命中次数来自整套 `./gradlew cucumber` 执行后的覆盖 HTML；数值会随套件规模变化，
> 但“哪些行由哪些场景覆盖”这一映射关系保持不变。`Swc_Encoder.c` 仅由
> `rzc_encoder.feature` 驱动，因此下述映射与新增 E2E 场景一一对应。

### 行覆盖分析（100.0%，96/96）

所有可执行行均被端到端场景覆盖，**无无法覆盖的代码**。

#### 宏与内部函数（L38-67）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L38, L41 | `ENC_STALL_GRACE_CYCLES` / `ENC_DIR_GRACE_CYCLES` | `stall_grace_after_true_reversal`、`dir_grace_after_true_reversal` 在真实正反转时设置宽限计数 | 4 |
| L64-L67 | `Enc_DisableMotor()`，写低 `R_EN/L_EN` | `stall_trips_at_50_and_latches`、`dir_mismatch_five_cycles_trips_and_latches` | 4 |

#### Swc_Encoder_Init（L74-86）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L74-L86 | 状态清零、故障位清零、`PrevDirection=STOP`、宽限计数归零、`Initialized=TRUE` | 除 `uninitialized_guard` 外的全部 13 个场景 | 27 |

#### Swc_Encoder_MainFunction（L93-208）

| 行号 | 代码 | 覆盖场景 | 实测命中 |
|---|---|---|---|
| L93-L104 | 局部变量、未初始化守卫及早返回 | 全部场景进入；`uninitialized_guard` 命中 L102-L104 的 true 侧，其他 13 个场景命中 false 侧继续执行 | 448 / 2 |
| L107-L120 | 读取编码器 count/dir；正常 delta 计算与 `uint32` 回绕分支；更新 `Enc_PrevCount` | 全部已初始化场景命中正常路径；`rpm_wraparound_uint32` 命中 L116-L119 回绕路径 | 446 / 2 |
| L126-L135 | RPM 计算、写入 `Enc_SpeedRpm` / `Enc_Direction`、读取 `commandedDir` / `torqueEcho` | 全部已初始化场景；`rpm_36_counts_forward`、`rpm_single_count_boundary`、`reverse_direction_output` 分别覆盖典型/边界 RPM 与反向方向 | 446 |
| L140-L148 | 命令方向变化检测；非 STOP↔非 STOP 时设置宽限，否则只更新 `Enc_PrevDirection` | 首周期 `STOP→FORWARD/REVERSE` 覆盖外层 true + 内层 false；`stall_grace_after_true_reversal`、`dir_grace_after_true_reversal` 覆盖 L144-L145；`stop_command_bypasses_direction_fault` 覆盖 `FORWARD→STOP` 时 L142 false 侧；稳定命令阶段覆盖外层 false 侧 | 446 / 33 / 4 |
| L151-L176 | 卡滞故障锁存守卫、卡滞宽限、`torqueEcho>10 && delta==0` 判断、49→50 周期确认、`delta>0` 复位计数、无 PWM 的有意停机分支 | `stall_grace_after_true_reversal` 覆盖 L152-L155；`stall_pwm_threshold_10_not_triggered` 覆盖“扭矩阈值 false + intentional stop”；`stall_counter_resets_on_movement` 覆盖 L159-L170 的“未到 50 次 + 运动复位”；`stall_trips_at_50_and_latches` 覆盖 L161-L167 的 trip 与下一周期 L151 false 侧 | 446 / 60 / 215 / 2 |
| L179-L202 | 方向故障锁存守卫、方向宽限、`cmd!=enc && cmd!=STOP && delta>0` 三段判断、4→5 周期确认、else 复位计数 | `dir_grace_after_true_reversal` 覆盖 L180-L182；`rpm_36_counts_forward`、`reverse_direction_output` 覆盖 `cmd==enc` false 侧；`stop_command_bypasses_direction_fault` 覆盖 `cmd==STOP` false 侧；`mismatch_without_movement_does_not_count` 覆盖 `delta==0` false 侧；`dir_mismatch_four_cycles_no_fault` 覆盖 L188-L190 未到阈值；`dir_mismatch_five_cycles_trips_and_latches` 覆盖 L192-L196 trip 与下一周期 L179 false 侧 | 446 / 40 / 20 / 2 |
| L205-L207 | `Rte_Write(speed, dir, stall)` | 全部已初始化场景 | 446 |

### 分支覆盖分析（100.0%，34/34）

所有分支均被双侧覆盖，关键短路条件的覆盖如下：

| 条件 | true 侧 | false 侧 |
|---|---|---|
| `current_count >= Enc_PrevCount` | 全部常规速度/卡滞/方向场景 | `rpm_wraparound_uint32` |
| `Enc_PrevDirection != STOP && commandedDir != STOP` | `stall_grace_after_true_reversal`、`dir_grace_after_true_reversal` | 首周期 `STOP→FORWARD/REVERSE`、`stop_command_bypasses_direction_fault` |
| `torqueEcho > 10 && delta == 0` | `stall_counter_resets_on_movement`、`stall_trips_at_50_and_latches` | `stall_pwm_threshold_10_not_triggered`（扭矩阈值 false）、`rpm_36_counts_forward` / `stall_counter_resets_on_movement`（`delta>0` false 侧） |
| `commandedDir != encoderDir && commandedDir != STOP && delta > 0` | `dir_mismatch_four_cycles_no_fault`、`dir_mismatch_five_cycles_trips_and_latches` | `rpm_36_counts_forward` / `reverse_direction_output`（方向一致）、`stop_command_bypasses_direction_fault`（命令 STOP）、`mismatch_without_movement_does_not_count`（无位移） |

### 无法覆盖的代码说明

无。`Swc_Encoder.c` 的 96 行可执行代码、34 个分支和 3 个函数均已由端到端测试覆盖。

### 更新记录

| 日期 | 变更 |
|---|---|
| 2026-08-17 | 初版设计文档（输入/输出因子、流程图、14 个 E2E 用例） |
| 2026-08-17 | 新增 `rzc_encoder.feature`（14 场景全部通过）、`rzc_encoder_harness.c`、`/api/test/asw/rzc/encoder` 测试 API；全量 `./gradlew cucumber` 实测 **496 场景 / 3001 步全部通过**，并补充覆盖率（100% 行 / 100% 分支 / 100% 函数）与逐行映射 |
