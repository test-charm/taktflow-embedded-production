/**
 * @file    cvc_nvm_harness.c
 * @brief   Native test harness for Swc_Nvm (CVC NVM storage SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Nvm.c and drives its six APIs
 *          (Init / StoreDtc / LoadDtc / ReadCal / WriteCal / CalcCrc16)
 *          through a phase script read from stdin. Each phase is one line
 *          of whitespace-separated key=value tokens:
 *
 *            op=init                -- Swc_Nvm_Init()
 *            op=storeDtc dtcId=N status=N ffMode=0|1 [repeats=N]
 *                                   -- Swc_Nvm_StoreDtc (ffMode 0=NULL freeze
 *                                      frame, 1=0xA0+i pattern buffer)
 *            op=loadDtc slot=N [nullEntry=0|1]
 *                                   -- Swc_Nvm_LoadDtc(slot, &entry) or (slot, NULL)
 *            op=readCal [nullCal=0|1]
 *                                   -- Swc_Nvm_ReadCal(&cal) or (NULL)
 *            op=writeCal [nullCal=0|1] [pThreshold=N] [pDebounce=N]
 *                       [stuckThreshold=N] [stuckCycles=N] [lut0=N]
 *                                   -- Swc_Nvm_WriteCal (custom cal or NULL)
 *            op=corruptDtcCrc slot=N
 *                                   -- test hook: flip CRC of stored DTC slot
 *            op=corruptCalCrc       -- test hook: flip CRC of calibration block
 *            op=calcCrc [dataLen=N] [nullCrc=0|1]
 *                                   -- Swc_Nvm_CalcCrc16 on {1,2,3,4,...} or NULL
 *            skipInit=0|1           -- if set on the first phase, skip Init
 *                                     (uninitialized guard)
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[<per-op result>...],
 *             "initialized":0|1,"writeIndex":N,"dtcCount":N}
 *
 *          Per-op results:
 *            init:          {"op":"init"}
 *            storeDtc:      {"op":"storeDtc","ret":0|1,"slot":N,"writeIndex":N,"count":N}
 *            loadDtc:       {"op":"loadDtc","ret":0|1,"dtcId":N,"status":N,
 *                             "occurrenceCount":N,"ffHex":"<64 hex chars>"}
 *            readCal:       {"op":"readCal","ret":0|1,"plausThreshold":N,
 *                             "plausDebounce":N,"stuckThreshold":N,"stuckCycles":N,
 *                             "lut0":N,"lut15":N}
 *            writeCal:      {"op":"writeCal","ret":0|1}
 *            corruptDtcCrc: {"op":"corruptDtcCrc"}
 *            corruptCalCrc: {"op":"corruptCalCrc"}
 *            calcCrc:       {"op":"calcCrc","crc":N}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_Nvm.h"

#include "harness_common.h"

/* ==================================================================
 * Helpers
 * ================================================================== */

/* ==================================================================
 * Phase model
 * ================================================================== */

