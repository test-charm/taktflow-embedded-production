/**
 * @file    fzc_heartbeat_harness.c
 * @brief   Native test harness for Swc_Heartbeat (FZC heartbeat SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Heartbeat.c and drives its two
 *          APIs through a phase script read from stdin. Each phase is one
 *          line of whitespace-separated key=value tokens. The harness pins
 *          the RTE vehicle-state / fault-mask signals (read at the 50 ms TX
 *          boundary) and exposes the SWC's internal heartbeat state (alive
 *          counter, cycle counter, initialized flag) via the UNIT_TEST-only
 *          observation getters.
 *
 *          Phase keys:
 *            cycles      uint32   (default 1) Swc_Heartbeat_MainFunction calls
 *            skipInit    0|1      skip Swc_Heartbeat_Init (uninitialized guard)
 *            vehicleState uint32  RTE FZC_SIG_VEHICLE_STATE input read at TX
 *                                  boundary (0=INIT 1=RUN 2=DEGRADED 3=LIMP
 *                                  4=SAFE_STOP 5=SHUTDOWN)
 *            faultMask   uint32   RTE FZC_SIG_FAULT_MASK input read at TX
 *                                  boundary (bit 8 = FZC_FAULT_CAN_BUS_OFF
 *                                  suppresses TX)
 *
 *          Output is a single JSON object on stdout:
 *            {"aliveCounter":0,"cycleCounter":0,"initialized":1,
 *             "ecuId":2,"operatingMode":0,"faultStatus":0,"alive":0}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Fzc_App.h"
#include "Swc_Heartbeat.h"
#include "Rte.h"

#define MOCK_RTE_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];

/* ==================================================================
 * BSW stubs
 * ================================================================== */

Std_ReturnType Rte_Write(Rte_SignalIdType SignalId, uint32 Data)
{
    if (SignalId >= MOCK_RTE_MAX_SIGNALS) {
        return E_NOT_OK;
    }
    mock_rte_signals[SignalId] = Data;
    return E_OK;
}

Std_ReturnType Rte_Read(Rte_SignalIdType SignalId, uint32* DataPtr)
{
    if ((DataPtr == NULL_PTR) || (SignalId >= MOCK_RTE_MAX_SIGNALS)) {
        return E_NOT_OK;
    }
    *DataPtr = mock_rte_signals[SignalId];
    return E_OK;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 0);
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint32_t vehicle_state;
    uint32_t fault_mask;
} Phase;

static int run_phase(const Phase* p)
{
    uint32_t i;

    /* Pin the RTE inputs read at the TX boundary */
    mock_rte_signals[FZC_SIG_VEHICLE_STATE] = p->vehicle_state;
    mock_rte_signals[FZC_SIG_FAULT_MASK] = p->fault_mask;

    for (i = 0u; i < p->cycles; i++) {
        Swc_Heartbeat_MainFunction();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    char line[1024];
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    /* Defaults */
    mock_rte_signals[FZC_SIG_VEHICLE_STATE] = FZC_STATE_RUN;

    /* ---- parse all phases first (skipInit is decided on phase[0]) ---- */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        Phase p;
        char* token;
        char* saveptr = NULL;

        if (phase_count >= sizeof(phases) / sizeof(phases[0])) {
            fprintf(stderr, "too many phases (max 64)\n");
            return 2;
        }

        memset(&p, 0, sizeof(p));
        p.cycles = 1u;
        p.vehicle_state = FZC_STATE_RUN;

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    uint32_t val = parse_uint(eq + 1);
                    if (strcmp(key, "cycles") == 0)        p.cycles = val;
                    else if (strcmp(key, "skipInit") == 0) p.skip_init = (uint8_t)val;
                    else if (strcmp(key, "vehicleState") == 0) p.vehicle_state = val;
                    else if (strcmp(key, "faultMask") == 0) p.fault_mask = val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guard in MainFunction). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_Heartbeat_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    printf("{\"aliveCounter\":%u,\"cycleCounter\":%u,\"initialized\":%u,"
           "\"ecuId\":%u,\"operatingMode\":%u,\"faultStatus\":%u,\"alive\":%u}\n",
           (unsigned)Swc_Heartbeat_GetAliveCounter(),
           (unsigned)Swc_Heartbeat_GetCycleCounter(),
           (unsigned)(Swc_Heartbeat_GetInitialized() ? 1u : 0u),
           (unsigned)mock_rte_signals[FZC_SIG_FZC_HEARTBEAT_ECU_ID],
           (unsigned)mock_rte_signals[FZC_SIG_FZC_HEARTBEAT_OPERATING_MODE],
           (unsigned)mock_rte_signals[FZC_SIG_FZC_HEARTBEAT_FAULT_STATUS],
           (unsigned)mock_rte_signals[FZC_SIG_HEARTBEAT_ALIVE]);

    return 0;
}
