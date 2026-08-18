/**
 * @file    sc_plausibility.h
 * @brief   Torque-vs-current cross-plausibility check for Safety Controller
 * @date    2026-02-23
 *
 * @details 16-entry lookup table maps torque % to expected motor current.
 *          Linear interpolation between entries. 20% relative / 2000mA
 *          absolute threshold with 50ms (5-tick) debounce.
 *          Includes backup cutoff for FZC brake fault condition.
 *
 * @safety_req SWR-SC-007, SWR-SC-008, SWR-SC-009, SWR-SC-024
 * @traces_to  SSR-SC-007, SSR-SC-008, SSR-SC-009, SSR-SC-018
 * @note    Safety level: ASIL D
 * @standard ISO 26262 Part 6
 * @copyright Taktflow Systems 2026
 */
#ifndef SC_PLAUSIBILITY_H
#define SC_PLAUSIBILITY_H

#include "sc_types.h"

/**
 * @brief  Initialize plausibility checker — reset debounce and latch
 */
void SC_Plausibility_Init(void);

/**
 * @brief  10ms cyclic plausibility check
 *
 * Reads torque from CAN 0x100 byte 4, current from CAN 0x301 bytes 2-3.
 * Compares against lookup table. Debounces mismatch for 50ms.
 * Also checks backup cutoff (FZC brake fault + high current).
 */
void SC_Plausibility_Check(void);

/**
 * @brief  Check if plausibility fault is latched
 * @return TRUE if plausibility fault has been declared
 */
boolean SC_Plausibility_IsFaulted(void);

/**
 * @brief  10ms cyclic standstill torque cross-plausibility check (SSR-SC-018)
 *
 * If torque_pct == 0 (Vehicle_State 0x100 byte 4) and motor current > 500mA
 * (Motor_Current 0x301 bytes 2-3) for 2 consecutive cycles (20ms), latch
 * creep fault. Detects BTS7960 FET short (MB-006).
 *
 * @note  Non-clearable — only power cycle resets the latch.
 */
void SC_CreepGuard_Check(void);

/**
 * @brief  Check if creep guard fault is latched
 * @return TRUE if standstill torque cross-plausibility fault has been declared
 */
boolean SC_Plausibility_IsCreepFaulted(void);

/* ==================================================================
 * UNIT_TEST-only hooks (never compiled into production firmware)
 * ================================================================== */

#ifdef UNIT_TEST
/**
 * @brief  Test hook: read the plausibility debounce counter
 * @return Current plaus_debounce value
 */
uint8 SC_Plausibility_TestGetDebounce(void);

/**
 * @brief  Test hook: read the backup cutoff counter (SWR-SC-024)
 * @return Current backup_cutoff_counter value
 */
uint8 SC_Plausibility_TestGetBackupCutoffCounter(void);

/**
 * @brief  Test hook: read the startup grace counter
 * @return Current plaus_startup_grace value
 */
uint16 SC_Plausibility_TestGetStartupGrace(void);

/**
 * @brief  Test hook: read the creep-guard debounce counter (SSR-SC-018)
 * @return Current creep_debounce value
 */
uint8 SC_Plausibility_TestGetCreepDebounce(void);

/**
 * @brief  Test hook: directly drive the internal torque-to-current lookup
 *         so every LUT bracket / interpolation branch is exercised.
 * @param  torque_pct  Torque percentage (0-255)
 * @return Expected motor current in mA
 */
uint16 SC_Plausibility_TestLookupExpectedCurrent(uint8 torque_pct);

/**
 * @brief  Test hook: directly drive the internal implausibility comparator
 *         so both threshold policies (near-zero absolute / relative+floor)
 *         and every comparison branch are exercised.
 * @param  expected_ma  Expected current from LUT
 * @param  actual_ma    Measured motor current
 * @return TRUE if actual deviates beyond the plausibility threshold
 */
boolean SC_Plausibility_TestIsImplausible(uint16 expected_ma, uint16 actual_ma);
#endif /* UNIT_TEST */

#endif /* SC_PLAUSIBILITY_H */
