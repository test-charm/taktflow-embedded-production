/**
 * @file    cvc_heartbeat_harness.c
 * @brief   Native test harness for Swc_Heartbeat (CVC heartbeat SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Heartbeat.c and drives its four
 *          APIs through a phase script read from stdin. Each phase is one
 *          line of whitespace-separated key=value tokens. The harness pins
 *          the RTE vehicle-state signal (read at the 50 ms TX boundary),
 *          counts WdgM checkpoints, and exposes the SWC's internal heartbeat
 *          state (alive counter, RX flags, comm status, E2E SM status) via
 *          the UNIT_TEST-only observation getters.
 *
 *          Phase keys:
 *            cycles      uint32   (default 1) Swc_Heartbeat_MainFunction calls
 *            skipInit    0|1      skip Swc_Heartbeat_Init (uninitialized guard)
 *            vehicleState uint32  RTE CVC_SIG_VEHICLE_STATE input read at TX
 *                                  boundary (0=INIT 1=RUN 2=DEGRADED 3=LIMP
 *                                  4=SAFE_STOP 5=SHUTDOWN)
 *            rxEcu       uint32   call Swc_Heartbeat_RxIndication(ecuId)
 *                                  0=none, 2=FZC, 3=RZC, else unknown
 *            resetComm   0|1      call Swc_Heartbeat_ResetCommStatus() after
 *                                  cycles (post-INIT grace comm reset)
 *
 *          Output is a single JSON object on stdout:
 *            {"aliveCounter":0,"wdgmCheckpointCount":0,"wdgmLastSeId":0,
 *             "operatingMode":0,
 *             "fzcRxFlag":0,"rzcRxFlag":0,
 *             "fzcCommStatus":1,"rzcCommStatus":1,
 *             "fzcSmStatus":0,"rzcSmStatus":0,
 *             "rteFzcCommStatus":1,"rteRzcCommStatus":1}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_Heartbeat.h"
#include "Rte.h"
#include "WdgM.h"
#include "E2E_Sm.h"

#define MOCK_RTE_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];

/* WdgM checkpoint capture */
static uint32_t wdgm_checkpoint_count;
static uint32_t wdgm_last_se_id;

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

Std_ReturnType WdgM_CheckpointReached(WdgM_SupervisedEntityIdType SEId)
{
    wdgm_checkpoint_count++;
    wdgm_last_se_id = (uint32)SEId;
    return E_OK;
}

void E2E_Sm_Init(E2E_SmStateType* sm)
{
    /* Minimal stub: E2E_Sm.c is not linked in this harness. Reset the SM
     * to the INIT state so ResetCommStatus's forced-VALID transition is
     * observable through the UNIT_TEST getters. */
    if (sm == NULL_PTR) {
        return;
    }
    sm->Status = E2E_SM_INIT;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 10);
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint32_t vehicle_state;
    uint32_t rx_ecu;
    uint8_t  reset_comm;
} Phase;

static int run_phase(const Phase* p)
{
    uint32_t i;

    /* Pin the vehicle-state RTE input read at the TX boundary */
    mock_rte_signals[CVC_SIG_VEHICLE_STATE] = p->vehicle_state;

    if (p->rx_ecu != 0u) {
        Swc_Heartbeat_RxIndication((uint8)p->rx_ecu);
    }

    for (i = 0u; i < p->cycles; i++) {
        Swc_Heartbeat_MainFunction();
    }

    if (p->reset_comm != 0u) {
        Swc_Heartbeat_ResetCommStatus();
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
    mock_rte_signals[CVC_SIG_VEHICLE_STATE] = CVC_STATE_RUN;

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
        p.vehicle_state = CVC_STATE_RUN;

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
                    else if (strcmp(key, "rxEcu") == 0)    p.rx_ecu = val;
                    else if (strcmp(key, "resetComm") == 0) p.reset_comm = (uint8_t)val;
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

    printf("{\"aliveCounter\":%u,\"wdgmCheckpointCount\":%u,\"wdgmLastSeId\":%u,"
           "\"operatingMode\":%u,"
           "\"fzcRxFlag\":%u,\"rzcRxFlag\":%u,"
           "\"fzcCommStatus\":%u,\"rzcCommStatus\":%u,"
           "\"fzcSmStatus\":%u,\"rzcSmStatus\":%u,"
           "\"rteFzcCommStatus\":%u,\"rteRzcCommStatus\":%u}\n",
           (unsigned)Swc_Heartbeat_GetAliveCounter(),
           (unsigned)wdgm_checkpoint_count,
           (unsigned)wdgm_last_se_id,
           (unsigned)mock_rte_signals[CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE],
           (unsigned)(Swc_Heartbeat_GetFzcRxFlag() ? 1u : 0u),
           (unsigned)(Swc_Heartbeat_GetRzcRxFlag() ? 1u : 0u),
           (unsigned)Swc_Heartbeat_GetFzcCommStatus(),
           (unsigned)Swc_Heartbeat_GetRzcCommStatus(),
           (unsigned)Swc_Heartbeat_GetFzcSmStatus(),
           (unsigned)Swc_Heartbeat_GetRzcSmStatus(),
           (unsigned)mock_rte_signals[CVC_SIG_FZC_COMM_STATUS],
           (unsigned)mock_rte_signals[CVC_SIG_RZC_COMM_STATUS]);

    return 0;
}
