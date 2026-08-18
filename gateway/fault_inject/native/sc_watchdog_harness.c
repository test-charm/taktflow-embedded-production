/**
 * @file    sc_watchdog_harness.c
 * @brief   Native test harness for sc_watchdog (SC external watchdog feed)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_watchdog.c and drives its public APIs
 *          (SC_Watchdog_Init / SC_Watchdog_Feed) through a phase script read
 *          from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens:
 *
 *            op=init
 *            op=feed [ok=0|1] [repeats=N]
 *
 *          The TPS3823 WDI GIO pin (SC_GIO_PORT_A / SC_PIN_WDI) is mocked
 *          in-harness: gioSetBit records the output level (and mirrors it as
 *          the readback) while counting every WDI write. Output is a single
 *          JSON object on stdout carrying a per-op `results[]` array plus a
 *          final `state` snapshot:
 *            {"results":[...],"state":{"wdiPin":N,"wdiWriteCount":N}}
 *
 *          The harness is compiled with the production TMS570 logic — no
 *          PLATFORM_POSIX/HIL — so the feed gate semantics are exactly the
 *          production ones (SWR-SC-022): Feed toggles WDI only when
 *          allChecksOk==TRUE; otherwise the watchdog starves.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_watchdog.h"
#include "Sc_Hw_Cfg.h"

#include "harness_common.h"

/* ==================================================================
 * Mock GIO (WDI output + readback)
 * ================================================================== */

#define MOCK_GIO_MAX_PORTS  2u
#define MOCK_GIO_MAX_PINS   8u

static uint8 mock_gio_out[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];
static uint8 mock_gio_in[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];
static uint32 wdi_write_count;

void gioSetBit(uint8 port, uint8 pin, uint8 value)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        mock_gio_out[port][pin] = value;
        /* By default, readback matches output */
        mock_gio_in[port][pin]  = value;
        if (pin == SC_PIN_WDI) {
            wdi_write_count++;
        }
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
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_FEED
};

typedef struct {
    uint8_t  op;
    uint32_t ok;
    uint32_t repeats;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0) return OP_INIT;
    if (strcmp(s, "feed") == 0) return OP_FEED;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->ok = 1u;      /* default: all checks OK (feed toggles) */
    p->repeats = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)        p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "ok") == 0)   p->ok = uval;
    else if (strcmp(key, "repeats") == 0) p->repeats = uval;
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
    return (size_t)snprintf(
        buf, bufsize,
        "\"state\":{"
        "\"wdiPin\":%u,"
        "\"wdiWriteCount\":%u}",
        (unsigned)gioGetBit(SC_GIO_PORT_A, SC_PIN_WDI),
        (unsigned)wdi_write_count);
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

    memset(mock_gio_out, 0, sizeof(mock_gio_out));
    memset(mock_gio_in, 0, sizeof(mock_gio_in));
    wdi_write_count = 0u;

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, finish_phase);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    /* Harness startup runs SC_Watchdog_Init exactly like a real boot */
    SC_Watchdog_Init();

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        char state_buf[1024u];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_Watchdog_Init();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_FEED: {
                uint32_t t;
                for (t = 0u; t < p->repeats; t++) {
                    SC_Watchdog_Feed((p->ok != 0u) ? TRUE : FALSE);
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"feed\",\"ok\":%u,\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)((p->ok != 0u) ? 1u : 0u),
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
