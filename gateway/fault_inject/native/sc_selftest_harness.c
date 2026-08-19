/**
 * @file    sc_selftest_harness.c
 * @brief   Native test harness for sc_selftest (SC startup + runtime self-test)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_selftest.c and drives its public APIs
 *          (SC_SelfTest_Init / SC_SelfTest_Startup / SC_SelfTest_Runtime /
 *          SC_SelfTest_StackCanaryOk / SC_SelfTest_IsHealthy) through a phase
 *          script read from stdin. Each phase is one line of whitespace-
 *          separated key=value tokens:
 *
 *            op=init
 *            op=startup  [b1..b7=0|1]
 *            op=runtime  [repeats=N] [flashIncr=0|1] [dcanErr=0|1]
 *                        [readback=0|1] [corruptRam=0|1]
 *            op=canary   [corruptCanary=0|1]
 *
 *          The seven startup hardware diagnostics (lockstep BIST, RAM PBIST,
 *          flash CRC-32, DCAN loopback, GPIO readback, lamp test, watchdog
 *          test) and the two runtime hardware checks (flash CRC incremental,
 *          DCAN error status) are mocked in-harness; each mock also counts
 *          its invocations so a failing step provably blocks the later steps.
 *          The GIO relay pin readback (runtime step 3) is pinned by `readback`.
 *
 *          UNIT_TEST hooks observe the internal runtime tick and the
 *          startup/runtime health flags, and inject canary / RAM-pattern
 *          corruption to drive the failure branches of SC_SelfTest_Startup /
 *          SC_SelfTest_Runtime.
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[...],"state":{...}}
 *
 *          Per-op results:
 *            init:    {"op":"init","canaryOk":N,"healthy":N,"tick":N}
 *            startup: {"op":"startup","result":N,"healthy":N,
 *                      "startupPassed":N,"lockstepCalls":N,...,"watchdogCalls":N}
 *            runtime: {"op":"runtime","repeats":N,"tick":N,"healthy":N,
 *                      "startupPassed":N,"runtimeHealthy":N}
 *            canary:  {"op":"canary","canaryOk":N}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_selftest.h"
#include "Sc_Hw_Cfg.h"
#include "sc_gio.h"

#include "harness_common.h"

/* ==================================================================
 * Hardware check mocks — return TRUE/FALSE as pinned by the phase
 * ================================================================== */

static uint8_t mock_check_lockstep = 1u;
static uint8_t mock_check_ram_pbist = 1u;
static uint8_t mock_check_flash_crc = 1u;
static uint8_t mock_check_dcan_loopback = 1u;
static uint8_t mock_check_gpio_readback = 1u;
static uint8_t mock_check_lamp = 1u;
static uint8_t mock_check_watchdog = 1u;
static uint8_t mock_check_flash_incr = 1u;
static uint8_t mock_check_dcan_err = 1u;

static uint32_t calls_lockstep = 0u;
static uint32_t calls_ram_pbist = 0u;
static uint32_t calls_flash_crc = 0u;
static uint32_t calls_dcan_loopback = 0u;
static uint32_t calls_gpio_readback = 0u;
static uint32_t calls_lamp = 0u;
static uint32_t calls_watchdog = 0u;
static uint32_t calls_flash_incr = 0u;
static uint32_t calls_dcan_err = 0u;

boolean hw_lockstep_bist(void)
{
    calls_lockstep++;
    return mock_check_lockstep ? TRUE : FALSE;
}

boolean hw_ram_pbist(void)
{
    calls_ram_pbist++;
    return mock_check_ram_pbist ? TRUE : FALSE;
}

boolean hw_flash_crc_check(void)
{
    calls_flash_crc++;
    return mock_check_flash_crc ? TRUE : FALSE;
}

boolean hw_dcan_loopback_test(void)
{
    calls_dcan_loopback++;
    return mock_check_dcan_loopback ? TRUE : FALSE;
}

boolean hw_gpio_readback_test(void)
{
    calls_gpio_readback++;
    return mock_check_gpio_readback ? TRUE : FALSE;
}

boolean hw_lamp_test(void)
{
    calls_lamp++;
    return mock_check_lamp ? TRUE : FALSE;
}

boolean hw_watchdog_test(void)
{
    calls_watchdog++;
    return mock_check_watchdog ? TRUE : FALSE;
}

