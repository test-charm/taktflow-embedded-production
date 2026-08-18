/**
 * @file    sc_e2e_harness.c
 * @brief   Native test harness for sc_e2e (SC E2E CRC-8 / alive validation)
 * @date    2026-08-18
 *
 * @details Links the REAL production sc_e2e.c and drives its public APIs
 *          (Init / Check / IsMsgFailed / IsAnyCriticalFailed /
 *          ComputeCRC8) through a phase script read from stdin. Each phase
 *          is one line of whitespace-separated key=value tokens:
 *
 *            op=init
 *            op=check [dataId=N] [msgIndex=N] [dlc=N] [alive=N]
 *                     [crcCorrupt=N] [dataIdCorrupt=N] [payloadCorrupt=N]
 *                     [nullData=N]
 *            op=drainGrace ticks=N
 *            op=crc8 [len=N] [nullData=N]         (UNIT_TEST sc_crc8 hook)
 *            op=compute [len=N] [nullData=N]      (public SC_E2E_ComputeCRC8)
 *            skipInit=0|1
 *
 *          `check` builds a byte-0 [alive:4|dataId:4] + CRC-8 (poly 0x1D,
 *          init 0xFF, XOR-out 0xFF) protected CAN message using a fixed
 *          payload pattern (byte i = 0x10 + i) and applies the requested
 *          corruptions before calling SC_E2E_Check:
 *            crcCorrupt      flip the CRC byte (byte 1)
 *            dataIdCorrupt   force byte-0 lower nibble to differ from dataId
 *            payloadCorrupt  flip payload byte data[4]
 *            nullData        pass NULL_PTR instead of the data buffer
 *
 *          Output is a single JSON object on stdout carrying a per-op
 *          `results[]` array plus a final `state` snapshot of every
 *          observable internal counter/flag:
 *            {"results":[...],"state":{...}}
 *
 *          `drainGrace` is the only op that calls SC_E2E_IsAnyCriticalFailed
 *          (which decrements the boot grace counter while grace > 0); it
 *          records the return value of the last call as `anyCriticalFailed`.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sc_types.h"
#include "sc_e2e.h"
#include "Sc_Hw_Cfg.h"

#include "harness_common.h"

/* UNIT_TEST observation hooks (declared in sc_e2e.h) */
extern uint8   SC_E2E_TestGetLastAlive(uint8 msgIndex);
extern boolean SC_E2E_TestGetFirstRx(uint8 msgIndex);
extern uint8   SC_E2E_TestGetFailCount(uint8 msgIndex);
extern boolean SC_E2E_TestGetFailed(uint8 msgIndex);
extern uint16  SC_E2E_TestGetGraceRemaining(void);
extern uint8   SC_E2E_TestCrc8(const uint8* data, uint8 len);

/* ==================================================================
 * CRC-8 helper (matches sc_e2e.c: SAE-J1850, poly 0x1D, init 0xFF)
 * ================================================================== */

static uint8 crc8_helper(const uint8* data, uint8 len)
{
    uint8 crc = SC_CRC8_INIT;
    uint8 i;
    uint8 j;
    for (i = 0u; i < len; i++) {
        crc ^= data[i];
        for (j = 0u; j < 8u; j++) {
            crc = ((crc & 0x80u) != 0u) ? (uint8)((crc << 1u) ^ SC_CRC8_POLY)
                                        : (uint8)(crc << 1u);
        }
    }
    return crc ^ 0xFFu;
}