enum {
    OP_INIT = 1,
    OP_STORE_DTC,
    OP_LOAD_DTC,
    OP_READ_CAL,
    OP_WRITE_CAL,
    OP_CORRUPT_DTC_CRC,
    OP_CORRUPT_CAL_CRC,
    OP_CALC_CRC
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t repeats;
    uint32_t dtc_id;
    uint32_t status;
    uint32_t ff_mode;
    uint32_t slot;
    uint8_t  null_entry;
    uint8_t  null_cal;
    uint32_t p_threshold;
    uint32_t p_debounce;
    uint32_t stuck_threshold;
    uint32_t stuck_cycles;
    uint32_t lut0;
    uint32_t data_len;
    uint8_t  null_crc;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)            return OP_INIT;
    if (strcmp(s, "storeDtc") == 0)        return OP_STORE_DTC;
    if (strcmp(s, "loadDtc") == 0)         return OP_LOAD_DTC;
    if (strcmp(s, "readCal") == 0)         return OP_READ_CAL;
    if (strcmp(s, "writeCal") == 0)        return OP_WRITE_CAL;
    if (strcmp(s, "corruptDtcCrc") == 0)   return OP_CORRUPT_DTC_CRC;
    if (strcmp(s, "corruptCalCrc") == 0)   return OP_CORRUPT_CAL_CRC;
    if (strcmp(s, "calcCrc") == 0)         return OP_CALC_CRC;
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->repeats = 1u;
    p->data_len = 4u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "op") == 0)              p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0)   p->skip_init = (uint8_t)val;
    else if (strcmp(key, "repeats") == 0)    p->repeats = val;
    else if (strcmp(key, "dtcId") == 0)      p->dtc_id = val;
    else if (strcmp(key, "status") == 0)     p->status = val;
    else if (strcmp(key, "ffMode") == 0)     p->ff_mode = val;
    else if (strcmp(key, "slot") == 0)       p->slot = val;
    else if (strcmp(key, "nullEntry") == 0)  p->null_entry = (uint8_t)val;
    else if (strcmp(key, "nullCal") == 0)    p->null_cal = (uint8_t)val;
    else if (strcmp(key, "pThreshold") == 0) p->p_threshold = val;
    else if (strcmp(key, "pDebounce") == 0)  p->p_debounce = val;
    else if (strcmp(key, "stuckThreshold") == 0) p->stuck_threshold = val;
    else if (strcmp(key, "stuckCycles") == 0) p->stuck_cycles = val;
    else if (strcmp(key, "lut0") == 0)       p->lut0 = val;
    else if (strcmp(key, "dataLen") == 0)    p->data_len = val;
    else if (strcmp(key, "nullCrc") == 0)    p->null_crc = (uint8_t)val;
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
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    char results_buf[64u * 384u];
    size_t rb = 0u;
    uint8_t global_skip_init;

    /* ---- parse all phases ---- */
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
        Swc_Nvm_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                Swc_Nvm_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\"}", (rb > 0u) ? "," : "");
                break;

            case OP_STORE_DTC: {
                uint32 rep;
                int last_ret = 0;
                uint32 slot_at_store = 0u;
                for (rep = 0u; rep < p->repeats; rep++) {
                    uint8 ff[NVM_FREEZE_FRAME_SIZE];
                    uint8 i;
                    Std_ReturnType ret;
                    if (p->ff_mode != 0u) {
                        for (i = 0u; i < NVM_FREEZE_FRAME_SIZE; i++) {
                            ff[i] = (uint8)(0xA0u + i);
                        }
                    }
                    slot_at_store = (uint32)Swc_Nvm_TestGetDtcWriteIndex();
                    ret = Swc_Nvm_StoreDtc((uint8)p->dtc_id, (uint8)p->status,
                                           (p->ff_mode != 0u) ? ff : NULL_PTR);
                    last_ret = (ret == E_OK) ? 0 : 1;
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"storeDtc\",\"ret\":%d,\"slot\":%u,"
                             "\"writeIndex\":%u,\"count\":%u}",
                             (rb > 0u) ? "," : "", last_ret,
                             (unsigned)slot_at_store,
                             (unsigned)Swc_Nvm_TestGetDtcWriteIndex(),
                             (unsigned)Swc_Nvm_TestGetDtcCount());
                break;
            }

            case OP_LOAD_DTC: {
                Swc_Nvm_DtcEntryType entry;
                Std_ReturnType ret;
                char hex[65];
                uint8 i;
                ret = Swc_Nvm_LoadDtc((uint8)p->slot,
                                      p->null_entry ? NULL_PTR : &entry);
                for (i = 0u; i < NVM_FREEZE_FRAME_SIZE; i++) {
                    snprintf(hex + i * 2u, 3u, "%02x",
                             (unsigned)(p->null_entry ? 0u : entry.freezeFrame[i]));
                }
                hex[64] = '\0';
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"loadDtc\",\"ret\":%d,\"dtcId\":%u,"
                             "\"status\":%u,\"occurrenceCount\":%u,\"ffHex\":\"%s\"}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (unsigned)(p->null_entry ? 0u : entry.dtcId),
                             (unsigned)(p->null_entry ? 0u : entry.status),
                             (unsigned)(p->null_entry ? 0u : entry.occurrenceCount),
                             hex);
                break;
            }

            case OP_READ_CAL: {
                Swc_Nvm_CalDataType cal;
                Std_ReturnType ret;
                ret = Swc_Nvm_ReadCal(p->null_cal ? NULL_PTR : &cal);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"readCal\",\"ret\":%d,"
                             "\"plausThreshold\":%u,\"plausDebounce\":%u,"
                             "\"stuckThreshold\":%u,\"stuckCycles\":%u,"
                             "\"lut0\":%u,\"lut15\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (unsigned)(p->null_cal ? 0u : cal.plausThreshold),
                             (unsigned)(p->null_cal ? 0u : cal.plausDebounce),
                             (unsigned)(p->null_cal ? 0u : cal.stuckThreshold),
                             (unsigned)(p->null_cal ? 0u : cal.stuckCycles),
                             (unsigned)(p->null_cal ? 0u : cal.torqueLut[0]),
                             (unsigned)(p->null_cal ? 0u
                                          : cal.torqueLut[NVM_TORQUE_LUT_SIZE - 1u]));
                break;
            }

            case OP_WRITE_CAL: {
                Swc_Nvm_CalDataType cal;
                Std_ReturnType ret;
                uint8 i;
                memset(&cal, 0, sizeof(cal));
                cal.plausThreshold = (uint16)p->p_threshold;
                cal.plausDebounce  = (uint8)p->p_debounce;
                cal.stuckThreshold = (uint16)p->stuck_threshold;
                cal.stuckCycles    = (uint16)p->stuck_cycles;
                cal.torqueLut[0]   = (uint16)p->lut0;
                for (i = 1u; i < NVM_TORQUE_LUT_SIZE; i++) {
                    cal.torqueLut[i] = 0u;
                }
                ret = Swc_Nvm_WriteCal(p->null_cal ? NULL_PTR : &cal);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"writeCal\",\"ret\":%d}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1);
                break;
            }

            case OP_CORRUPT_DTC_CRC:
                Swc_Nvm_TestCorruptDtcCrc((uint8)p->slot);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptDtcCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CORRUPT_CAL_CRC:
                Swc_Nvm_TestCorruptCalCrc();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptCalCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CALC_CRC: {
                uint8 buf[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
                uint16 crc = Swc_Nvm_CalcCrc16(p->null_crc ? NULL_PTR : buf,
                                               (uint16)p->data_len);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"calcCrc\",\"crc\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)crc);
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

    printf("{\"results\":[%s],"
           "\"initialized\":%u,\"writeIndex\":%u,\"dtcCount\":%u}\n",
           results_buf,
           (unsigned)Swc_Nvm_TestGetInitialized(),
           (unsigned)Swc_Nvm_TestGetDtcWriteIndex(),
           (unsigned)Swc_Nvm_TestGetDtcCount());

    return 0;
}
