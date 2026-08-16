/**
 * @file    fzc_canmonitor_harness.c
 * @brief   Native test harness for Swc_FzcCanMonitor (FZC CAN bus monitor SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_FzcCanMonitor.c and drives its
 *          Init / Check / GetStatus / NotifyRx APIs through a phase script
 *          read from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens. The harness advances the CAN controller mode
 *          and error counters per phase, and optionally injects NotifyRx
 *          calls before each Check to simulate messages arriving on the bus.
 *
 *          Phase keys:
 *            cycles      uint32   (default 1) Swc_FzcCanMonitor_Check calls
 *            skipInit    0|1      skip Swc_FzcCanMonitor_Init (uninitialized guard)
 *            canMode     uint32   Can_GetControllerMode(0) return value
 *                                  (0=UNINIT 1=STOPPED 2=STARTED 3=SLEEP;
 *                                  default STARTED)
 *            tec         uint32   Can_GetErrorCounters transmit error counter
 *            rec         uint32   Can_GetErrorCounters receive error counter
 *            notifyRx    0|1      call Swc_FzcCanMonitor_NotifyRx before each
 *                                  Check call (1 = messages arriving, resets
 *                                  the silence counter)
 *
 *          Output is a single JSON object on stdout:
 *            {"status":0,"initialized":1,"silenceCount":0,"graceCycles":0,
 *             "errWarnCount":0,"safeLatched":0,
 *             "brakeCmd":0,"steerCmd":0,"buzzerPattern":0,"dtcReported":0}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_FzcCanMonitor.h"
#include "Rte.h"
#include "Can.h"
#include "Dem.h"

#define MOCK_RTE_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];

/* Mocked BSW state (per-phase) */
static uint32_t mock_can_mode;
static uint8_t  mock_tec;
static uint8_t  mock_rec;
static uint8_t  mock_dtc_reported;   /* Dem_ReportErrorStatus(FZC_DTC_CAN_BUS_OFF, FAILED) count */

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

Can_StateType Can_GetControllerMode(uint8 Controller)
{
    (void)Controller;
    return (Can_StateType)mock_can_mode;
}

Std_ReturnType Can_GetErrorCounters(uint8 Controller, uint8* tec, uint8* rec)
{
    (void)Controller;
    if ((tec == NULL_PTR) || (rec == NULL_PTR)) {
        return E_NOT_OK;
    }
    *tec = mock_tec;
    *rec = mock_rec;
    return E_OK;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if ((EventId == FZC_DTC_CAN_BUS_OFF) && (EventStatus == DEM_EVENT_STATUS_FAILED)) {
        mock_dtc_reported++;
    }
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
    uint32_t can_mode;
    uint32_t tec;
    uint32_t rec;
    uint8_t  notify_rx;
} Phase;

static int run_phase(const Phase* p)
{
    uint32_t i;

    mock_can_mode = p->can_mode;
    mock_tec      = (uint8_t)p->tec;
    mock_rec      = (uint8_t)p->rec;

    for (i = 0u; i < p->cycles; i++) {
        if (p->notify_rx != 0u) {
            Swc_FzcCanMonitor_NotifyRx();
        }
        Swc_FzcCanMonitor_Check();
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
    mock_can_mode = CAN_CS_STARTED;

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
        p.can_mode = CAN_CS_STARTED;

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
                    else if (strcmp(key, "canMode") == 0)  p.can_mode = val;
                    else if (strcmp(key, "tec") == 0)      p.tec = val;
                    else if (strcmp(key, "rec") == 0)      p.rec = val;
                    else if (strcmp(key, "notifyRx") == 0) p.notify_rx = (uint8_t)val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guard in Check). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_FzcCanMonitor_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    printf("{\"status\":%u,\"initialized\":%u,\"silenceCount\":%u,\"graceCycles\":%u,"
           "\"errWarnCount\":%u,\"safeLatched\":%u,"
           "\"brakeCmd\":%u,\"steerCmd\":%u,\"buzzerPattern\":%u,\"dtcReported\":%u}\n",
           (unsigned)Swc_FzcCanMonitor_GetStatus(),
           (unsigned)Swc_FzcCanMonitor_GetInitialized(),
           (unsigned)Swc_FzcCanMonitor_GetSilenceCount(),
           (unsigned)Swc_FzcCanMonitor_GetGraceCycles(),
           (unsigned)Swc_FzcCanMonitor_GetErrWarnCount(),
           (unsigned)Swc_FzcCanMonitor_GetSafeLatched(),
           (unsigned)mock_rte_signals[FZC_SIG_BRAKE_CMD],
           (unsigned)mock_rte_signals[FZC_SIG_STEER_CMD],
           (unsigned)mock_rte_signals[FZC_SIG_BUZZER_PATTERN],
           (unsigned)mock_dtc_reported);

    return 0;
}
