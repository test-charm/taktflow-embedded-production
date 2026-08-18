/**
 * @file    sc_plausibility_harness.c
 * @brief   Native test harness for sc_plausibility (torque-vs-current check)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_plausibility.c and drives its public
 *          APIs (Init / Check / IsFaulted / CreepGuard_Check / IsCreepFaulted)
 *          through a phase script read from stdin. Each phase is one line of
 *          whitespace-separated key=value tokens:
 *
 *            op=init
 *            op=check [torque=N] [current=N] [vehValid=N] [curValid=N]
 *                     [brakeFault=N] [repeats=N]
 *            op=creep [torque=N] [current=N] [vehValid=N] [curValid=N]
 *                     [repeats=N]
 *            op=drainGrace ticks=N
 *            op=lookup torque=N
 *            op=implausible expected=N actual=N
 *            skipInit=0|1
 *
 *          The harness injects the vehicle-state (SC_MB_IDX_VEHICLE_STATE,
 *          torque_pct at data[4]) and motor-current (SC_MB_IDX_MOTOR_CURRENT,
 *          mA little-endian at data[2:3]) CAN data into the mocked
 *          SC_CAN_GetMessage, mocks SC_Heartbeat_IsFzcBrakeFault, and mocks
 *          the GIO system-fault LED. UNIT_TEST hooks observe every internal
 *          counter/flag and the internal lookup/is_implausible statics.
 *
 *          Output is a single JSON object on stdout carrying a per-op
 *          `results[]` array plus a final `state` snapshot:
 *            {"results":[...],"state":{...}}
 *
 *          The harness is compiled with the production TMS570 logic — no
 *          PLATFORM_POSIX/HIL — so the strict 10-tick plausibility debounce,
 *          10-tick backup-cutoff, 2-cycle creep debounce, and 1500-tick
 *          startup grace apply (values from Sc_Cfg_Platform.h selected by
 *          include path; see test-design doc).
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_plausibility.h"
#include "Sc_Hw_Cfg.h"

#include "harness_common.h"

/* UNIT_TEST observation hooks (declared in sc_plausibility.h) */
extern uint8   SC_Plausibility_TestGetDebounce(void);
extern uint8   SC_Plausibility_TestGetBackupCutoffCounter(void);
extern uint16  SC_Plausibility_TestGetStartupGrace(void);
extern uint8   SC_Plausibility_TestGetCreepDebounce(void);
extern uint16  SC_Plausibility_TestLookupExpectedCurrent(uint8 torque_pct);
extern boolean SC_Plausibility_TestIsImplausible(uint16 expected_ma,
                                                 uint16 actual_ma);

/* ==================================================================
 * Mock GIO (system fault LED)
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

/* ==================================================================
 * Mock CAN message data
 * ================================================================== */

static uint8   mock_can_data[SC_MB_COUNT][SC_CAN_DLC];
static boolean mock_can_valid[SC_MB_COUNT];

boolean SC_CAN_GetMessage(uint8 mbIndex, uint8* data, uint8* dlc)
{
    uint8 i;
    if ((mbIndex >= SC_MB_COUNT) || (data == NULL_PTR) || (dlc == NULL_PTR)) {
        return FALSE;
    }
    if (mock_can_valid[mbIndex] == FALSE) {
        return FALSE;
    }
    for (i = 0u; i < SC_CAN_DLC; i++) {
        data[i] = mock_can_data[mbIndex][i];
    }
    *dlc = SC_CAN_DLC;
    return TRUE;
}

/* ==================================================================
 * Mock heartbeat FZC brake fault
 * ================================================================== */

static boolean mock_fzc_brake_fault;

boolean SC_Heartbeat_IsFzcBrakeFault(void)
{
    return mock_fzc_brake_fault;
}

static void set_can_data(uint32_t torque, uint32_t current,
                         uint32_t veh_valid, uint32_t cur_valid)
{
    memset(mock_can_data, 0, sizeof(mock_can_data));
    memset(mock_can_valid, 0, sizeof(mock_can_valid));
    mock_can_valid[SC_MB_IDX_VEHICLE_STATE] = (veh_valid != 0u) ? TRUE : FALSE;
    mock_can_valid[SC_MB_IDX_MOTOR_CURRENT] = (cur_valid != 0u) ? TRUE : FALSE;
    mock_can_data[SC_MB_IDX_VEHICLE_STATE][4] = (uint8)(torque & 0xFFu);
    mock_can_data[SC_MB_IDX_MOTOR_CURRENT][2] = (uint8)(current & 0xFFu);
    mock_can_data[SC_MB_IDX_MOTOR_CURRENT][3] = (uint8)((current >> 8u) & 0xFFu);
}

