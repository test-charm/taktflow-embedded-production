/**
 * @file    rzc_nvm_harness.c
 * @brief   Native test harness for Swc_RzcNvm (RZC NVM DTC persistence SWC)
 * @date    2026-08-18
 *
 * @details Links the REAL production Swc_RzcNvm.c and drives its public APIs
 *          (Init / StoreDtc / LoadDtc / GetWriteIndex) through a phase script
 *          read from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens:
 *
 *            op=init
 *            op=storeDtc dtcId=N status=N timestamp=N [motorCurrentMa=N]
 *                       [motorTempDdc=N] [motorSpeedRpm=N] [batteryMv=N]
 *                       [torqueCmdPct=N] [vehicleState=N] [repeats=N]
 *                       [nullFreeze=0|1]
 *            op=loadDtc slot=N [nullEntry=0|1]
 *            op=corruptDtcCrc slot=N
 *            op=calcCrc [dataLen=N]
 *            skipInit=0|1
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[...],"initialized":0|1,"writeIndex":N}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_RzcNvm.h"

#include "harness_common.h"

extern uint8  Swc_RzcNvm_TestGetInitialized(void);
extern void   Swc_RzcNvm_TestCorruptDtcCrc(uint8 slotIndex);
extern uint16 Swc_RzcNvm_TestCrc16(const uint8 *data, uint16 length);

enum {
    OP_INIT = 1,
    OP_STORE_DTC,
    OP_LOAD_DTC,
    OP_CORRUPT_DTC_CRC,
    OP_CALC_CRC
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t repeats;
    uint32_t dtc_id;
    uint32_t status;
    uint32_t timestamp;
    uint32_t motor_current_ma;
    int32_t  motor_temp_ddc;
    uint32_t motor_speed_rpm;
    uint32_t battery_mv;
    int32_t  torque_cmd_pct;
    uint32_t vehicle_state;
    uint32_t slot;
    uint8_t  null_freeze;
    uint8_t  null_entry;
    uint32_t data_len;
} Phase;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)          return OP_INIT;
    if (strcmp(s, "storeDtc") == 0)      return OP_STORE_DTC;
    if (strcmp(s, "loadDtc") == 0)       return OP_LOAD_DTC;
    if (strcmp(s, "corruptDtcCrc") == 0) return OP_CORRUPT_DTC_CRC;
    if (strcmp(s, "calcCrc") == 0)       return OP_CALC_CRC;
    return 0;
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->repeats = 1u;
    p->data_len = 4u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t uval = harness_parse_uint(value);
    int32_t  sval = harness_parse_int(value);

    if (strcmp(key, "op") == 0)               p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0)    p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "repeats") == 0)     p->repeats = uval;
    else if (strcmp(key, "dtcId") == 0)       p->dtc_id = uval;
    else if (strcmp(key, "status") == 0)      p->status = uval;
    else if (strcmp(key, "timestamp") == 0)   p->timestamp = uval;
    else if (strcmp(key, "motorCurrentMa") == 0)  p->motor_current_ma = uval;
    else if (strcmp(key, "motorTempDdc") == 0)    p->motor_temp_ddc = sval;
    else if (strcmp(key, "motorSpeedRpm") == 0)   p->motor_speed_rpm = uval;
    else if (strcmp(key, "batteryMv") == 0)       p->battery_mv = uval;
    else if (strcmp(key, "torqueCmdPct") == 0)    p->torque_cmd_pct = sval;
    else if (strcmp(key, "vehicleState") == 0)    p->vehicle_state = uval;
    else if (strcmp(key, "slot") == 0)            p->slot = uval;
    else if (strcmp(key, "nullFreeze") == 0)      p->null_freeze = (uint8_t)uval;
    else if (strcmp(key, "nullEntry") == 0)       p->null_entry = (uint8_t)uval;
    else if (strcmp(key, "dataLen") == 0)         p->data_len = uval;
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
    char results_buf[HARNESS_MAX_PHASES * 512u];
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
        Swc_RzcNvm_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                Swc_RzcNvm_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\"}", (rb > 0u) ? "," : "");
                break;

            case OP_STORE_DTC: {
                uint32_t rep;
                int last_ret = 0;
                uint32_t slot_at_store = 0u;
                Swc_RzcNvm_FreezeFrameType ff;
                memset(&ff, 0, sizeof(ff));
                ff.motor_current_ma = (uint16)p->motor_current_ma;
                ff.motor_temp_ddc   = (sint16)p->motor_temp_ddc;
                ff.motor_speed_rpm  = (uint16)p->motor_speed_rpm;
                ff.battery_mv       = (uint16)p->battery_mv;
                ff.torque_cmd_pct   = (sint16)p->torque_cmd_pct;
                ff.vehicle_state    = (uint8)p->vehicle_state;
                for (rep = 0u; rep < p->repeats; rep++) {
                    Std_ReturnType ret;
                    slot_at_store = (uint32_t)Swc_RzcNvm_GetWriteIndex();
                    ret = Swc_RzcNvm_StoreDtc(
                        (uint8)p->dtc_id,
                        (uint8)p->status,
                        p->timestamp,
                        p->null_freeze ? NULL_PTR : &ff);
                    last_ret = (ret == E_OK) ? 0 : 1;
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"storeDtc\",\"ret\":%d,\"slot\":%u,"
                             "\"writeIndex\":%u}",
                             (rb > 0u) ? "," : "", last_ret,
                             (unsigned)slot_at_store,
                             (unsigned)Swc_RzcNvm_GetWriteIndex());
                break;
            }

            case OP_LOAD_DTC: {
                Swc_RzcNvm_DtcEntryType entry;
                Std_ReturnType ret = Swc_RzcNvm_LoadDtc(
                    (uint8)p->slot,
                    p->null_entry ? NULL_PTR : &entry);
                if (p->null_entry != 0u || ret != E_OK) {
                    memset(&entry, 0, sizeof(entry));
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"loadDtc\",\"ret\":%d,\"dtcId\":%u,"
                             "\"status\":%u,\"timestamp\":%u,"
                             "\"motorCurrentMa\":%u,\"motorTempDdc\":%d,"
                             "\"motorSpeedRpm\":%u,\"batteryMv\":%u,"
                             "\"torqueCmdPct\":%d,\"vehicleState\":%u,"
                             "\"crc\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (unsigned)entry.dtc_id,
                             (unsigned)entry.status,
                             (unsigned)entry.timestamp,
                             (unsigned)entry.freeze_frame.motor_current_ma,
                             (int)entry.freeze_frame.motor_temp_ddc,
                             (unsigned)entry.freeze_frame.motor_speed_rpm,
                             (unsigned)entry.freeze_frame.battery_mv,
                             (int)entry.freeze_frame.torque_cmd_pct,
                             (unsigned)entry.freeze_frame.vehicle_state,
                             (unsigned)entry.crc16);
                break;
            }

            case OP_CORRUPT_DTC_CRC:
                Swc_RzcNvm_TestCorruptDtcCrc((uint8)p->slot);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptDtcCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CALC_CRC: {
                uint8 buf[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
                uint16 crc = Swc_RzcNvm_TestCrc16(buf, (uint16)p->data_len);
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

    printf("{\"results\":[%s],\"initialized\":%u,\"writeIndex\":%u}\n",
           results_buf,
           (unsigned)Swc_RzcNvm_TestGetInitialized(),
           (unsigned)Swc_RzcNvm_GetWriteIndex());

    return 0;
}
