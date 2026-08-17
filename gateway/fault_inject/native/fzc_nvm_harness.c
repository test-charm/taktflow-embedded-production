/**
 * @file    fzc_nvm_harness.c
 * @brief   Native test harness for Swc_FzcNvm (FZC NVM storage SWC)
 * @date    2026-08-17
 *
 * @details Links the REAL production Swc_FzcNvm.c and drives its public APIs
 *          (Init / StoreDtc / LoadDtc / LoadCal / StoreCal / Crc16) through a
 *          phase script read from stdin. Each phase is one line of
 *          whitespace-separated key=value tokens:
 *
 *            op=init
 *            op=storeDtc dtcId=N steerAngle=N brakePos=N lidarDist=N [repeats=N]
 *            op=loadDtc slot=N [nullRecord=0|1]
 *            op=readCal [nullCal=0|1]
 *            op=writeCal [nullCal=0|1] [steerCenterOffset=N] [steerGain=N]
 *                       [brakePosOffset=N] [brakeGain=N] [lidarWarnCm=N]
 *                       [lidarBrakeCm=N] [lidarEmergencyCm=N]
 *            op=corruptDtcCrc slot=N
 *            op=corruptCalCrc
 *            op=corruptBackendCalCrc
 *            op=calcCrc [dataLen=N] [nullCrc=0|1]
 *            skipInit=0|1
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[...],"initialized":0|1,"occupiedSlots":N,
 *             "backendReadCount":N,"backendWriteCount":N}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Swc_FzcNvm.h"

#include "harness_common.h"

extern uint8 Swc_FzcNvm_TestGetInitialized(void);
extern void Swc_FzcNvm_TestCorruptDtcCrc(uint8 index);
extern void Swc_FzcNvm_TestCorruptCalCrc(void);

enum {
    OP_INIT = 1,
    OP_STORE_DTC,
    OP_LOAD_DTC,
    OP_READ_CAL,
    OP_WRITE_CAL,
    OP_CORRUPT_DTC_CRC,
    OP_CORRUPT_CAL_CRC,
    OP_CORRUPT_BACKEND_CAL_CRC,
    OP_CALC_CRC
};

typedef struct {
    uint8_t  op;
    uint8_t  skip_init;
    uint32_t repeats;
    uint32_t dtc_id;
    int32_t  steer_angle;
    uint32_t brake_pos;
    uint32_t lidar_dist;
    uint32_t slot;
    uint8_t  null_record;
    uint8_t  null_cal;
    int32_t  steer_center_offset;
    uint32_t steer_gain;
    int32_t  brake_pos_offset;
    uint32_t brake_gain;
    uint32_t lidar_warn_cm;
    uint32_t lidar_brake_cm;
    uint32_t lidar_emergency_cm;
    uint32_t data_len;
    uint8_t  null_crc;
} Phase;

static Swc_FzcNvm_DtcRecord backend_dtc_slots[FZC_NVM_DTC_MAX_SLOTS];
static Swc_FzcNvm_CalData   backend_cal_data;
static uint32_t backend_read_count;
static uint32_t backend_write_count;

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)                 return OP_INIT;
    if (strcmp(s, "storeDtc") == 0)             return OP_STORE_DTC;
    if (strcmp(s, "loadDtc") == 0)              return OP_LOAD_DTC;
    if (strcmp(s, "readCal") == 0)              return OP_READ_CAL;
    if (strcmp(s, "writeCal") == 0)             return OP_WRITE_CAL;
    if (strcmp(s, "corruptDtcCrc") == 0)        return OP_CORRUPT_DTC_CRC;
    if (strcmp(s, "corruptCalCrc") == 0)        return OP_CORRUPT_CAL_CRC;
    if (strcmp(s, "corruptBackendCalCrc") == 0) return OP_CORRUPT_BACKEND_CAL_CRC;
    if (strcmp(s, "calcCrc") == 0)              return OP_CALC_CRC;
    return 0;
}

Std_ReturnType NvM_ReadBlock(uint8 BlockId, void* DstPtr)
{
    backend_read_count++;
    if (DstPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (BlockId == 0u) {
        memcpy(DstPtr, backend_dtc_slots, sizeof(backend_dtc_slots));
        return E_OK;
    }
    if (BlockId == 1u) {
        memcpy(DstPtr, &backend_cal_data, sizeof(backend_cal_data));
        return E_OK;
    }
    return E_NOT_OK;
}

Std_ReturnType NvM_WriteBlock(uint8 BlockId, const void* SrcPtr)
{
    backend_write_count++;
    if (SrcPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (BlockId == 0u) {
        memcpy(backend_dtc_slots, SrcPtr, sizeof(backend_dtc_slots));
        return E_OK;
    }
    if (BlockId == 1u) {
        memcpy(&backend_cal_data, SrcPtr, sizeof(backend_cal_data));
        return E_OK;
    }
    return E_NOT_OK;
}

static uint32_t count_occupied_slots(void)
{
    uint32_t count = 0u;
    uint8 i;
    for (i = 0u; i < FZC_NVM_DTC_MAX_SLOTS; i++) {
        Swc_FzcNvm_DtcRecord record;
        if (Swc_FzcNvm_LoadDtc(i, &record) == E_OK) {
            count++;
        }
    }
    return count;
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

    if (strcmp(key, "op") == 0)                        p->op = (uint8_t)parse_op(value);
    else if (strcmp(key, "skipInit") == 0)            p->skip_init = (uint8_t)uval;
    else if (strcmp(key, "repeats") == 0)             p->repeats = uval;
    else if (strcmp(key, "dtcId") == 0)               p->dtc_id = uval;
    else if (strcmp(key, "steerAngle") == 0)          p->steer_angle = sval;
    else if (strcmp(key, "brakePos") == 0)            p->brake_pos = uval;
    else if (strcmp(key, "lidarDist") == 0)           p->lidar_dist = uval;
    else if (strcmp(key, "slot") == 0)                p->slot = uval;
    else if (strcmp(key, "nullRecord") == 0)          p->null_record = (uint8_t)uval;
    else if (strcmp(key, "nullCal") == 0)             p->null_cal = (uint8_t)uval;
    else if (strcmp(key, "steerCenterOffset") == 0)   p->steer_center_offset = sval;
    else if (strcmp(key, "steerGain") == 0)           p->steer_gain = uval;
    else if (strcmp(key, "brakePosOffset") == 0)      p->brake_pos_offset = sval;
    else if (strcmp(key, "brakeGain") == 0)           p->brake_gain = uval;
    else if (strcmp(key, "lidarWarnCm") == 0)         p->lidar_warn_cm = uval;
    else if (strcmp(key, "lidarBrakeCm") == 0)        p->lidar_brake_cm = uval;
    else if (strcmp(key, "lidarEmergencyCm") == 0)    p->lidar_emergency_cm = uval;
    else if (strcmp(key, "dataLen") == 0)             p->data_len = uval;
    else if (strcmp(key, "nullCrc") == 0)             p->null_crc = (uint8_t)uval;
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

    memset(backend_dtc_slots, 0, sizeof(backend_dtc_slots));
    memset(&backend_cal_data, 0, sizeof(backend_cal_data));

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
        Swc_FzcNvm_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                Swc_FzcNvm_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\",\"backendReadCount\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)backend_read_count);
                break;

            case OP_STORE_DTC: {
                uint32_t rep;
                int last_ret = 0;
                for (rep = 0u; rep < p->repeats; rep++) {
                    Std_ReturnType ret = Swc_FzcNvm_StoreDtc(
                        (uint8)p->dtc_id,
                        (sint16)p->steer_angle,
                        (uint8)p->brake_pos,
                        (uint16)p->lidar_dist);
                    last_ret = (ret == E_OK) ? 0 : 1;
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"storeDtc\",\"ret\":%d,"
                             "\"occupiedSlots\":%u,\"backendWriteCount\":%u}",
                             (rb > 0u) ? "," : "",
                             last_ret,
                             (unsigned)count_occupied_slots(),
                             (unsigned)backend_write_count);
                break;
            }

            case OP_LOAD_DTC: {
                Swc_FzcNvm_DtcRecord record;
                Std_ReturnType ret = Swc_FzcNvm_LoadDtc(
                    (uint8)p->slot,
                    p->null_record ? NULL_PTR : &record);
                if (p->null_record != 0u || ret != E_OK) {
                    memset(&record, 0, sizeof(record));
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"loadDtc\",\"ret\":%d,\"dtcId\":%u,"
                             "\"status\":%u,\"freezeSteer\":%d,"
                             "\"freezeBrake\":%u,\"freezeLidar\":%u,\"crc\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (unsigned)record.dtcId,
                             (unsigned)record.status,
                             (int)record.freezeSteer,
                             (unsigned)record.freezeBrake,
                             (unsigned)record.freezeLidar,
                             (unsigned)record.crc);
                break;
            }

            case OP_READ_CAL: {
                Swc_FzcNvm_CalData cal;
                Std_ReturnType ret = Swc_FzcNvm_LoadCal(p->null_cal ? NULL_PTR : &cal);
                if (p->null_cal != 0u) {
                    memset(&cal, 0, sizeof(cal));
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"readCal\",\"ret\":%d,"
                             "\"steerCenterOffset\":%d,\"steerGain\":%u,"
                             "\"brakePosOffset\":%d,\"brakeGain\":%u,"
                             "\"lidarWarnCm\":%u,\"lidarBrakeCm\":%u,"
                             "\"lidarEmergencyCm\":%u,\"crc\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (int)cal.steerCenterOffset,
                             (unsigned)cal.steerGain,
                             (int)cal.brakePosOffset,
                             (unsigned)cal.brakeGain,
                             (unsigned)cal.lidarWarnCm,
                             (unsigned)cal.lidarBrakeCm,
                             (unsigned)cal.lidarEmergencyCm,
                             (unsigned)cal.crc);
                break;
            }

            case OP_WRITE_CAL: {
                Swc_FzcNvm_CalData cal;
                Std_ReturnType ret;
                memset(&cal, 0, sizeof(cal));
                cal.steerCenterOffset = (sint16)p->steer_center_offset;
                cal.steerGain         = (uint16)p->steer_gain;
                cal.brakePosOffset    = (sint16)p->brake_pos_offset;
                cal.brakeGain         = (uint16)p->brake_gain;
                cal.lidarWarnCm       = (uint16)p->lidar_warn_cm;
                cal.lidarBrakeCm      = (uint16)p->lidar_brake_cm;
                cal.lidarEmergencyCm  = (uint16)p->lidar_emergency_cm;
                ret = Swc_FzcNvm_StoreCal(p->null_cal ? NULL_PTR : &cal);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"writeCal\",\"ret\":%d,"
                             "\"backendWriteCount\":%u}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1,
                             (unsigned)backend_write_count);
                break;
            }

            case OP_CORRUPT_DTC_CRC:
                Swc_FzcNvm_TestCorruptDtcCrc((uint8)p->slot);
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptDtcCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CORRUPT_CAL_CRC:
                Swc_FzcNvm_TestCorruptCalCrc();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptCalCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CORRUPT_BACKEND_CAL_CRC:
                backend_cal_data.crc ^= 0xFFFFu;
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"corruptBackendCalCrc\"}",
                             (rb > 0u) ? "," : "");
                break;

            case OP_CALC_CRC: {
                uint8 buf[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
                uint16 crc = Swc_FzcNvm_Crc16(p->null_crc ? NULL_PTR : buf,
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

    printf("{\"results\":[%s],\"initialized\":%u,\"occupiedSlots\":%u,"
           "\"backendReadCount\":%u,\"backendWriteCount\":%u}\n",
           results_buf,
           (unsigned)Swc_FzcNvm_TestGetInitialized(),
           (unsigned)count_occupied_slots(),
           (unsigned)backend_read_count,
           (unsigned)backend_write_count);

    return 0;
}
