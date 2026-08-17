/**
 * @file    cvc_scheduler_harness.c
 * @brief   Native test harness for Swc_Scheduler (CVC runnable table SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Scheduler.c and drives its
 *          Swc_Scheduler_Init / Swc_Scheduler_GetConfig /
 *          Swc_Scheduler_GetRunnableCount APIs through a phase script read
 *          from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens.
 *
 *          Phase keys:
 *            skipInit      0|1   skip Swc_Scheduler_Init (uninitialized guard)
 *            initNull      0|1   call Swc_Scheduler_Init(NULL_PTR)
 *                                 (NULL-config guard → de-init)
 *            nullRunnables 0|1   Init with config.runnables == NULL_PTR
 *                                 (null-runnables guard → de-init)
 *            zeroCount     0|1   Init with config.runnableCount == 0
 *                                 (zero-count guard → de-init)
 *            tableIndex    uint  config table to use for a valid Init:
 *                                  0 = production 8-runnable table (SWR-CVC-032)
 *                                  1 = minimal 1-runnable table (count boundary)
 *                                  2 = max 16-runnable table (SCHED_MAX_RUNNABLES)
 *                                 (default 0)
 *
 *          Output is a single JSON object on stdout:
 *            {"initialized":1,"configMatches":1,"runnableCount":8,
 *             "runnables":[{"runnableId":0,"periodMs":10,"priority":10,
 *                           "wcetUs":200,"asilLevel":4},...],
 *             "safetyPriorityOk":1,"wcetWithinCycle":1}
 *          where runnables is the table read back through Swc_Scheduler_GetConfig
 *          (the SWC stores a pointer to the config, so the read-back equals the
 *          passed table), safetyPriorityOk / wcetWithinCycle are data checks
 *          computed from the returned table matching the ASIL-D unit-test
 *          assertions (SWR-CVC-032).
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_Scheduler.h"

#include "harness_common.h"

/* ==================================================================
 * Runnable tables (mirror SWR-CVC-032 default configuration)
 * ================================================================== */

/* Production table — ID 0..7 as documented in Swc_Scheduler.c header. */
static const Swc_Scheduler_RunnableType prod_runnables[] = {
    {  0u,  10u, 10u,  200u, SCHED_ASIL_D },  /* Swc_Pedal        */
    {  1u,  10u, 10u,  100u, SCHED_ASIL_D },  /* Swc_VehicleState */
    {  2u,  10u, 11u,   50u, SCHED_ASIL_D },  /* Swc_EStop        */
    {  3u,  50u,  8u,  150u, SCHED_ASIL_B },  /* Swc_Heartbeat    */
    {  4u, 200u,  3u,  500u, SCHED_ASIL_QM }, /* Swc_Dashboard    */
    {  5u,  10u,  9u,  100u, SCHED_ASIL_D },  /* Swc_CanMonitor   */
    {  6u, 100u,  9u,   50u, SCHED_ASIL_D },  /* Swc_Watchdog     */
    {  7u,  10u, 10u,  200u, SCHED_ASIL_D },  /* Swc_CvcCom       */
};

/* Minimal table — single runnable (runnableCount boundary = 1). */
static const Swc_Scheduler_RunnableType min_runnables[] = {
    {  9u,  10u,  5u,  100u, SCHED_ASIL_QM },
};

/* Max table — 16 runnables (SCHED_MAX_RUNNABLES boundary). */
static const Swc_Scheduler_RunnableType max_runnables[16] = {
    {  0u,  10u, 10u,  200u, SCHED_ASIL_D },
    {  1u,  10u, 10u,  100u, SCHED_ASIL_D },
    {  2u,  10u, 11u,   50u, SCHED_ASIL_D },
    {  3u,  50u,  8u,  150u, SCHED_ASIL_B },
    {  4u, 200u,  3u,  500u, SCHED_ASIL_QM },
    {  5u,  10u,  9u,  100u, SCHED_ASIL_D },
    {  6u, 100u,  9u,   50u, SCHED_ASIL_D },
    {  7u,  10u, 10u,  200u, SCHED_ASIL_D },
    {  8u,  20u,  7u,  100u, SCHED_ASIL_C },
    {  9u,  50u,  6u,  150u, SCHED_ASIL_B },
    { 10u,  10u, 12u,  100u, SCHED_ASIL_D },
    { 11u, 100u,  5u,  200u, SCHED_ASIL_B },
    { 12u,  20u,  4u,  100u, SCHED_ASIL_C },
    { 13u,  50u,  3u,  150u, SCHED_ASIL_A },
    { 14u,  10u, 13u,  100u, SCHED_ASIL_D },
    { 15u, 200u,  2u,  500u, SCHED_ASIL_QM },
};

/* ==================================================================
 * Config holders (one per table so the SWC can keep a stable pointer)
 * ================================================================== */

static Swc_Scheduler_ConfigType prod_cfg;
static Swc_Scheduler_ConfigType min_cfg;
static Swc_Scheduler_ConfigType max_cfg;
static Swc_Scheduler_ConfigType null_runnables_cfg;   /* runnables = NULL  */
static Swc_Scheduler_ConfigType zero_count_cfg;        /* runnableCount = 0 */

static void init_cfgs(void)
{
    prod_cfg.runnables     = prod_runnables;
    prod_cfg.runnableCount = (uint8)(sizeof(prod_runnables) / sizeof(prod_runnables[0]));

    min_cfg.runnables     = min_runnables;
    min_cfg.runnableCount = (uint8)(sizeof(min_runnables) / sizeof(min_runnables[0]));

    max_cfg.runnables     = max_runnables;
    max_cfg.runnableCount = (uint8)(sizeof(max_runnables) / sizeof(max_runnables[0]));

    null_runnables_cfg.runnables     = NULL_PTR;
    null_runnables_cfg.runnableCount = 8u;

    zero_count_cfg.runnables     = prod_runnables;
    zero_count_cfg.runnableCount = 0u;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint8_t  skip_init;
    uint8_t  init_null;
    uint8_t  null_runnables;
    uint8_t  zero_count;
    uint32_t table_index;
} Phase;

