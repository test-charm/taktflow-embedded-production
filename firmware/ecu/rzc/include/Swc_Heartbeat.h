/**
 * @file    Swc_Heartbeat.h
 * @brief   RZC heartbeat SWC -- alive counter, fault bitmask, CAN TX
 * @date    2026-02-23
 *
 * @safety_req SWR-RZC-021, SWR-RZC-022
 * @traces_to  SSR-RZC-021, SSR-RZC-022, TSR-022
 *
 * @standard AUTOSAR SWC pattern, ISO 26262 Part 6
 * @copyright Taktflow Systems 2026
 */
#ifndef SWC_HEARTBEAT_H
#define SWC_HEARTBEAT_H

#include "Std_Types.h"

/* ==================================================================
 * API Functions
 * ================================================================== */

void Swc_Heartbeat_Init(void);
void Swc_Heartbeat_MainFunction(void);

/* ==================================================================
 * Test-only observation API — compiled only when UNIT_TEST is defined.
 * Production firmware builds do not define UNIT_TEST, so these accessors
 * never reach shipping firmware. The native ASW E2E harness uses them to
 * verify heartbeat internal state (alive counter, cycle counter,
 * initialized flag) that is otherwise not externally observable.
 * ================================================================== */
#ifdef UNIT_TEST
uint8 Swc_Heartbeat_GetAliveCounter(void);
uint8 Swc_Heartbeat_GetCycleCounter(void);
uint8 Swc_Heartbeat_GetInitialized(void);
#endif /* UNIT_TEST */

#endif /* SWC_HEARTBEAT_H */