boolean hw_flash_crc_incremental(void)
{
    calls_flash_incr++;
    return mock_check_flash_incr ? TRUE : FALSE;
}

boolean hw_dcan_error_check(void)
{
    calls_dcan_err++;
    return mock_check_dcan_err ? TRUE : FALSE;
}

/* ==================================================================
 * Mock GIO (relay pin readback for runtime step 3)
 * ================================================================== */

#define MOCK_GIO_MAX_PORTS  2u
#define MOCK_GIO_MAX_PINS   8u

static uint8 mock_gio_out[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];
static uint8 mock_gio_in[MOCK_GIO_MAX_PORTS][MOCK_GIO_MAX_PINS];

void gioSetBit(uint8 port, uint8 pin, uint8 value)
{
    if ((port < MOCK_GIO_MAX_PORTS) && (pin < MOCK_GIO_MAX_PINS)) {
        mock_gio_out[port][pin] = value;
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
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_STARTUP,
    OP_RUNTIME,
    OP_CANARY
};

typedef struct {
    uint8_t  op;
    uint8_t  b1;
    uint8_t  b2;
    uint8_t  b3;
    uint8_t  b4;
    uint8_t  b5;
    uint8_t  b6;
    uint8_t  b7;
    uint8_t  flash_incr;
    uint8_t  dcan_err;
    uint8_t  readback;
    uint8_t  corrupt_canary;
    uint8_t  corrupt_ram;
    uint32_t repeats;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)     return OP_INIT;
    if (strcmp(s, "startup") == 0)  return OP_STARTUP;
    if (strcmp(s, "runtime") == 0)  return OP_RUNTIME;
    if (strcmp(s, "canary") == 0)   return OP_CANARY;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->b1 = 1u;
    p->b2 = 1u;
    p->b3 = 1u;
    p->b4 = 1u;
    p->b5 = 1u;
    p->b6 = 1u;
    p->b7 = 1u;
    p->flash_incr = 1u;
    p->dcan_err = 1u;
    p->readback = 0u;
    p->corrupt_canary = 0u;
    p->corrupt_ram = 0u;
    p->repeats = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)          p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "b1") == 0)     p->b1 = (uint8_t)uval;
    else if (strcmp(key, "b2") == 0)     p->b2 = (uint8_t)uval;
    else if (strcmp(key, "b3") == 0)     p->b3 = (uint8_t)uval;
    else if (strcmp(key, "b4") == 0)     p->b4 = (uint8_t)uval;
    else if (strcmp(key, "b5") == 0)     p->b5 = (uint8_t)uval;
    else if (strcmp(key, "b6") == 0)     p->b6 = (uint8_t)uval;
    else if (strcmp(key, "b7") == 0)     p->b7 = (uint8_t)uval;
    else if (strcmp(key, "flashIncr") == 0) p->flash_incr = (uint8_t)uval;
    else if (strcmp(key, "dcanErr") == 0)   p->dcan_err = (uint8_t)uval;
    else if (strcmp(key, "readback") == 0)  p->readback = (uint8_t)uval;
    else if (strcmp(key, "corruptCanary") == 0) p->corrupt_canary = (uint8_t)uval;
    else if (strcmp(key, "corruptRam") == 0)    p->corrupt_ram = (uint8_t)uval;
    else if (strcmp(key, "repeats") == 0)       p->repeats = uval;
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
 * main
 * ================================================================== */

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count;
    size_t pi;
    char results_buf[HARNESS_MAX_PHASES * 512u];
    size_t rb = 0u;

    memset(mock_gio_out, 0, sizeof(mock_gio_out));
    memset(mock_gio_in, 0, sizeof(mock_gio_in));

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, finish_phase);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    /* Harness startup runs SC_SelfTest_Init exactly like a real boot */
    SC_SelfTest_Init();

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_SelfTest_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",\"canaryOk\":%u,\"healthy\":%u,\"tick\":%u}",
                             (rb > 0u) ? "," : "",
                             (SC_SelfTest_StackCanaryOk() == TRUE) ? 1u : 0u,
                             (SC_SelfTest_IsHealthy() == TRUE) ? 1u : 0u,
                             (unsigned)SC_SelfTest_TestGetRuntimeTick());
                break;

            case OP_STARTUP: {
                uint8 result;
                mock_check_lockstep = p->b1;
                mock_check_ram_pbist = p->b2;
                mock_check_flash_crc = p->b3;
                mock_check_dcan_loopback = p->b4;
                mock_check_gpio_readback = p->b5;
                mock_check_lamp = p->b6;
                mock_check_watchdog = p->b7;
                result = SC_SelfTest_Startup();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"startup\",\"result\":%u,\"healthy\":%u,"
                             "\"startupPassed\":%u,"
                             "\"lockstepCalls\":%u,\"ramPbistCalls\":%u,"
                             "\"flashCrcCalls\":%u,\"dcanLoopbackCalls\":%u,"
                             "\"gpioReadbackCalls\":%u,\"lampCalls\":%u,"
                             "\"watchdogCalls\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)result,
                             (SC_SelfTest_IsHealthy() == TRUE) ? 1u : 0u,
                             (SC_SelfTest_TestGetStartupPassed() == TRUE) ? 1u : 0u,
                             (unsigned)calls_lockstep,
                             (unsigned)calls_ram_pbist,
                             (unsigned)calls_flash_crc,
                             (unsigned)calls_dcan_loopback,
                             (unsigned)calls_gpio_readback,
                             (unsigned)calls_lamp,
                             (unsigned)calls_watchdog);
                break;
            }

            case OP_RUNTIME: {
                uint32_t t;
                mock_check_flash_incr = p->flash_incr;
                mock_check_dcan_err = p->dcan_err;
                mock_gio_in[SC_GIO_PORT_A][SC_PIN_RELAY] = p->readback;
                if (p->corrupt_ram != 0u) {
                    SC_SelfTest_TestCorruptRam();
                }
                for (t = 0u; t < p->repeats; t++) {
                    SC_SelfTest_Runtime();
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"runtime\",\"repeats\":%u,\"tick\":%u,"
                             "\"healthy\":%u,\"startupPassed\":%u,"
                             "\"runtimeHealthy\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->repeats,
                             (unsigned)SC_SelfTest_TestGetRuntimeTick(),
                             (SC_SelfTest_IsHealthy() == TRUE) ? 1u : 0u,
                             (SC_SelfTest_TestGetStartupPassed() == TRUE) ? 1u : 0u,
                             (SC_SelfTest_TestGetRuntimeHealthy() == TRUE) ? 1u : 0u);
                break;
            }

            case OP_CANARY:
                if (p->corrupt_canary != 0u) {
                    SC_SelfTest_TestCorruptCanary();
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"canary\",\"canaryOk\":%u}",
                             (rb > 0u) ? "," : "",
                             (SC_SelfTest_StackCanaryOk() == TRUE) ? 1u : 0u);
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

    printf("{\"results\":[%s],\"state\":{"
           "\"canaryOk\":%u,\"healthy\":%u,\"startupPassed\":%u,"
           "\"runtimeHealthy\":%u,\"tick\":%u,"
           "\"lockstepCalls\":%u,\"ramPbistCalls\":%u,"
           "\"flashCrcCalls\":%u,\"dcanLoopbackCalls\":%u,"
           "\"gpioReadbackCalls\":%u,\"lampCalls\":%u,"
           "\"watchdogCalls\":%u,\"flashIncrCalls\":%u,\"dcanErrCalls\":%u}}\n",
           results_buf,
           (SC_SelfTest_StackCanaryOk() == TRUE) ? 1u : 0u,
           (SC_SelfTest_IsHealthy() == TRUE) ? 1u : 0u,
           (SC_SelfTest_TestGetStartupPassed() == TRUE) ? 1u : 0u,
           (SC_SelfTest_TestGetRuntimeHealthy() == TRUE) ? 1u : 0u,
           (unsigned)SC_SelfTest_TestGetRuntimeTick(),
           (unsigned)calls_lockstep,
           (unsigned)calls_ram_pbist,
           (unsigned)calls_flash_crc,
           (unsigned)calls_dcan_loopback,
           (unsigned)calls_gpio_readback,
           (unsigned)calls_lamp,
           (unsigned)calls_watchdog,
           (unsigned)calls_flash_incr,
           (unsigned)calls_dcan_err);

    return 0;
}
