/**
 * @file    sc_relay_harness.c
 * @brief   Native test harness for sc_relay (SC kill relay GPIO control)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_relay.c and drives its public APIs
 *          (Init / Energize / DeEnergize / CheckTriggers / IsKilled /
 *          GetKillReason) through a phase script read from stdin. Each phase
 *          is one line of whitespace-separated key=value tokens:
 *
 *            op=init
 *            op=energize
 *            op=deEnergize
 *            op=checkTriggers [repeats=N]
 *            op=setMock [estop=N] [hb=N] [plaus=N] [creep=N] [e2e=N]
 *                        [selftest=N] [esm=N] [busoff=N] [busSilent=N]
 *            op=setReadback value=N
 *            skipInit=0|1
 *
 *          External module getters (SC_CAN_IsEStopActive / IsBusOff /
 *          IsBusSilent, SC_Heartbeat_IsAnyConfirmed, SC_Plausibility_IsFaulted /
 *          IsCreepFaulted, SC_E2E_IsAnyCriticalFailed, SC_SelfTest_IsHealthy,
 *          SC_ESM_IsErrorActive) are injected through `setMock`. The GIO
 *          port/pin used by the relay (SC_GIO_PORT_A / SC_PIN_RELAY) is
 *          mocked in-harness: gioSetBit records the output level and mirrors
 *          it as the readback by default, while `setReadback` overrides the
 *          readback value directly to drive the readback-mismatch branches.
 *
 *          Output is a single JSON object on stdout carrying a per-op
 *          `results[]` array plus a final `state` snapshot of every
 *          observable internal flag/counter:
 *            {"results":[...],"state":{...}}
 *
 *          The harness is compiled with the production TMS570 logic — no
 *          PLATFORM_POSIX/HIL — so DeEnergize latches the kill, the bus-off /
 *          bus-silence / readback triggers are active, and IsKilled reports
 *          the latch directly (SWR-SC-010/011/012).
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_relay.h"
#include "Sc_Hw_Cfg.h"

#include "harness_common.h"

/* UNIT_TEST observation hooks (declared in sc_relay.h) */
extern boolean SC_Relay_TestGetKilled(void);
extern boolean SC_Relay_TestGetCommanded(void);
extern uint8   SC_Relay_TestGetReadbackMismatchCount(void);

/* ==================================================================
 * Mock GIO (relay output + readback)
 * ================================================================== */

#define MOCK_GIO_MAX_PORTS  2u
#define MOCK_GIO_MAX_PINS   8u

static uint8 mock_gio_out[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];
static uint8 mock_gio_in[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];

void gioSetBit(uint8 port, uint8 pin, uint8 value)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        mock_gio_out[port][pin] = value;
        /* By default, readback matches output (healthy relay) */
        mock_gio_in[port][pin]  = value;
    }
}

uint8 gioGetBit(uint8 port, uint8 pin)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        return mock_gio_in[port][pin];
    }
    return 0u;
}

/* ==================================================================
 * Mock module state getters
 * ================================================================== */

static boolean mock_estop_active;
static boolean mock_hb_any_confirmed;
static boolean mock_plaus_faulted;
static boolean mock_creep_faulted;
static boolean mock_e2e_any_critical_failed;
static boolean mock_selftest_healthy;
static boolean mock_esm_error;
static boolean mock_can_bus_off;
static boolean mock_can_bus_silent;

boolean SC_CAN_IsEStopActive(void)
{
    return mock_estop_active;
}

boolean SC_Heartbeat_IsAnyConfirmed(void)
{
    return mock_hb_any_confirmed;
}

boolean SC_Plausibility_IsFaulted(void)
{
    return mock_plaus_faulted;
}

boolean SC_Plausibility_IsCreepFaulted(void)
{
    return mock_creep_faulted;
}

boolean SC_E2E_IsAnyCriticalFailed(void)
{
    return mock_e2e_any_critical_failed;
}

boolean SC_SelfTest_IsHealthy(void)
{
    return mock_selftest_healthy;
}

boolean SC_ESM_IsErrorActive(void)
{
    return mock_esm_error;
}

boolean SC_CAN_IsBusOff(void)
{
    return mock_can_bus_off;
}

boolean SC_CAN_IsBusSilent(void)
{
    return mock_can_bus_silent;
}

static void reset_mocks(void)
{
    mock_estop_active            = FALSE;
    mock_hb_any_confirmed        = FALSE;
    mock_plaus_faulted           = FALSE;
    mock_creep_faulted           = FALSE;
    mock_e2e_any_critical_failed = FALSE;
    mock_selftest_healthy        = TRUE;
    mock_esm_error               = FALSE;
    mock_can_bus_off             = FALSE;
    mock_can_bus_silent          = FALSE;
}

