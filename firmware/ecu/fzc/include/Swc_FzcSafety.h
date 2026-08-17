/**
 * @file    Swc_FzcSafety.h
 * @brief   FZC local safety monitoring — watchdog, plausibility aggregation
 * @date    2026-02-23
 *
 * @safety_req SWR-FZC-023, SWR-FZC-025
 * @traces_to  SSR-FZC-023, SSR-FZC-025, TSR-046
 *
 * @standard AUTOSAR SWC pattern, ISO 26262 Part 6
 * @copyright Taktflow Systems 2026
 */
#ifndef SWC_FZC_SAFETY_H
#define SWC_FZC_SAFETY_H

#include "Std_Types.h"

/* ==================================================================
 * API Functions
 * ================================================================== */

void Swc_FzcSafety_Init(void);
void Swc_FzcSafety_MainFunction(void);
uint8 Swc_FzcSafety_GetStatus(void);

#ifdef UNIT_TEST
uint8   Swc_FzcSafety_GetInitialized(void);
uint16  Swc_FzcSafety_GetGraceCounter(void);
uint8   Swc_FzcSafety_GetSelfTestDone(void);
uint8   Swc_FzcSafety_GetWdiToggle(void);
void    Swc_FzcSafety_SetSelfTestDone(uint8 done);
#endif /* UNIT_TEST */

#endif /* SWC_FZC_SAFETY_H */
