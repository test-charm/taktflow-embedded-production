/**
 * @file    sc_heartbeat_harness.c
 * @brief   Native test harness for sc_heartbeat (SC heartbeat monitoring)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_heartbeat.c and drives its public APIs
 *          (Init / NotifyRx / Monitor / ValidateContent / IsTimedOut /
 *          IsAnyConfirmed / IsContentFault / IsFzcBrakeFault) through a phase
 *          script read from stdin. Each phase is one line of whitespace-
 *          separated key=value tokens:
 *
 *            op=init
 *            op=monitor ticks=N [notifyA=N] [notifyB=N]
 *            op=notifyRx ecu=N [repeats=N]
 *            op=validate ecu=N payload3=N [repeats=N]
 *            skipInit=0|1
 *
 *          `monitor` may additionally specify `notifyA`/`notifyB` (SC_ECU_*
 *          indices, default 255 = none); when set, the harness calls
 *          SC_Heartbeat_NotifyRx for that ECU once per tick before each
 *          Monitor call, simulating a heartbeat received every cycle for the
 *          independent-ECU timeout scenario.
 *
 *          Output is a single JSON object on stdout carrying a per-op
 *          `results[]` array plus a final `state` snapshot of every
 *          observable internal counter/flag:
 *            {"results":[...],"state":{...}}
 *
 *          `gioSetBit`/`gioGetBit` are mocked in-harness to record LED pin
 *          levels; the observed LED state is reported per ECU.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_heartbeat.h"
#include "Sc_Hw_Cfg.h"

#include "harness_common.h"

/* UNIT_TEST observation hooks (declared in sc_heartbeat.h) */
extern uint16  SC_Heartbeat_TestGetCounter(uint8 ecuIndex);
extern uint16  SC_Heartbeat_TestGetStartupGrace(void);
extern boolean SC_Heartbeat_TestGetTimedOut(uint8 ecuIndex);
extern boolean SC_Heartbeat_TestGetConfirmed(uint8 ecuIndex);
extern uint8   SC_Heartbeat_TestGetRecoveryCount(uint8 ecuIndex);
extern uint8   SC_Heartbeat_TestGetConfirmCounter(uint8 ecuIndex);
extern uint8   SC_Heartbeat_TestGetStuckDegradedCnt(uint8 ecuIndex);
extern uint8   SC_Heartbeat_TestGetFaultEscalateCnt(uint8 ecuIndex);
extern boolean SC_Heartbeat_TestGetContentFault(uint8 ecuIndex);
extern uint8   SC_Heartbeat_TestGetLastFaultStatus(uint8 ecuIndex);

/* ==================================================================
 * Mock GIO (LED outputs)
 * ================================================================== */

#define MOCK_GIO_MAX_PORTS  2u
#define MOCK_GIO_MAX_PINS   8u

static uint8 mock_gio[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];

void gioSetBit(uint8 port, uint8 pin, uint8 value)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        mock_gio[port][pin] = value;
    }
}

uint8 gioGetBit(uint8 port, uint8 pin)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        return mock_gio[port][pin];
    }
    return 0u;
}

static uint8 led_for_ecu(uint8 ecu)
{
    uint8 led = 0u;
    switch (ecu) {
        case SC_ECU_CVC:
            led = gioGetBit(SC_PORT_LED_CVC, SC_PIN_LED_CVC);
            break;
        case SC_ECU_FZC:
            led = gioGetBit(SC_PORT_LED_FZC, SC_PIN_LED_FZC);
            break;
        case SC_ECU_RZC:
            led = gioGetBit(SC_PORT_LED_RZC, SC_PIN_LED_RZC);
            break;
        default:
            break;
    }
    return led;
}

/* ==================================================================
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_MONITOR,
    OP_NOTIFY_RX,
    OP_VALIDATE
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t ticks;
    uint32_t ecu;
    uint32_t repeats;
    uint32_t payload3;
    uint32_t notify_a;
    uint32_t notify_b;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)          return OP_INIT;
    if (strcmp(s, "monitor") == 0)       return OP_MONITOR;
    if (strcmp(s, "notifyRx") == 0)      return OP_NOTIFY_RX;
    if (strcmp(s, "validate") == 0)      return OP_VALIDATE;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->ticks = 1u;
    p->repeats = 1u;
    p->notify_a = 255u;
    p->notify_b = 255u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)          p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "ticks") == 0)  p->ticks = uval;
    else if (strcmp(key, "ecu") == 0)    p->ecu = uval;
    else if (strcmp(key, "repeats") == 0) p->repeats = uval;
    else if (strcmp(key, "payload3") == 0) p->payload3 = uval;
    else if (strcmp(key, "notifyA") == 0) p->notify_a = uval;
    else if (strcmp(key, "notifyB") == 0) p->notify_b = uval;
    return 0;
}

static int finish_phase(void* phase, size_t index)
{
    Phase* p = (Phase*)phase;
    if (p->op == 0u) {
        fprintf(stderr, "missing/unknown op in phase %zu\n", index);
        return 1;
    }
    return 0;
}

/* ==================================================================
 * State snapshot formatting
 * ================================================================== */

