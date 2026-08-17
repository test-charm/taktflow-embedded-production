/**
 * @file    fzc_scheduler_harness.c
 * @brief   Native test harness for Swc_FzcScheduler (FZC runnable table SWC)
 * @date    2026-08-17
 *
 * @details Links the REAL production Swc_FzcScheduler.c and drives its
 *          Swc_FzcScheduler_Init / Swc_FzcScheduler_GetTable /
 *          Swc_FzcScheduler_GetCount APIs through a phase script read
 *          from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens.
 *
 *          Phase keys:
 *            skipInit      0|1   skip Swc_FzcScheduler_Init (uninitialized guard)
 *            reinit        0|1   call Swc_FzcScheduler_Init again at phase start
 *                                 (idempotent re-init)
 *
 *          Output is a single JSON object on stdout:
 *            {"initialized":1,"tableMatches":1,"runnableCount":7,
 *             "runnables":[{"index":0,"name":"SteeringMonitor",
 *                           "periodMs":10,"priority":3,"wcetUs":800,
 *                           "asilLevel":4},...],
 *             "safetyPriorityOk":1,"wcetWithinCycle":1,"wcetTotalUs":2900}
 *          where runnables is the table read back through
 *          Swc_FzcScheduler_GetTable (the SWC owns a static const table, so
 *          the read-back equals SWR-FZC-029), tableMatches is 1 when the
 *          read-back table matches the SWR-FZC-029 expected values field by
 *          field (name / periodMs / priority / wcetUs / asilLevel),
 *          safetyPriorityOk / wcetWithinCycle / wcetTotalUs are data checks
 *          matching the ASIL-D unit-test assertions (SWR-FZC-029).
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_FzcScheduler.h"

/* ASIL levels (mirror Swc_FzcScheduler.c readability constants) */
#define SCHED_ASIL_QM  0u
#define SCHED_ASIL_A   1u
#define SCHED_ASIL_B   2u
#define SCHED_ASIL_C   3u
#define SCHED_ASIL_D   4u

/* ==================================================================
 * Expected SWR-FZC-029 production table
 * ================================================================== */

static const Swc_FzcScheduler_RunnableType expected_table[FZC_SCHED_RUNNABLE_COUNT] = {
    /* 0: SteeringMonitor — ASIL D, 10ms, High priority */
    {
        "SteeringMonitor",
        10u,
        FZC_SCHED_PRIO_HIGH,
        800u,
        SCHED_ASIL_D
    },
    /* 1: BrakeMonitor — ASIL D, 10ms, High priority */
    {
        "BrakeMonitor",
        10u,
        FZC_SCHED_PRIO_HIGH,
        600u,
        SCHED_ASIL_D
    },
    /* 2: LidarMonitor — ASIL C, 10ms, High priority */
    {
        "LidarMonitor",
        10u,
        FZC_SCHED_PRIO_HIGH,
        500u,
        SCHED_ASIL_C
    },
    /* 3: CanReceive — ASIL D, 10ms, High priority */
    {
        "CanReceive",
        10u,
        FZC_SCHED_PRIO_HIGH,
        400u,
        SCHED_ASIL_D
    },
    /* 4: HeartbeatTx — ASIL B, 50ms, Med priority */
    {
        "HeartbeatTx",
        50u,
        FZC_SCHED_PRIO_MED,
        200u,
        SCHED_ASIL_B
    },
    /* 5: WatchdogFeed — ASIL D, 100ms, High priority */
    {
        "WatchdogFeed",
        100u,
        FZC_SCHED_PRIO_HIGH,
        100u,
        SCHED_ASIL_D
    },
    /* 6: BuzzerDriver — QM, 10ms, Low priority */
    {
        "BuzzerDriver",
        10u,
        FZC_SCHED_PRIO_LOW,
        300u,
        SCHED_ASIL_QM
    }
};

/* ==================================================================
 * Phase parsing
 * ================================================================== */

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 0);
}

typedef struct {
    uint8_t skip_init;
    uint8_t reinit;
} Phase;

