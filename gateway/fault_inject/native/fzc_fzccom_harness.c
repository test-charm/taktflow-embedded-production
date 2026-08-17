/**
 * @file    fzc_fzccom_harness.c
 * @brief   Native test harness for Swc_FzcCom (FZC CAN communication SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_FzcCom.c and drives its five APIs
 *          through a phase script read from stdin. Each phase is one line
 *          of whitespace-separated key=value tokens:
 *
 *            op=init                       -- Swc_FzcCom_Init()
 *            op=e2eProtect data=<hex16> [dataId=N] [len=N] [repeats=N]
 *                                          -- Swc_FzcCom_E2eProtect (CRC-8 + alive)
 *            op=e2eCheck  data=<hex16> [dataId=N] [len=N] [repeats=N]
 *                                          -- Swc_FzcCom_E2eCheck (CRC verify)
 *            op=receive [cycles=N]         -- Swc_FzcCom_Receive() N times
 *            op=tx [cycles=N] [RTE inputs] -- Swc_FzcCom_TransmitSchedule() N times
 *
 *          Phase keys (all optional except op):
 *            cycles      uint32 (default 1) cyclic call count
 *            skipInit    0|1    skip Swc_FzcCom_Init (uninitialized guard)
 *            dataId      uint32 E2E Data ID (FZC-specific, from Fzc_Cfg.h)
 *            data        hex    E2E 8-byte payload (hex, e.g. 0011223344556677)
 *                               special value "null" passes NULL_PTR
 *            len         uint32 E2E payload length (default 8)
 *            repeats     uint32 repeat count for e2eProtect / e2eCheck
 *            vehicleState uint32 RTE FZC_SIG_VEHICLE_STATE (tx input)
 *            faultMask   uint32 RTE FZC_SIG_FAULT_MASK (tx input)
 *            steerAngle  uint32 RTE FZC_SIG_STEER_ANGLE (tx input, sint16)
 *            steerFault  uint32 RTE FZC_SIG_STEER_FAULT (tx input)
 *            brakePos    uint32 RTE FZC_SIG_BRAKE_POS (tx input)
 *            brakeFault  uint32 RTE FZC_SIG_BRAKE_FAULT (tx input)
 *            motorCutoff uint32 RTE FZC_SIG_MOTOR_CUTOFF (tx input)
 *            lidarZone   uint32 RTE FZC_SIG_LIDAR_ZONE (tx input)
 *            lidarDist   uint32 RTE FZC_SIG_LIDAR_DIST (tx input, uint16)
 *            lidarSignal uint32 RTE FZC_SIG_LIDAR_SIGNAL (tx input, uint16)
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[<per-op result>...],
 *             "canmonNotify":N,"rteDispatch":N,"steerComSend":N,
 *             "hbEcuId":N,"hbOpMode":N,"hbFaultStatus":N,
 *             "steerAngle":N,"steerFault":N,"brakePos":N,
 *             "brakeFaultType":N,"motorCutoffReq":N,
 *             "lidarZone":N,"lidarRange":N,"lidarSignal":N}
 *          Per-op results:
 *            init:       {"op":"init"}
 *            e2eProtect: {"op":"e2eProtect","ret":0|1,"data":"<hex16>"}
 *            e2eCheck:   {"op":"e2eCheck","ret":0|1}
 *            receive:    {"op":"receive","canmonNotify":N}
 *            tx:         {"op":"tx"}
 *          canmonNotify counts Swc_FzcCanMonitor_NotifyRx calls during the
 *          phase (reset per phase); rteDispatch / steerComSend are the
 *          g_dbg_* instrumentation counters (reset per phase).
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_FzcCom.h"
#include "Com.h"
#include "Rte.h"
#include "Swc_FzcCanMonitor.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_COM_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];
static uint32_t mock_canmon_notify_count;

/* ==================================================================
 * BSW stubs
 * ================================================================== */

Std_ReturnType Rte_Read(Rte_SignalIdType SignalId, uint32* DataPtr)
{
    if ((DataPtr == NULL_PTR) || (SignalId >= MOCK_RTE_MAX_SIGNALS)) {
        return E_NOT_OK;
    }
    *DataPtr = mock_rte_signals[SignalId];
    return E_OK;
}

static uint8 com_signal_is_16bit(Com_SignalIdType SignalId)
{
    switch (SignalId) {
        case FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE:
        case FZC_COM_SIG_LIDAR_DISTANCE_RANGE_CM:
            return 1u;
        default:
            return 0u;
    }
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (com_signal_is_16bit(SignalId)) {
        mock_com_signals[SignalId] = (uint32)(*(const uint16*)SignalDataPtr);
    } else {
        mock_com_signals[SignalId] = (uint32)(*(const uint8*)SignalDataPtr);
    }
    return E_OK;
}

