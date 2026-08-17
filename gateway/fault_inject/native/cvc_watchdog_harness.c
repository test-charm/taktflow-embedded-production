/**
 * @file    cvc_watchdog_harness.c
 * @brief   Native test harness for Swc_Watchdog (CVC external watchdog SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Watchdog.c and drives its
 *          Init / Feed APIs through a phase script read from stdin. Each
 *          phase is one line of whitespace-separated key=value tokens.
 *          The harness mocks Dio_FlipChannel with call/level counters and
 *          exposes the SWC's internal watchdog state (initialization flag,
 *          feed counter) via the UNIT_TEST-only observation getters.
 *
 *          Phase keys:
 *            cycles       uint32   (default 1) number of phases to repeat
 *            skipInit     0|1      skip Swc_Watchdog_Init (uninitialized guard)
 *            initNull     0|1      call Swc_Watchdog_Init(NULL_PTR)
 *                                  (NULL-config guard → de-init)
 *            loopComplete 0|1      Swc_Watchdog_Feed arg (main loop finished)
 *            canaryOk     0|1      Swc_Watchdog_Feed arg (stack canary intact)
 *            ramOk        0|1      Swc_Watchdog_Feed arg (RAM pattern passed)
 *            canOk        0|1      Swc_Watchdog_Feed arg (CAN not bus-off)
 *            feedCount    uint32   (default 1) Swc_Watchdog_Feed calls
 *
 *          Output is a single JSON object on stdout:
 *            {"initialized":1,"feedCount":0,"dioFlipCount":0,
 *             "dioLastChannel":255,"feedResult":-1}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_Watchdog.h"

#include "harness_common.h"

/* ==================================================================
 * Dio_FlipChannel mock — counts toggles and records the last channel
 * ================================================================== */

static uint32_t mock_dio_flip_count;
static uint8_t  mock_dio_last_channel;

uint8 Dio_FlipChannel(uint8 ChannelId)
{
    mock_dio_flip_count++;
    mock_dio_last_channel = ChannelId;
    return (uint8)((mock_dio_flip_count & 1u) ? 1u : 0u);
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint8_t  skip_init;
    uint8_t  init_null;
    uint8_t  loop_complete;
    uint8_t  canary_ok;
    uint8_t  ram_ok;
    uint8_t  can_ok;
    uint32_t feed_count;
} Phase;

static int run_phase(const Phase* p, int* feed_result)
{
    uint32_t i;

    *feed_result = -1;

    for (i = 0u; i < p->feed_count; i++) {
        *feed_result = (int)Swc_Watchdog_Feed(p->loop_complete,
                                              p->canary_ok,
                                              p->ram_ok,
                                              p->can_ok);
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->loop_complete = 1u;
    p->canary_ok = 1u;
    p->ram_ok = 1u;
    p->can_ok = 1u;
    p->feed_count = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "skipInit") == 0)      p->skip_init = (uint8_t)val;
    else if (strcmp(key, "initNull") == 0) p->init_null = (uint8_t)val;
    else if (strcmp(key, "loopComplete") == 0) p->loop_complete = (uint8_t)val;
    else if (strcmp(key, "canaryOk") == 0) p->canary_ok = (uint8_t)val;
    else if (strcmp(key, "ramOk") == 0)    p->ram_ok = (uint8_t)val;
    else if (strcmp(key, "canOk") == 0)    p->can_ok = (uint8_t)val;
    else if (strcmp(key, "feedCount") == 0) p->feed_count = val;
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;
    uint8_t global_init_null = 0u;
    int feed_result = -1;

    static Swc_Watchdog_ConfigType wdg_config;

    /* Default WDI channel used by the unit tests / scheduler */
    wdg_config.wdiDioChannel = 6u;

    /* ---- parse all phases first (skipInit/initNull decided on phase[0]) ---- */
        {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guard in Feed). initNull
     * exercises the NULL-config guard in Init. */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    global_init_null = (phase_count == 0u) ? 0u : phases[0].init_null;
    if (global_skip_init == 0u) {
        Swc_Watchdog_Init(global_init_null != 0u ? NULL_PTR : &wdg_config);
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi], &feed_result) != 0) {
            return 2;
        }
    }

    printf("{\"initialized\":%u,\"feedCount\":%u,\"dioFlipCount\":%u,"
           "\"dioLastChannel\":%u,\"feedResult\":%d}\n",
           (unsigned)Swc_Watchdog_GetInitialized(),
           (unsigned)Swc_Watchdog_GetFeedCount(),
           (unsigned)mock_dio_flip_count,
           (unsigned)mock_dio_last_channel,
           feed_result);

    return 0;
}