/* ==================================================================
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_CHECK,
    OP_CREEP,
    OP_DRAIN_GRACE,
    OP_LOOKUP,
    OP_IMPLAUSIBLE
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t torque;
    uint32_t current;
    uint32_t veh_valid;
    uint32_t cur_valid;
    uint32_t brake_fault;
    uint32_t repeats;
    uint32_t ticks;
    uint32_t expected;
    uint32_t actual;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)        return OP_INIT;
    if (strcmp(s, "check") == 0)       return OP_CHECK;
    if (strcmp(s, "creep") == 0)       return OP_CREEP;
    if (strcmp(s, "drainGrace") == 0)  return OP_DRAIN_GRACE;
    if (strcmp(s, "lookup") == 0)      return OP_LOOKUP;
    if (strcmp(s, "implausible") == 0) return OP_IMPLAUSIBLE;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->torque = 0u;
    p->current = 0u;
    p->veh_valid = 1u;
    p->cur_valid = 1u;
    p->brake_fault = 0u;
    p->repeats = 1u;
    p->ticks = 1u;
    p->expected = 0u;
    p->actual = 0u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)         p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "torque") == 0) p->torque = uval;
    else if (strcmp(key, "current") == 0) p->current = uval;
    else if (strcmp(key, "vehValid") == 0) p->veh_valid = uval;
    else if (strcmp(key, "curValid") == 0) p->cur_valid = uval;
    else if (strcmp(key, "brakeFault") == 0) p->brake_fault = uval;
    else if (strcmp(key, "repeats") == 0) p->repeats = uval;
    else if (strcmp(key, "ticks") == 0) p->ticks = uval;
    else if (strcmp(key, "expected") == 0) p->expected = uval;
    else if (strcmp(key, "actual") == 0) p->actual = uval;
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
    /* Exercise the public query APIs (SC_Plausibility_IsFaulted /
     * IsCreepFaulted) and every UNIT_TEST getter so the production
     * latch-read paths are covered as well as the hooks. */
    return (size_t)snprintf(
        buf, bufsize,
        "\"state\":{"
        "\"faulted\":%u,"
        "\"creepFaulted\":%u,"
        "\"debounce\":%u,"
        "\"backupCutoff\":%u,"
        "\"startupGrace\":%u,"
        "\"creepDebounce\":%u,"
        "\"ledSys\":%u}",
        (unsigned)(SC_Plausibility_IsFaulted() ? 1u : 0u),
        (unsigned)(SC_Plausibility_IsCreepFaulted() ? 1u : 0u),
        (unsigned)SC_Plausibility_TestGetDebounce(),
        (unsigned)SC_Plausibility_TestGetBackupCutoffCounter(),
        (unsigned)SC_Plausibility_TestGetStartupGrace(),
        (unsigned)SC_Plausibility_TestGetCreepDebounce(),
        (unsigned)gioGetBit(SC_PORT_LED_SYS, SC_PIN_LED_SYS));
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
    set_can_data(0u, 0u, 1u, 1u);
    mock_fzc_brake_fault = FALSE;

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
        SC_Plausibility_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        char state_buf[1024u];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_Plausibility_Init();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_CHECK: {
                uint32_t t;
                set_can_data(p->torque, p->current, p->veh_valid, p->cur_valid);
                mock_fzc_brake_fault = (p->brake_fault != 0u) ? TRUE : FALSE;
                for (t = 0u; t < p->repeats; t++) {
                    SC_Plausibility_Check();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"check\",\"torque\":%u,\"current\":%u,"
                             "\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->torque, (unsigned)p->current,
                             (unsigned)p->repeats, state_buf);
                break;
            }

            case OP_CREEP: {
                uint32_t t;
                set_can_data(p->torque, p->current, p->veh_valid, p->cur_valid);
                for (t = 0u; t < p->repeats; t++) {
                    SC_CreepGuard_Check();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"creep\",\"torque\":%u,\"current\":%u,"
                             "\"repeats\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->torque, (unsigned)p->current,
                             (unsigned)p->repeats, state_buf);
                break;
            }

            case OP_DRAIN_GRACE: {
                uint32_t t;
                for (t = 0u; t < p->ticks; t++) {
                    SC_Plausibility_Check();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"drainGrace\",\"ticks\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->ticks, state_buf);
                break;
            }

            case OP_LOOKUP: {
                uint16_t expected = SC_Plausibility_TestLookupExpectedCurrent(
                    (uint8)(p->torque & 0xFFu));
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"lookup\",\"torque\":%u,"
                             "\"expected\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->torque, (unsigned)expected, state_buf);
                break;
            }

            case OP_IMPLAUSIBLE: {
                boolean result = SC_Plausibility_TestIsImplausible(
                    (uint16)p->expected, (uint16)p->actual);
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"implausible\",\"expected\":%u,"
                             "\"actual\":%u,\"result\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->expected, (unsigned)p->actual,
                             (result == TRUE) ? 1u : 0u, state_buf);
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
