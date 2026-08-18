/**
 * @file    sc_state_harness.c
 * @brief   Native test harness for sc_state (SC runtime state machine)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_state.c and drives its public APIs
 *          (Init / Get / Transition) through a phase script read from stdin.
 *          Each phase is one line of whitespace-separated key=value tokens:
 *
 *            op=init
 *            op=transition newState=N
 *            op=setRaw state=N
 *            skipInit=0|1
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[...],"state":N}
 *
 *          Per-op results:
 *            init:       {"op":"init","state":N}
 *            transition: {"op":"transition","ret":0|1,"state":N}
 *            setRaw:     {"op":"setRaw","state":N}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_state.h"

#include "harness_common.h"

extern void SC_State_TestSetRaw(uint8 state);

enum {
    OP_INIT = 1,
    OP_TRANSITION,
    OP_SET_RAW
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t new_state;
    uint32_t set_raw_state;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)          return OP_INIT;
    if (strcmp(s, "transition") == 0)    return OP_TRANSITION;
    if (strcmp(s, "setRaw") == 0)        return OP_SET_RAW;
    return 0;
}

static void reset_phase(void* phase)
{
    (void)phase;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)          p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "newState") == 0) p->new_state = uval;
    else if (strcmp(key, "state") == 0)    p->set_raw_state = uval;
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

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count;
    size_t pi;
    char results_buf[HARNESS_MAX_PHASES * 256u];
    size_t rb = 0u;
    uint8_t global_skip_init;

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
        SC_State_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_State_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",\"state\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)SC_State_Get());
                break;

            case OP_TRANSITION: {
                boolean ret = SC_State_Transition((uint8)p->new_state);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"transition\",\"ret\":%u,\"state\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == TRUE) ? 1u : 0u,
                             (unsigned)SC_State_Get());
                break;
            }

            case OP_SET_RAW:
                SC_State_TestSetRaw((uint8)p->set_raw_state);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"setRaw\",\"state\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)SC_State_Get());
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

    printf("{\"results\":[%s],\"state\":%u}\n",
           results_buf,
           (unsigned)SC_State_Get());

    return 0;
}
