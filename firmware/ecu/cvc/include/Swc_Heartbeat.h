/**
 * @file    Swc_Heartbeat.h
 * @brief   Heartbeat TX/RX monitoring SWC
 * @date    2026-02-21
 *
 * @safety_req SWR-CVC-021, SWR-CVC-022
 * @traces_to  SSR-CVC-021, SSR-CVC-022, TSR-022, TSR-046
 *
 * @details  Transmits CVC heartbeat on CAN every 50ms with E2E protection
 *           and alive counter. Monitors FZC and RZC heartbeat reception
 *           with timeout detection (3 missed periods) and recovery.
 *           Reports comm status to RTE and DTCs to Dem.
 *           10ms cyclic execution.
 *
 * @standard AUTOSAR, ISO 26262 Part 6 (ASIL D)
 * @copyright Taktflow Systems 2026
 */
#ifndef SWC_HEARTBEAT_H
#define SWC_HEARTBEAT_H

#include "Std_Types.h"

/**
 * @brief  Initialise heartbeat SWC — reset timers, counters, comm status
 * @note   Must be called once before Swc_Heartbeat_MainFunction
 */
void Swc_Heartbeat_Init(void);

/**
 * @brief  Heartbeat cyclic main function — 10ms period
 * @note   Handles TX timing (50ms period) and RX timeout monitoring
 */
void Swc_Heartbeat_MainFunction(void);

/**
 * @brief  Called when a heartbeat CAN message is received from another ECU
 * @param  ecuId  Source ECU identifier (CVC_ECU_ID_FZC or CVC_ECU_ID_RZC)
 */
void Swc_Heartbeat_RxIndication(uint8 ecuId);

/**
 * @brief  Reset comm status to OK — called when post-INIT grace expires
 * @note   Clears stale TIMEOUT status accumulated during boot transient and
 *         forces the E2E state machines to VALID (grace period already
 *         absorbed the transient).
 */
void Swc_Heartbeat_ResetCommStatus(void);

/* ====================================================================
 * Test-only observation API — compiled only when UNIT_TEST is defined.
 * Production firmware builds (STM32/TMS570/POSIX target) do not define
 * UNIT_TEST, so these accessors never reach shipping firmware. They let
 * the native ASW E2E harness verify the SWC's internal heartbeat state
 * (alive counter, RX flags, comm status, E2E SM status).
 * ==================================================================== */
#ifdef UNIT_TEST
uint8   Swc_Heartbeat_GetAliveCounter(void);
boolean Swc_Heartbeat_GetFzcRxFlag(void);
boolean Swc_Heartbeat_GetRzcRxFlag(void);
uint8   Swc_Heartbeat_GetFzcCommStatus(void);
uint8   Swc_Heartbeat_GetRzcCommStatus(void);
uint8   Swc_Heartbeat_GetFzcSmStatus(void);
uint8   Swc_Heartbeat_GetRzcSmStatus(void);
#endif /* UNIT_TEST */

#endif /* SWC_HEARTBEAT_H */
