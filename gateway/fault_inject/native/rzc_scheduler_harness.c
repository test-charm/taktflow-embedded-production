/**
 * @file    rzc_scheduler_harness.c
 * @brief   Native test harness for Swc_RzcScheduler (RZC runnable table SWC)
 * @date    2026-08-18
 *
 * @details Links the REAL production Swc_RzcScheduler.c and drives its
 *          Swc_RzcScheduler_Init / Swc_RzcScheduler_Tick /
 *          Swc_RzcScheduler_GetTable / Swc_RzcScheduler_GetUtilPct APIs
 *          through a phase script read from stdin. Each phase is one line
 *          of whitespace-separated key=value tokens.
 *
 *          Phase keys:
 *            skipInit      0|1   skip Swc_RzcScheduler_Init (uninitialized guard)
 *            reinit        0|1   call Swc_RzcScheduler_Init again at phase start
 *                                 (idempotent re-init)
 *            ticks         uint  number of Swc_RzcScheduler_Tick calls
 *                                 (dispatch simulation, default 0)
 *
 *          The 8 runnable entry points referenced by the production
 *          RzcSched_Table are mocked here as counters, so each Tick
 *          dispatch is observable per-runnable.
 *
 *          Output is a single JSON object on stdout:
 *            {"initialized":1,"tableMatches":1,"runnableCount":8,
 *             "runnables":[{"index":0,"periodMs":1,"priority":1,
 *                           "wcetUs":50},...],
 *             "priorityOrdered":1,"utilPct":10,"utilUnderMax":1,
 *             "ticks":10,"dispatchTotal":12,
 *             "tickCounts":[10,1,1,1,0,0,0,0],
 *             "currentMonitor":10,"motor":1,"encoder":1,"comReceive":1,
 *             "temp":0,"battery":0,"heartbeat":0,"wdgm":0}
 *          where runnables is the table read back through
 *          Swc_RzcScheduler_GetTable (the SWC owns a static const table, so
 *          the read-back equals SWR-RZC-028), tableMatches is 1 when the
 *          read-back table matches the SWR-RZC-028 expected values field by
 *          field (period_ms / priority / wcet_us), initialized is read back
 *          through the UNIT_TEST-only getter, priorityOrdered checks the
 *          table is sorted by priority (safety preempts QM), and
 *          utilPct / utilUnderMax match the ASIL-D unit-test assertions.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_RzcScheduler.h"

#include "harness_common.h"

/* ==================================================================
 * Mock runnable entry-point counters
 * ================================================================== */

static uint32_t cnt_current_monitor;
static uint32_t cnt_motor;
static uint32_t cnt_encoder;
static uint32_t cnt_com_receive;
static uint32_t cnt_temp;
static uint32_t cnt_battery;
static uint32_t cnt_heartbeat;
static uint32_t cnt_wdgm;

void Swc_CurrentMonitor_MainFunction(void) { cnt_current_monitor++; }
void Swc_Motor_MainFunction(void)          { cnt_motor++; }
void Swc_Encoder_MainFunction(void)        { cnt_encoder++; }
void Swc_RzcCom_Receive(void)              { cnt_com_receive++; }
void Swc_TempMonitor_MainFunction(void)    { cnt_temp++; }
void Swc_Battery_MainFunction(void)        { cnt_battery++; }
void Swc_Heartbeat_MainFunction(void)      { cnt_heartbeat++; }
void WdgM_MainFunction(void)               { cnt_wdgm++; }

/* ==================================================================
 * Expected SWR-RZC-028 production table (period_ms / priority / wcet_us)
 * ================================================================== */

typedef struct {
    uint16_t period_ms;
    uint8_t  priority;
    uint16_t wcet_us;
} ExpectedEntry;

static const ExpectedEntry expected_table[RZC_SCHED_RUNNABLE_COUNT] = {
    {  1u, 1u,  50u },  /* 0: CurrentMonitor   — 1ms  Highest(1) */
    { 10u, 2u, 200u },  /* 1: MotorControl     — 10ms High(2)    */
    { 10u, 2u, 100u },  /* 2: DirectionMonitor — 10ms High(2)    */
    { 10u, 2u, 150u },  /* 3: CanReceive       — 10ms High(2)    */
    {100u, 3u, 300u },  /* 4: TempMonitor      — 100ms Med(3)    */
    {100u, 3u, 200u },  /* 5: BatteryMonitor   — 100ms Med(3)    */
    { 50u, 3u, 100u },  /* 6: HeartbeatTx      — 50ms  Med(3)    */
    {100u, 3u,  50u },  /* 7: WatchdogFeed     — 100ms Med(3)    */
};

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint8_t  skip_init;
    uint8_t  reinit;
    uint32_t ticks;
} Phase;

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->ticks = 0u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "reinit") == 0) p->reinit = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "ticks") == 0) p->ticks = harness_parse_uint(value);
    return 0;
}