static size_t format_state(char* buf, size_t bufsize)
{
    /* Exercise the public query APIs (SC_Heartbeat_IsTimedOut /
     * IsContentFault) and every UNIT_TEST getter's out-of-range guard
     * branch (index SC_ECU_COUNT) so all defensive lines are covered. */
    uint32_t guard_probe = 0u;
    uint8_t api_to[SC_ECU_COUNT];
    uint8_t api_cf[SC_ECU_COUNT];
    uint8_t e;

    for (e = 0u; e < SC_ECU_COUNT; e++) {
        api_to[e] = SC_Heartbeat_IsTimedOut(e) ? 1u : 0u;
        api_cf[e] = SC_Heartbeat_IsContentFault(e) ? 1u : 0u;
    }
    guard_probe += SC_Heartbeat_TestGetCounter(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetTimedOut(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetConfirmed(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetRecoveryCount(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetConfirmCounter(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetStuckDegradedCnt(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetFaultEscalateCnt(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetContentFault(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_TestGetLastFaultStatus(SC_ECU_COUNT);
    /* Public query APIs with an out-of-range index (guard branches) */
    guard_probe += SC_Heartbeat_IsTimedOut(SC_ECU_COUNT);
    guard_probe += SC_Heartbeat_IsContentFault(SC_ECU_COUNT);
    /* ValidateContent NULL-payload guard branch */
    SC_Heartbeat_ValidateContent(SC_ECU_CVC, NULL_PTR);

    return (size_t)snprintf(
        buf, bufsize,
        "\"state\":{"
        "\"counters\":[%u,%u,%u],"
        "\"timedOut\":[%u,%u,%u],"
        "\"confirmed\":[%u,%u,%u],"
        "\"recoveryCounts\":[%u,%u,%u],"
        "\"confirmCounters\":[%u,%u,%u],"
        "\"stuckDegraded\":[%u,%u,%u],"
        "\"faultEscalate\":[%u,%u,%u],"
        "\"contentFault\":[%u,%u,%u],"
        "\"lastFaultStatus\":[%u,%u,%u],"
        "\"leds\":[%u,%u,%u],"
        "\"isTimedOutApi\":[%u,%u,%u],"
        "\"isContentFaultApi\":[%u,%u,%u],"
        "\"guardProbe\":%u,"
        "\"startupGrace\":%u,"
        "\"anyConfirmed\":%u,"
        "\"fzcBrakeFault\":%u}",
        (unsigned)SC_Heartbeat_TestGetCounter(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetCounter(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetCounter(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetTimedOut(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetTimedOut(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetTimedOut(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetConfirmed(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetConfirmed(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetConfirmed(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetRecoveryCount(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetRecoveryCount(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetRecoveryCount(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetConfirmCounter(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetConfirmCounter(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetConfirmCounter(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetStuckDegradedCnt(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetStuckDegradedCnt(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetStuckDegradedCnt(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetFaultEscalateCnt(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetFaultEscalateCnt(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetFaultEscalateCnt(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetContentFault(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetContentFault(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetContentFault(SC_ECU_RZC),
        (unsigned)SC_Heartbeat_TestGetLastFaultStatus(SC_ECU_CVC),
        (unsigned)SC_Heartbeat_TestGetLastFaultStatus(SC_ECU_FZC),
        (unsigned)SC_Heartbeat_TestGetLastFaultStatus(SC_ECU_RZC),
        (unsigned)led_for_ecu(SC_ECU_CVC),
        (unsigned)led_for_ecu(SC_ECU_FZC),
        (unsigned)led_for_ecu(SC_ECU_RZC),
        (unsigned)api_to[0], (unsigned)api_to[1], (unsigned)api_to[2],
        (unsigned)api_cf[0], (unsigned)api_cf[1], (unsigned)api_cf[2],
        (unsigned)guard_probe,
        (unsigned)SC_Heartbeat_TestGetStartupGrace(),
        (unsigned)SC_Heartbeat_IsAnyConfirmed() ? 1u : 0u,
        (unsigned)SC_Heartbeat_IsFzcBrakeFault() ? 1u : 0u);
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count;
    size_t pi;
    char results_buf[HARNESS_MAX_PHASES * 1024u];
    size_t rb = 0u;
    uint8_t global_skip_init;

    memset(mock_gio, 0, sizeof(mock_gio));

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, finish_phase);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        SC_Heartbeat_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        char state_buf[1024u];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_Heartbeat_Init();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_MONITOR: {
                uint32_t t;
                for (t = 0u; t < p->ticks; t++) {
                    if (p->notify_a < SC_ECU_COUNT) {
                        SC_Heartbeat_NotifyRx((uint8)p->notify_a);
                    }
                    if (p->notify_b < SC_ECU_COUNT) {
                        SC_Heartbeat_NotifyRx((uint8)p->notify_b);
                    }
                    SC_Heartbeat_Monitor();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"monitor\",\"ticks\":%u,%s}",
                             (rb > 0u) ? "," : "", (unsigned)p->ticks, state_buf);
                break;
            }

            case OP_NOTIFY_RX: {
                uint32_t r;
                for (r = 0u; r < p->repeats; r++) {
                    SC_Heartbeat_NotifyRx((uint8)p->ecu);
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"notifyRx\",\"ecu\":%u,\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->ecu, (unsigned)p->repeats, state_buf);
                break;
            }

            case OP_VALIDATE: {
                uint8 payload[4];
                uint32_t r;
                memset(payload, 0, sizeof(payload));
                payload[3] = (uint8)p->payload3;
                for (r = 0u; r < p->repeats; r++) {
                    SC_Heartbeat_ValidateContent((uint8)p->ecu, payload);
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"validate\",\"ecu\":%u,\"payload3\":%u,"
                             "\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->ecu, (unsigned)p->payload3,
                             (unsigned)p->repeats, state_buf);
                break;
            }

            default:
                fprintf(stderr, "unhandled op\n");
                return 2;
        }

        if (n < 0 || (size_t)n >= sizeof(results_buf) - rb) {
            fprintf(stderr, "results buffer overflow\n");
            return 2;
        }
        rb += (size_t)n;
    }

    {
        char state_buf[1024u];
        format_state(state_buf, sizeof(state_buf));
        printf("{\"results\":[%s],%s}\n", results_buf, state_buf);
    }

    return 0;
}