/* ==================================================================
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_CHECK,
    OP_DRAIN_GRACE,
    OP_CRC8,
    OP_COMPUTE
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t data_id;
    uint32_t msg_index;
    uint32_t dlc;
    uint32_t alive;
    uint32_t crc_corrupt;
    uint32_t data_id_corrupt;
    uint32_t payload_corrupt;
    uint32_t null_data;
    uint32_t ticks;
    uint32_t len;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)        return OP_INIT;
    if (strcmp(s, "check") == 0)       return OP_CHECK;
    if (strcmp(s, "drainGrace") == 0)  return OP_DRAIN_GRACE;
    if (strcmp(s, "crc8") == 0)        return OP_CRC8;
    if (strcmp(s, "compute") == 0)     return OP_COMPUTE;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->data_id = 1u;
    p->msg_index = 0u;
    p->dlc = 8u;
    p->alive = 0u;
    p->ticks = 1u;
    p->len = 3u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);

    if (strcmp(key, "op") == 0)             p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0)  p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "dataId") == 0)    p->data_id = uval;
    else if (strcmp(key, "msgIndex") == 0)  p->msg_index = uval;
    else if (strcmp(key, "dlc") == 0)       p->dlc = uval;
    else if (strcmp(key, "alive") == 0)     p->alive = uval;
    else if (strcmp(key, "crcCorrupt") == 0) p->crc_corrupt = uval;
    else if (strcmp(key, "dataIdCorrupt") == 0) p->data_id_corrupt = uval;
    else if (strcmp(key, "payloadCorrupt") == 0) p->payload_corrupt = uval;
    else if (strcmp(key, "nullData") == 0)  p->null_data = uval;
    else if (strcmp(key, "ticks") == 0)     p->ticks = uval;
    else if (strcmp(key, "len") == 0)       p->len = uval;
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
 * Message builder (mirrors test_sc_e2e_asild.c build_valid_msg)
 * ================================================================== */

static void build_msg(uint8* data, uint8 data_id, uint8 alive, uint8 dlc)
{
    uint8 crc_input[7];
    uint8 payload_len;
    uint8 i;

    memset(data, 0u, 8u);
    data[0] = (uint8)((alive << 4u) | (data_id & 0x0Fu));

    payload_len = (dlc > 2u) ? (uint8)(dlc - 2u) : 0u;
    if (payload_len > 6u) {
        payload_len = 6u;
    }
    for (i = 0u; i < payload_len; i++) {
        data[2u + i] = (uint8)(0x10u + i);
    }
    for (i = 0u; i < payload_len; i++) {
        crc_input[i] = data[2u + i];
    }
    crc_input[payload_len] = data_id;
    data[1] = crc8_helper(crc_input, (uint8)(payload_len + 1u));
}

/* ==================================================================
 * State snapshot formatting
 * ================================================================== */