/* ==================================================================
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_ENERGIZE,
    OP_DEENERGIZE,
    OP_CHECK_TRIGGERS,
    OP_SET_MOCK,
    OP_SET_READBACK
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t repeats;
    uint32_t estop;
    uint32_t hb;
    uint32_t plaus;
    uint32_t creep;
    uint32_t e2e;
    uint32_t selftest;
    uint32_t esm;
    uint32_t busoff;
    uint32_t bus_silent;
    uint32_t readback_value;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)          return OP_INIT;
    if (strcmp(s, "energize") == 0)      return OP_ENERGIZE;
    if (strcmp(s, "deEnergize") == 0)    return OP_DEENERGIZE;
    if (strcmp(s, "checkTriggers") == 0) return OP_CHECK_TRIGGERS;
    if (strcmp(s, "setMock") == 0)       return OP_SET_MOCK;
    if (strcmp(s, "setReadback") == 0)   return OP_SET_READBACK;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->repeats = 1u;
    p->selftest = 1u;  /* default: healthy */
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)           p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "repeats") == 0) p->repeats = uval;
    else if (strcmp(key, "estop") == 0)   p->estop = uval;
    else if (strcmp(key, "hb") == 0)      p->hb = uval;
    else if (strcmp(key, "plaus") == 0)   p->plaus = uval;
    else if (strcmp(key, "creep") == 0)   p->creep = uval;
    else if (strcmp(key, "e2e") == 0)     p->e2e = uval;
    else if (strcmp(key, "selftest") == 0) p->selftest = uval;
    else if (strcmp(key, "esm") == 0)     p->esm = uval;
    else if (strcmp(key, "busoff") == 0)  p->busoff = uval;
    else if (strcmp(key, "busSilent") == 0) p->bus_silent = uval;
    else if (strcmp(key, "value") == 0)   p->readback_value = uval;
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
    /* Exercise the public query APIs (SC_Relay_IsKilled / GetKillReason)
     * so the production IsKilled latch-read path is covered as well as the
     * UNIT_TEST observation hooks. */
    return (size_t)snprintf(
        buf, bufsize,
        "\"state\":{"
        "\"commanded\":%u,"
        "\"killed\":%u,"
        "\"killedApi\":%u,"
        "\"mismatchCount\":%u,"
        "\"reason\":%u,"
        "\"gioRelay\":%u}",
        (unsigned)(SC_Relay_TestGetCommanded() ? 1u : 0u),
        (unsigned)(SC_Relay_TestGetKilled() ? 1u : 0u),
        (unsigned)(SC_Relay_IsKilled() ? 1u : 0u),
        (unsigned)SC_Relay_TestGetReadbackMismatchCount(),
        (unsigned)SC_Relay_GetKillReason(),
        (unsigned)gioGetBit(SC_GIO_PORT_A, SC_PIN_RELAY));
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

    memset(mock_gio_out, 0, sizeof(mock_gio_out));
    memset(mock_gio_in, 0, sizeof(mock_gio_in));
    reset_mocks();

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
        SC_Relay_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        char state_buf[1024u];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_Relay_Init();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_ENERGIZE:
                SC_Relay_Energize();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"energize\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_DEENERGIZE:
                SC_Relay_DeEnergize();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"deEnergize\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_CHECK_TRIGGERS: {
                uint32_t t;
                for (t = 0u; t < p->repeats; t++) {
                    SC_Relay_CheckTriggers();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"checkTriggers\",\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->repeats, state_buf);
                break;
            }

            case OP_SET_MOCK:
                mock_estop_active            = (p->estop != 0u) ? TRUE : FALSE;
                mock_hb_any_confirmed        = (p->hb != 0u) ? TRUE : FALSE;
                mock_plaus_faulted           = (p->plaus != 0u) ? TRUE : FALSE;
                mock_creep_faulted           = (p->creep != 0u) ? TRUE : FALSE;
                mock_e2e_any_critical_failed = (p->e2e != 0u) ? TRUE : FALSE;
                mock_selftest_healthy        = (p->selftest != 0u) ? TRUE : FALSE;
                mock_esm_error               = (p->esm != 0u) ? TRUE : FALSE;
                mock_can_bus_off             = (p->busoff != 0u) ? TRUE : FALSE;
                mock_can_bus_silent          = (p->bus_silent != 0u) ? TRUE : FALSE;
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"setMock\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_SET_READBACK:
                mock_gio_in[SC_GIO_PORT_A][SC_PIN_RELAY] = (uint8)(p->readback_value & 0x01u);
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"setReadback\",\"value\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)(p->readback_value & 0x01u), state_buf);
                break;

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