static int run_phase(const Phase* p)
{
    uint32_t i;
    if (p->reinit != 0u) {
        Swc_RzcScheduler_Init();
    }
    for (i = 0u; i < p->ticks; i++) {
        Swc_RzcScheduler_Tick();
    }
    return 0;
}

/* ==================================================================
 * Table / data checks
 * ================================================================== */

static int table_matches(const Swc_RzcSched_RunnableType* table)
{
    uint8 i;
    if (table == NULL_PTR) {
        return 0;
    }
    for (i = 0u; i < RZC_SCHED_RUNNABLE_COUNT; i++) {
        if ((table[i].period_ms != expected_table[i].period_ms) ||
            (table[i].priority != expected_table[i].priority) ||
            (table[i].wcet_us != expected_table[i].wcet_us)) {
            return 0;
        }
    }
    return 1;
}

static int priority_ordered(const Swc_RzcSched_RunnableType* table)
{
    uint8 i;
    if (table == NULL_PTR) {
        return 0;
    }
    for (i = 1u; i < RZC_SCHED_RUNNABLE_COUNT; i++) {
        if (table[i].priority < table[i - 1u].priority) {
            return 0;
        }
    }
    return 1;
}

static void print_runnables(const Swc_RzcSched_RunnableType* table)
{
    uint8 i;
    printf("  \"runnables\": [");
    if (table != NULL_PTR) {
        for (i = 0u; i < RZC_SCHED_RUNNABLE_COUNT; i++) {
            if (i > 0u) {
                printf(",");
            }
            printf("{\"index\":%u,\"periodMs\":%u,\"priority\":%u,\"wcetUs\":%u}",
                   (unsigned)i,
                   (unsigned)table[i].period_ms,
                   (unsigned)table[i].priority,
                   (unsigned)table[i].wcet_us);
        }
    }
    printf("]");
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count;
    size_t pi;
    uint8_t global_skip_init = 0u;
    uint32_t total_ticks = 0u;
    uint32_t dispatch_total;
    const Swc_RzcSched_RunnableType* table;
    uint8_t util_pct;
    int n;

    /* ---- parse all phases (skipInit is decided on phase[0]) ---- */
    n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                            set_phase_field, NULL);
    if (n < 0) {
        return 2;
    }
    phase_count = (size_t)n;

    /* Init once per harness run unless the first phase skips it. */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_RzcScheduler_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
        total_ticks += phases[pi].ticks;
    }

    table = Swc_RzcScheduler_GetTable();
    util_pct = Swc_RzcScheduler_GetUtilPct();
    dispatch_total = cnt_current_monitor + cnt_motor + cnt_encoder +
                     cnt_com_receive + cnt_temp + cnt_battery +
                     cnt_heartbeat + cnt_wdgm;

    printf("{\"initialized\":%u,\"tableMatches\":%d,\"runnableCount\":%u,",
           (unsigned)Swc_RzcScheduler_GetInitialized(),
           table_matches(table),
           (unsigned)RZC_SCHED_RUNNABLE_COUNT);
    print_runnables(table);
    printf(",\"priorityOrdered\":%d,\"utilPct\":%u,\"utilUnderMax\":%d,",
           priority_ordered(table),
           (unsigned)util_pct,
           (util_pct < RZC_SCHED_MAX_UTIL_PCT) ? 1 : 0);
    printf("\"ticks\":%u,\"dispatchTotal\":%u,\"tickCounts\":[%u,%u,%u,%u,%u,%u,%u,%u],",
           (unsigned)total_ticks,
           (unsigned)dispatch_total,
           (unsigned)cnt_current_monitor, (unsigned)cnt_motor,
           (unsigned)cnt_encoder, (unsigned)cnt_com_receive,
           (unsigned)cnt_temp, (unsigned)cnt_battery,
           (unsigned)cnt_heartbeat, (unsigned)cnt_wdgm);
    printf("\"currentMonitor\":%u,\"motor\":%u,\"encoder\":%u,\"comReceive\":%u,",
           (unsigned)cnt_current_monitor, (unsigned)cnt_motor,
           (unsigned)cnt_encoder, (unsigned)cnt_com_receive);
    printf("\"temp\":%u,\"battery\":%u,\"heartbeat\":%u,\"wdgm\":%u}\n",
           (unsigned)cnt_temp, (unsigned)cnt_battery,
           (unsigned)cnt_heartbeat, (unsigned)cnt_wdgm);

    return 0;
}