static int run_phase(const Phase* p)
{
    if (p->skip_init != 0u) {
        return 0;
    }

    if (p->init_null != 0u) {
        Swc_Scheduler_Init(NULL_PTR);
        return 0;
    }

    if (p->null_runnables != 0u) {
        Swc_Scheduler_Init(&null_runnables_cfg);
        return 0;
    }

    if (p->zero_count != 0u) {
        Swc_Scheduler_Init(&zero_count_cfg);
        return 0;
    }

    switch (p->table_index) {
        case 1u:
            Swc_Scheduler_Init(&min_cfg);
            break;
        case 2u:
            Swc_Scheduler_Init(&max_cfg);
            break;
        default:
            Swc_Scheduler_Init(&prod_cfg);
            break;
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */

static void print_runnables(const Swc_Scheduler_ConfigType* cfg)
{
    uint8 i;
    printf("  \"runnables\": [");
    if (cfg != NULL_PTR) {
        for (i = 0u; i < cfg->runnableCount; i++) {
            if (i > 0u) {
                printf(",");
            }
            printf("{\"runnableId\":%u,\"periodMs\":%u,\"priority\":%u,"
                   "\"wcetUs\":%u,\"asilLevel\":%u}",
                   (unsigned)cfg->runnables[i].runnableId,
                   (unsigned)cfg->runnables[i].periodMs,
                   (unsigned)cfg->runnables[i].priority,
                   (unsigned)cfg->runnables[i].wcetUs,
                   (unsigned)cfg->runnables[i].asilLevel);
        }
    }
    printf("]");
}

static int data_checks_ok(const Swc_Scheduler_ConfigType* cfg,
                          int* safety_ok, int* wcet_ok)
{
    uint8  i;
    uint8  min_safety_prio;
    uint8  max_qm_prio;
    uint32 total_wcet_us;
    uint16 shortest_period_ms;

    *safety_ok = 0;
    *wcet_ok   = 0;
    if (cfg == NULL_PTR || cfg->runnables == NULL_PTR || cfg->runnableCount == 0u) {
        return 0;
    }

    min_safety_prio  = 0xFFu;
    max_qm_prio      = 0u;
    total_wcet_us    = 0u;
    shortest_period_ms = 0xFFFFu;

    for (i = 0u; i < cfg->runnableCount; i++) {
        total_wcet_us += (uint32)cfg->runnables[i].wcetUs;
        if (cfg->runnables[i].periodMs < shortest_period_ms) {
            shortest_period_ms = cfg->runnables[i].periodMs;
        }
        if (cfg->runnables[i].asilLevel >= SCHED_ASIL_B) {
            if (cfg->runnables[i].priority < min_safety_prio) {
                min_safety_prio = cfg->runnables[i].priority;
            }
        } else {
            if (cfg->runnables[i].priority > max_qm_prio) {
                max_qm_prio = cfg->runnables[i].priority;
            }
        }
    }

    /* Every safety task priority must be > every QM task priority. */
    *safety_ok = (min_safety_prio > max_qm_prio) ? 1 : 0;
    /* Total WCET must fit within one shortest cycle. */
    *wcet_ok = (total_wcet_us < ((uint32)shortest_period_ms * 1000u)) ? 1 : 0;
    return 0;
}


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->table_index = 0u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "skipInit") == 0)          p->skip_init = (uint8_t)val;
    else if (strcmp(key, "initNull") == 0)     p->init_null = (uint8_t)val;
    else if (strcmp(key, "nullRunnables") == 0) p->null_runnables = (uint8_t)val;
    else if (strcmp(key, "zeroCount") == 0)    p->zero_count = (uint8_t)val;
    else if (strcmp(key, "tableIndex") == 0)   p->table_index = val;
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    const Swc_Scheduler_ConfigType* cfg;
    const Swc_Scheduler_ConfigType* last_cfg = NULL_PTR;
    int safety_ok;
    int wcet_ok;

    init_cfgs();

    /* ---- parse all phases ---- */
        {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
        /* Track the config the last phase attempted to set. */
        if (phases[pi].skip_init != 0u) {
            continue;
        }
        if (phases[pi].init_null != 0u) {
            last_cfg = NULL_PTR;
        } else if (phases[pi].null_runnables != 0u) {
            last_cfg = &null_runnables_cfg;
        } else if (phases[pi].zero_count != 0u) {
            last_cfg = &zero_count_cfg;
        } else if (phases[pi].table_index == 1u) {
            last_cfg = &min_cfg;
        } else if (phases[pi].table_index == 2u) {
            last_cfg = &max_cfg;
        } else {
            last_cfg = &prod_cfg;
        }
    }

    cfg = Swc_Scheduler_GetConfig();
    data_checks_ok(cfg, &safety_ok, &wcet_ok);

    printf("{\"initialized\":%u,\"configMatches\":%u,\"runnableCount\":%u,",
           (unsigned)(cfg != NULL_PTR ? 1u : 0u),
           (unsigned)(cfg == last_cfg ? 1u : 0u),
           (unsigned)Swc_Scheduler_GetRunnableCount());
    print_runnables(cfg);
    printf(",\"safetyPriorityOk\":%d,\"wcetWithinCycle\":%d}\n",
           safety_ok, wcet_ok);

    return 0;
}
