/**
 * @file    cvc_canmonitor_harness.c
 * @brief   Native test harness for Swc_CanMonitor (CVC CAN bus monitor SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_CanMonitor.c and drives its
 *          Init / Check / Recovery / GetStatus APIs through a phase script
 *          read from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens. The harness advances currentTimeMs per Check
 *          call (timeStartMs + timeStepMs*i), optionally increments the RX
 *          message count (rxInc) to distinguish "messages arriving" from
 *          "bus silence", and optionally calls Recovery after the Check
 *          cycles.
 *
 *          Phase keys:
 *            cycles         uint32   (default 1) Swc_CanMonitor_Check calls
 *            skipInit       0|1      skip Swc_CanMonitor_Init (uninitialized guard)
 *            isBusOff       0|1      Check isBusOff arg (bus-off detection)
 *            rxMsgCount     uint32   Check rxMsgCount arg (total RX count);
 *                                    overrides the monotonic running counter
 *                                    carried across phases
 *            rxInc          0|1      increment the running RX counter each Check
 *                                    call (1 = messages arriving, 0 = silence)
 *            errorWarning   0|1      Check errorWarning arg
 *            timeStartMs    uint32   currentTimeMs for first Check call
 *            timeStepMs     uint32   currentTimeMs delta between Check calls
 *                                    (default 100)
 *            recovery       0|1      call Swc_CanMonitor_Recovery after cycles
 *            recoveryTimeMs uint32   currentTimeMs for the Recovery call
 *
 *          Output is a single JSON object on stdout:
 *            {"status":0,"checkResult":0,"recoveryResult":-1,
 *             "initialized":1,
 *             "lastRxCount":0,"lastRxTimeMs":0,
 *             "errorWarnActive":0,"errorWarnStartMs":0,
 *             "recoveryAttempts":0,"recoveryWindowStartMs":0}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_CanMonitor.h"

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
    uint8_t  is_bus_off;
    uint32_t rx_msg_count;
    uint8_t  rx_count_set;
    uint8_t  rx_inc;
    uint8_t  error_warning;
    uint32_t time_start_ms;
    uint32_t time_step_ms;
    uint8_t  recovery;
    uint32_t recovery_time_ms;
} Phase;

/* Monotonic RX message count carried across phases (as in the real system,
 * the total received-message counter never goes backwards). A phase may
 * override it explicitly via rxMsgCount, e.g. to start a fresh silence
 * window at a specific count. */
static uint32_t running_rx_count;

static int run_phase(const Phase* p, uint8_t* check_result, int* recovery_result)
{
    uint32_t i;

    if (p->rx_count_set != 0u) {
        running_rx_count = p->rx_msg_count;
    }

    for (i = 0u; i < p->cycles; i++) {
        uint32_t time_ms = p->time_start_ms + (p->time_step_ms * i);
        *check_result = Swc_CanMonitor_Check((uint8)p->is_bus_off, running_rx_count,
                                             (uint8)p->error_warning, time_ms);
        if (p->rx_inc != 0u) {
            running_rx_count++;
        }
    }

    if (p->recovery != 0u) {
        *recovery_result = (int)Swc_CanMonitor_Recovery(p->recovery_time_ms);
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
    uint8_t check_result = 0u;
    int recovery_result = -1;

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
        p.time_step_ms = 100u;

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    uint32_t val = parse_uint(eq + 1);
                    if (strcmp(key, "cycles") == 0)           p.cycles = val;
                    else if (strcmp(key, "skipInit") == 0)    p.skip_init = (uint8_t)val;
                    else if (strcmp(key, "isBusOff") == 0)    p.is_bus_off = (uint8_t)val;
                    else if (strcmp(key, "rxMsgCount") == 0)  { p.rx_msg_count = val; p.rx_count_set = 1u; }
                    else if (strcmp(key, "rxInc") == 0)       p.rx_inc = (uint8_t)val;
                    else if (strcmp(key, "errorWarning") == 0) p.error_warning = (uint8_t)val;
                    else if (strcmp(key, "timeStartMs") == 0) p.time_start_ms = val;
                    else if (strcmp(key, "timeStepMs") == 0)  p.time_step_ms = val;
                    else if (strcmp(key, "recovery") == 0)    p.recovery = (uint8_t)val;
                    else if (strcmp(key, "recoveryTimeMs") == 0) p.recovery_time_ms = val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guards in Check and Recovery). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_CanMonitor_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi], &check_result, &recovery_result) != 0) {
            return 2;
        }
    }

    printf("{\"status\":%u,\"checkResult\":%u,\"recoveryResult\":%d,"
           "\"initialized\":%u,"
           "\"lastRxCount\":%u,\"lastRxTimeMs\":%u,"
           "\"errorWarnActive\":%u,\"errorWarnStartMs\":%u,"
           "\"recoveryAttempts\":%u,\"recoveryWindowStartMs\":%u}\n",
           (unsigned)Swc_CanMonitor_GetStatus(),
           (unsigned)check_result,
           recovery_result,
           (unsigned)Swc_CanMonitor_GetInitialized(),
           (unsigned)Swc_CanMonitor_GetLastRxCount(),
           (unsigned)Swc_CanMonitor_GetLastRxTimeMs(),
           (unsigned)Swc_CanMonitor_GetErrorWarnActive(),
           (unsigned)Swc_CanMonitor_GetErrorWarnStartMs(),
           (unsigned)Swc_CanMonitor_GetRecoveryAttempts(),
           (unsigned)Swc_CanMonitor_GetRecoveryWindowStartMs());

    return 0;
}