static int run_phase(const Phase* p)
{
    if (p->reinit != 0u) {
        Swc_FzcScheduler_Init();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */

static int table_matches(const Swc_FzcScheduler_RunnableType* table)
{
    uint8 i;
    if (table == NULL_PTR) {
        return 0;
    }
    for (i = 0u; i < FZC_SCHED_RUNNABLE_COUNT; i++) {
        if ((strcmp(table[i].name, expected_table[i].name) != 0) ||
            (table[i].periodMs != expected_table[i].periodMs) ||
            (table[i].priority != expected_table[i].priority) ||
            (table[i].wcetUs != expected_table[i].wcetUs) ||
            (table[i].asilLevel != expected_table[i].asilLevel)) {
            return 0;
        }
    }
    return 1;
}

static void print_runnables(const Swc_FzcScheduler_RunnableType* table)
{
    uint8 i;
    printf("  \"runnables\": [");
    if (table != NULL_PTR) {
        for (i = 0u; i < FZC_SCHED_RUNNABLE_COUNT; i++) {
            if (i > 0u) {
                printf(",");
            }
            printf("{\"index\":%u,\"name\":\"%s\",\"periodMs\":%u,"
                   "\"priority\":%u,\"wcetUs\":%u,\"asilLevel\":%u}",
                   (unsigned)i,
                   table[i].name,
                   (unsigned)table[i].periodMs,
                   (unsigned)table[i].priority,
                   (unsigned)table[i].wcetUs,
                   (unsigned)table[i].asilLevel);
        }
    }
    printf("]");
}

static int data_checks_ok(const Swc_FzcScheduler_RunnableType* table,
                          int* safety_ok, int* wcet_ok, uint32* total_wcet)
{
    uint8  i;
    uint8  min_safety_prio;
    uint8  max_qm_prio;
    uint32 total;

    *safety_ok = 0;
    *wcet_ok   = 0;
    *total_wcet = 0u;
    if (table == NULL_PTR) {
        return 0;
    }

    min_safety_prio = 0xFFu;
    max_qm_prio     = 0u;
    total           = 0u;

    for (i = 0u; i < FZC_SCHED_RUNNABLE_COUNT; i++) {
        total += (uint32)table[i].wcetUs;
        if (table[i].asilLevel >= SCHED_ASIL_C) {
            if (table[i].priority < min_safety_prio) {
                min_safety_prio = table[i].priority;
            }
        } else if (table[i].asilLevel == SCHED_ASIL_QM) {
            if (table[i].priority > max_qm_prio) {
                max_qm_prio = table[i].priority;
            }
        }
    }

    *total_wcet = total;
    /* Safety (ASIL C/D) runnables must be strictly above QM priority. */
    *safety_ok = (min_safety_prio > max_qm_prio) ? 1 : 0;
    /* Total WCET must be under 80% of the 10ms (10000us) shortest period. */
    *wcet_ok = (total < 8000u) ? 1 : 0;
    return 0;
}

int main(void)
{
    char line[1024];
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;
    const Swc_FzcScheduler_RunnableType* table;
    int safety_ok;
    int wcet_ok;
    uint32 total_wcet;

    /* ---- parse all phases (skipInit is decided on phase[0]) ---- */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        Phase p;
        char* token;
        char* saveptr = NULL;

        if (phase_count >= sizeof(phases) / sizeof(phases[0])) {
            fprintf(stderr, "too many phases (max 64)\n");
            return 2;
        }

        memset(&p, 0, sizeof(p));

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    uint32_t val = parse_uint(eq + 1);
                    if (strcmp(key, "skipInit") == 0) p.skip_init = (uint8_t)val;
                    else if (strcmp(key, "reinit") == 0) p.reinit = (uint8_t)val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    /* Init once per harness run unless the first phase skips it. */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_FzcScheduler_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    table = Swc_FzcScheduler_GetTable();
    data_checks_ok(table, &safety_ok, &wcet_ok, &total_wcet);

    printf("{\"initialized\":%u,\"tableMatches\":%d,\"runnableCount\":%u,",
           (unsigned)(table != NULL_PTR ? 1u : 0u),
           table_matches(table),
           (unsigned)Swc_FzcScheduler_GetCount());
    print_runnables(table);
    printf(",\"safetyPriorityOk\":%d,\"wcetWithinCycle\":%d,\"wcetTotalUs\":%u}\n",
           safety_ok, wcet_ok, (unsigned)total_wcet);

    return 0;
}