static size_t format_state(char* buf, size_t bufsize)
{
    /* Exercise every UNIT_TEST getter's out-of-range guard branch
     * (index SC_MB_COUNT) and the public IsMsgFailed guard so all
     * defensive lines are covered. */
    uint32_t guard_probe = 0u;
    uint8_t  api_failed[SC_MB_COUNT];
    uint8_t  e;

    for (e = 0u; e < SC_MB_COUNT; e++) {
        api_failed[e] = SC_E2E_IsMsgFailed(e) ? 1u : 0u;
    }
    guard_probe += SC_E2E_TestGetLastAlive(SC_MB_COUNT);
    guard_probe += SC_E2E_TestGetFirstRx(SC_MB_COUNT);
    guard_probe += SC_E2E_TestGetFailCount(SC_MB_COUNT);
    guard_probe += SC_E2E_TestGetFailed(SC_MB_COUNT);
    guard_probe += SC_E2E_IsMsgFailed(SC_MB_COUNT);

    return (size_t)snprintf(
        buf, bufsize,
        "\"state\":{"
        "\"lastAlive\":[%u,%u,%u,%u,%u,%u],"
        "\"firstRx\":[%u,%u,%u,%u,%u,%u],"
        "\"failCount\":[%u,%u,%u,%u,%u,%u],"
        "\"failed\":[%u,%u,%u,%u,%u,%u],"
        "\"isMsgFailed\":[%u,%u,%u,%u,%u,%u],"
        "\"isMsgFailedInvalid\":%u,"
        "\"grace\":%u,"
        "\"guardProbe\":%u}",
        (unsigned)SC_E2E_TestGetLastAlive(0),
        (unsigned)SC_E2E_TestGetLastAlive(1),
        (unsigned)SC_E2E_TestGetLastAlive(2),
        (unsigned)SC_E2E_TestGetLastAlive(3),
        (unsigned)SC_E2E_TestGetLastAlive(4),
        (unsigned)SC_E2E_TestGetLastAlive(5),
        (unsigned)SC_E2E_TestGetFirstRx(0) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFirstRx(1) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFirstRx(2) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFirstRx(3) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFirstRx(4) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFirstRx(5) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailCount(0),
        (unsigned)SC_E2E_TestGetFailCount(1),
        (unsigned)SC_E2E_TestGetFailCount(2),
        (unsigned)SC_E2E_TestGetFailCount(3),
        (unsigned)SC_E2E_TestGetFailCount(4),
        (unsigned)SC_E2E_TestGetFailCount(5),
        (unsigned)SC_E2E_TestGetFailed(0) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailed(1) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailed(2) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailed(3) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailed(4) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetFailed(5) ? 1u : 0u,
        (unsigned)api_failed[0], (unsigned)api_failed[1], (unsigned)api_failed[2],
        (unsigned)api_failed[3], (unsigned)api_failed[4], (unsigned)api_failed[5],
        (unsigned)SC_E2E_IsMsgFailed(SC_MB_COUNT) ? 1u : 0u,
        (unsigned)SC_E2E_TestGetGraceRemaining(),
        (unsigned)guard_probe);
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
        SC_E2E_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        char state_buf[1024u];
        int n;

        switch (p->op) {
            case OP_INIT:
                SC_E2E_Init();
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",%s}",
                             (rb > 0u) ? "," : "", state_buf);
                break;

            case OP_CHECK: {
                uint8 data[8];
                boolean ret;

                if (p->null_data != 0u) {
                    ret = SC_E2E_Check(NULL_PTR, (uint8)p->dlc,
                                       (uint8)p->data_id, (uint8)p->msg_index);
                } else {
                    build_msg(data, (uint8)p->data_id, (uint8)p->alive,
                              (uint8)p->dlc);
                    if (p->crc_corrupt != 0u) {
                        data[1] ^= 0xFFu;
                    }
                    if (p->data_id_corrupt != 0u) {
                        data[0] = (uint8)((data[0] & 0xF0u) |
                                          ((p->data_id + 1u) & 0x0Fu));
                    }
                    if (p->payload_corrupt != 0u) {
                        data[4] ^= 0x01u;
                    }
                    ret = SC_E2E_Check(data, (uint8)p->dlc,
                                       (uint8)p->data_id, (uint8)p->msg_index);
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"check\",\"ret\":%u,\"dataId\":%u,"
                             "\"msgIndex\":%u,\"dlc\":%u,\"alive\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (ret == TRUE) ? 1u : 0u,
                             (unsigned)p->data_id, (unsigned)p->msg_index,
                             (unsigned)p->dlc, (unsigned)p->alive, state_buf);
                break;
            }

            case OP_DRAIN_GRACE: {
                uint32_t t;
                boolean last = FALSE;
                for (t = 0u; t < p->ticks; t++) {
                    last = SC_E2E_IsAnyCriticalFailed();
                }
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"drainGrace\",\"ticks\":%u,"
                             "\"anyCriticalFailed\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->ticks,
                             (last == TRUE) ? 1u : 0u, state_buf);
                break;
            }

            case OP_CRC8: {
                uint8 vec[3] = { 0x01u, 0xAAu, 0x55u };
                uint8 crc = SC_E2E_TestCrc8((p->null_data != 0u) ? NULL_PTR : vec,
                                            (uint8)p->len);
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"crc8\",\"len\":%u,"
                             "\"crc\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->len, (unsigned)crc, state_buf);
                break;
            }

            case OP_COMPUTE: {
                uint8 vec[3] = { 0x01u, 0xAAu, 0x55u };
                uint8 crc = SC_E2E_ComputeCRC8((p->null_data != 0u) ? NULL_PTR : vec,
                                               (uint8)p->len);
                format_state(state_buf, sizeof(state_buf));
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"compute\",\"len\":%u,"
                             "\"crc\":%u,%s}",
                             (rb > 0u) ? "," : "",
                             (unsigned)p->len, (unsigned)crc, state_buf);
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