void Swc_FzcCanMonitor_NotifyRx(void)
{
    mock_canmon_notify_count++;
}

/* ==================================================================
 * Helpers
 * ================================================================== */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char* s, uint8* out, uint8 max_len)
{
    size_t slen = strlen(s);
    size_t i;
    if ((slen % 2u) != 0u) {
        return -1;
    }
    for (i = 0u; i < slen / 2u; i++) {
        int hi = hex_nibble(s[i * 2u]);
        int lo = hex_nibble(s[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        if (i >= (size_t)max_len) {
            return -1;
        }
        out[i] = (uint8)((hi << 4) | lo);
    }
    return (int)(slen / 2u);
}

static void reset_com_signals(void)
{
    uint16_t i;
    for (i = 0u; i < MOCK_COM_MAX_SIGNALS; i++) {
        mock_com_signals[i] = 0u;
    }
}

typedef struct {
    uint8_t  op;                 /* OP_* enum below */
    uint8_t  skip_init;
    uint32_t cycles;
    uint32_t data_id;
    uint8_t  data[8];
    uint8_t  data_len;
    uint8_t  data_is_null;
    uint32_t repeats;
    uint32_t vehicle_state;
    uint32_t fault_mask;
    uint32_t steer_angle;
    uint32_t steer_fault;
    uint32_t brake_pos;
    uint32_t brake_fault;
    uint32_t motor_cutoff;
    uint32_t lidar_zone;
    uint32_t lidar_dist;
    uint32_t lidar_signal;
} Phase;

enum {
    OP_INIT = 1,
    OP_E2E_PROTECT,
    OP_E2E_CHECK,
    OP_RECEIVE,
    OP_TX
};

static int parse_op(const char* s)
{
    if (strcmp(s, "init") == 0)       return OP_INIT;
    if (strcmp(s, "e2eProtect") == 0) return OP_E2E_PROTECT;
    if (strcmp(s, "e2eCheck") == 0)   return OP_E2E_CHECK;
    if (strcmp(s, "receive") == 0)    return OP_RECEIVE;
    if (strcmp(s, "tx") == 0)         return OP_TX;
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles        = 1u;
    p->data_len      = 8u;
    p->repeats       = 1u;
    p->vehicle_state = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    const char* val = value;
    if (strcmp(key, "op") == 0)          p->op = (uint8_t)parse_op(val);
    else if (strcmp(key, "cycles") == 0) p->cycles = harness_parse_uint(val);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)harness_parse_uint(val);
    else if (strcmp(key, "dataId") == 0) p->data_id = harness_parse_uint(val);
    else if (strcmp(key, "len") == 0)    p->data_len = (uint8_t)harness_parse_uint(val);
    else if (strcmp(key, "repeats") == 0) p->repeats = harness_parse_uint(val);
    else if (strcmp(key, "data") == 0) {
        if (strcmp(val, "null") == 0) {
            p->data_is_null = 1u;
        } else {
            int n = parse_hex(val, p->data, 8u);
            if (n < 0) {
                fprintf(stderr, "bad hex data: %s\n", val);
                return 1;
            }
            p->data_len = (uint8_t)n;
        }
    }
    else if (strcmp(key, "vehicleState") == 0) p->vehicle_state = harness_parse_uint(val);
    else if (strcmp(key, "faultMask") == 0)   p->fault_mask = harness_parse_uint(val);
    else if (strcmp(key, "steerAngle") == 0)  p->steer_angle = harness_parse_uint(val);
    else if (strcmp(key, "steerFault") == 0)  p->steer_fault = harness_parse_uint(val);
    else if (strcmp(key, "brakePos") == 0)    p->brake_pos = harness_parse_uint(val);
    else if (strcmp(key, "brakeFault") == 0)  p->brake_fault = harness_parse_uint(val);
    else if (strcmp(key, "motorCutoff") == 0) p->motor_cutoff = harness_parse_uint(val);
    else if (strcmp(key, "lidarZone") == 0)   p->lidar_zone = harness_parse_uint(val);
    else if (strcmp(key, "lidarDist") == 0)   p->lidar_dist = harness_parse_uint(val);
    else if (strcmp(key, "lidarSignal") == 0) p->lidar_signal = harness_parse_uint(val);
    return 0;
}

static int finish_phase(void* phase, size_t index)
{
    Phase* p = (Phase*)phase;
    if (p->op == 0) {
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
    uint8_t global_skip_init = 0u;
    /* results JSON buffer: 64 results x ~256 chars */
    char results_buf[64u * 256u];
    size_t rb = 0u;

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
        Swc_FzcCom_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                Swc_FzcCom_Init();
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"init\"}", (rb > 0u) ? "," : "");
                break;

            case OP_E2E_PROTECT: {
                uint8 buf[8];
                Std_ReturnType ret;
                char hex[17];
                uint32 rep;
                uint8 i;
                memcpy(buf, p->data, sizeof(buf));
                ret = E_OK;
                for (rep = 0u; rep < p->repeats; rep++) {
                    ret = Swc_FzcCom_E2eProtect(p->data_is_null ? NULL_PTR : buf,
                                                p->data_len, (uint8)p->data_id);
                }
                for (i = 0u; i < 8u; i++) {
                    snprintf(hex + i * 2u, 3u, "%02x", (unsigned)buf[i]);
                }
                hex[16] = '\0';
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"e2eProtect\",\"ret\":%d,\"data\":\"%s\"}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1, hex);
                break;
            }

            case OP_E2E_CHECK: {
                Std_ReturnType ret = E_OK;
                uint32 rep;
                for (rep = 0u; rep < p->repeats; rep++) {
                    ret = Swc_FzcCom_E2eCheck(
                        p->data_is_null ? NULL_PTR : p->data,
                        p->data_is_null ? 0u : p->data_len, (uint8)p->data_id);
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"e2eCheck\",\"ret\":%d}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1);
                break;
            }

            case OP_RECEIVE: {
                uint32_t i;
                mock_canmon_notify_count = 0u;
                for (i = 0u; i < p->cycles; i++) {
                    Swc_FzcCom_Receive();
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"receive\",\"canmonNotify\":%u}",
                             (rb > 0u) ? "," : "",
                             (unsigned)mock_canmon_notify_count);
                break;
            }

            case OP_TX: {
                uint32_t i;
                mock_rte_signals[FZC_SIG_VEHICLE_STATE] = p->vehicle_state;
                mock_rte_signals[FZC_SIG_FAULT_MASK]    = p->fault_mask;
                mock_rte_signals[FZC_SIG_STEER_ANGLE]   = p->steer_angle;
                mock_rte_signals[FZC_SIG_STEER_FAULT]   = p->steer_fault;
                mock_rte_signals[FZC_SIG_BRAKE_POS]     = p->brake_pos;
                mock_rte_signals[FZC_SIG_BRAKE_FAULT]   = p->brake_fault;
                mock_rte_signals[FZC_SIG_MOTOR_CUTOFF]  = p->motor_cutoff;
                mock_rte_signals[FZC_SIG_LIDAR_ZONE]    = p->lidar_zone;
                mock_rte_signals[FZC_SIG_LIDAR_DIST]    = p->lidar_dist;
                mock_rte_signals[FZC_SIG_LIDAR_SIGNAL]  = p->lidar_signal;
                reset_com_signals();
                g_dbg_steer_rte_dispatch = 0u;
                g_dbg_steer_com_send = 0u;
                for (i = 0u; i < p->cycles; i++) {
                    Swc_FzcCom_TransmitSchedule();
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"tx\"}", (rb > 0u) ? "," : "");
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
           "\"canmonNotify\":%u,\"rteDispatch\":%u,\"steerComSend\":%u,"
           "\"hbEcuId\":%u,\"hbOpMode\":%u,\"hbFaultStatus\":%u,"
           "\"steerAngle\":%u,\"steerFault\":%u,\"brakePos\":%u,"
           "\"brakeFaultType\":%u,\"motorCutoffReq\":%u,"
           "\"lidarZone\":%u,\"lidarRange\":%u,\"lidarSignal\":%u}\n",
           results_buf,
           (unsigned)mock_canmon_notify_count,
           (unsigned)g_dbg_steer_rte_dispatch,
           (unsigned)g_dbg_steer_com_send,
           (unsigned)mock_com_signals[FZC_COM_SIG_FZC_HEARTBEAT_ECU_ID],
           (unsigned)mock_com_signals[FZC_COM_SIG_FZC_HEARTBEAT_OPERATING_MODE],
           (unsigned)mock_com_signals[FZC_COM_SIG_FZC_HEARTBEAT_FAULT_STATUS],
           (unsigned)mock_com_signals[FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE],
           (unsigned)mock_com_signals[FZC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS],
           (unsigned)mock_com_signals[FZC_COM_SIG_BRAKE_STATUS_BRAKE_POSITION],
           (unsigned)mock_com_signals[FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE],
           (unsigned)mock_com_signals[FZC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE],
           (unsigned)mock_com_signals[FZC_COM_SIG_LIDAR_DISTANCE_OBSTACLE_ZONE],
           (unsigned)mock_com_signals[FZC_COM_SIG_LIDAR_DISTANCE_RANGE_CM],
           (unsigned)mock_com_signals[FZC_COM_SIG_LIDAR_DISTANCE_SIGNAL_STRENGTH]);

    return 0;
}
