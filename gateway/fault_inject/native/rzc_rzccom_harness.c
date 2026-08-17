/**
 * @file    rzc_rzccom_harness.c
 * @brief   Native test harness for Swc_RzcCom (RZC CAN communication SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_RzcCom.c and drives its five APIs
 *          through a phase script read from stdin:
 *
 *            op=init                 -- Swc_RzcCom_Init()
 *            op=e2eProtect pduId=N data=<hex16> [len=N]
 *                                    -- Swc_RzcCom_E2eProtect (CRC-8 + alive)
 *            op=e2eCheck  pduId=N data=<hex16> [len=N]
 *                                    -- Swc_RzcCom_E2eCheck (CRC + alive verify)
 *            op=receive [estop] [vehicleState] [torqueCmd] [cycles]
 *                                    -- Swc_RzcCom_Receive() N times
 *            op=tx [cycles] [RTE inputs...]
 *                                    -- Swc_RzcCom_TransmitSchedule() N times
 *
 *          Phase keys (all optional except op):
 *            cycles      uint32 (default 1) cyclic call count
 *            skipInit    0|1    skip Swc_RzcCom_Init (uninitialized guard)
 *            pduId       uint32 E2E PDU index
 *            data        hex    E2E 8-byte payload (hex, e.g. 0011223344556677)
 *                               special value "null" passes NULL_PTR
 *            len         uint32 E2E payload length (default 8)
 *            estop       uint32 RTE RZC_SIG_ESTOP_ACTIVE (receive input)
 *            vehicleState uint32 RTE RZC_SIG_VEHICLE_STATE
 *            torqueCmd   int32  RTE RZC_SIG_TORQUE_CMD
 *            faultMask   uint32 RTE RZC_SIG_FAULT_MASK (tx input)
 *            torqueEcho  uint32 RTE RZC_SIG_TORQUE_ECHO
 *            speedRpm    uint32 RTE RZC_SIG_ENCODER_SPEED
 *            motorDir    uint32 RTE RZC_SIG_MOTOR_DIR
 *            motorEnable uint32 RTE RZC_SIG_MOTOR_ENABLE
 *            motorFault  uint32 RTE RZC_SIG_MOTOR_FAULT
 *            currentMa   uint32 RTE RZC_SIG_CURRENT_MA
 *            overcurrent uint32 RTE RZC_SIG_OVERCURRENT
 *            temp1Dc     uint32 RTE RZC_SIG_TEMP1_DC
 *            temp2Dc     uint32 RTE RZC_SIG_TEMP2_DC
 *            deratingPct uint32 RTE RZC_SIG_DERATING_PCT
 *            batteryMv   uint32 RTE RZC_SIG_BATTERY_MV
 *            batteryStatus uint32 RTE RZC_SIG_BATTERY_STATUS
 *
 *          Output is a single JSON object on stdout:
 *            {"results":[<per-op result>...],
 *             "torqueCmd":..,"estopActive":..,"demBusOff":..,
 *             "hbEcuId":..,"hbFaultStatus":.., ...tx Com signals...}
 *          Per-op results:
 *            init:       {"op":"init"}
 *            e2eProtect: {"op":"e2eProtect","ret":0|1,"data":"<hex16>"}
 *            e2eCheck:   {"op":"e2eCheck","ret":0|1}
 *            receive:    {"op":"receive","torqueCmd":..,"estopActive":..,"demBusOff":..}
 *            tx:         {"op":"tx"}
 *          demBusOff: 1=FAILED reported on RZC_DTC_CAN_BUS_OFF, -1=not reported.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_RzcCom.h"
#include "Com.h"
#include "Rte.h"
#include "Dem.h"
#include "Swc_RzcSafety.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_COM_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS 16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS];   /* per DTC, -1 = not reported */

/* ==================================================================
 * BSW stubs
 * ================================================================== */

Std_ReturnType Rte_Write(Rte_SignalIdType SignalId, uint32 Data)
{
    if (SignalId >= MOCK_RTE_MAX_SIGNALS) {
        return E_NOT_OK;
    }
    mock_rte_signals[SignalId] = Data;
    return E_OK;
}

Std_ReturnType Rte_Read(Rte_SignalIdType SignalId, uint32* DataPtr)
{
    if ((DataPtr == NULL_PTR) || (SignalId >= MOCK_RTE_MAX_SIGNALS)) {
        return E_NOT_OK;
    }
    *DataPtr = mock_rte_signals[SignalId];
    return E_OK;
}

static uint8 com_signal_is_u16(Com_SignalIdType SignalId)
{
    switch (SignalId) {
        case RZC_COM_SIG_MOTOR_STATUS_MOTOR_SPEED_RPM:
        case RZC_COM_SIG_MOTOR_CURRENT_PHASE_M_A:
        case RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C:
        case RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C:
        case RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V:
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
    if (com_signal_is_u16(SignalId)) {
        mock_com_signals[SignalId] = (uint32)(*(const uint16*)SignalDataPtr);
    } else {
        mock_com_signals[SignalId] = (uint32)(*(const uint8*)SignalDataPtr);
    }
    return E_OK;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if (EventId < MOCK_DEM_MAX_EVENTS) {
        mock_dem_status[EventId] = (int8_t)EventStatus;
    }
}

void Swc_RzcSafety_NotifyCanRx(void)
{
    /* no-op: the safety module is not linked in this harness */
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

static void reset_state(void)
{
    uint16_t i;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    for (i = 0u; i < MOCK_COM_MAX_SIGNALS; i++) {
        mock_com_signals[i] = 0u;
    }
    for (i = 0u; i < MOCK_DEM_MAX_EVENTS; i++) {
        mock_dem_status[i] = (int8_t)-1;
    }
}

typedef struct {
    uint8_t  op;                 /* OP_* enum below */
    uint8_t  skip_init;
    uint32_t repeats;            /* repeat count for e2eProtect / e2eCheck */
    uint32_t pdu_id;
    uint8_t  data[8];
    uint8_t  data_len;
    uint8_t  data_is_null;
    uint32_t cycles;
    uint32_t estop;
    uint32_t vehicle_state;
    int32_t  torque_cmd;
    uint32_t fault_mask;
    uint32_t torque_echo;
    uint32_t speed_rpm;
    uint32_t motor_dir;
    uint32_t motor_enable;
    uint32_t motor_fault;
    uint32_t current_ma;
    uint32_t overcurrent;
    uint32_t temp1_dc;
    uint32_t temp2_dc;
    uint32_t derating_pct;
    uint32_t battery_mv;
    uint32_t battery_status;
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
    p->cycles       = 1u;
    p->repeats      = 1u;
    p->data_len     = 8u;
    p->vehicle_state = 1u;
    p->derating_pct = 100u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    const char* val = value;
    if (strcmp(key, "op") == 0)          p->op = (uint8_t)parse_op(val);
    else if (strcmp(key, "pduId") == 0)  p->pdu_id = harness_parse_uint(val);
    else if (strcmp(key, "cycles") == 0) p->cycles = harness_parse_uint(val);
    else if (strcmp(key, "repeats") == 0) p->repeats = harness_parse_uint(val);
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)harness_parse_uint(val);
    else if (strcmp(key, "len") == 0)    p->data_len = (uint8_t)harness_parse_uint(val);
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
    else if (strcmp(key, "estop") == 0)       p->estop = harness_parse_uint(val);
    else if (strcmp(key, "vehicleState") == 0) p->vehicle_state = harness_parse_uint(val);
    else if (strcmp(key, "torqueCmd") == 0)   p->torque_cmd = harness_parse_int(val);
    else if (strcmp(key, "faultMask") == 0)   p->fault_mask = harness_parse_uint(val);
    else if (strcmp(key, "torqueEcho") == 0)  p->torque_echo = harness_parse_uint(val);
    else if (strcmp(key, "speedRpm") == 0)    p->speed_rpm = harness_parse_uint(val);
    else if (strcmp(key, "motorDir") == 0)    p->motor_dir = harness_parse_uint(val);
    else if (strcmp(key, "motorEnable") == 0) p->motor_enable = harness_parse_uint(val);
    else if (strcmp(key, "motorFault") == 0)  p->motor_fault = harness_parse_uint(val);
    else if (strcmp(key, "currentMa") == 0)   p->current_ma = harness_parse_uint(val);
    else if (strcmp(key, "overcurrent") == 0) p->overcurrent = harness_parse_uint(val);
    else if (strcmp(key, "temp1Dc") == 0)     p->temp1_dc = harness_parse_uint(val);
    else if (strcmp(key, "temp2Dc") == 0)     p->temp2_dc = harness_parse_uint(val);
    else if (strcmp(key, "deratingPct") == 0) p->derating_pct = harness_parse_uint(val);
    else if (strcmp(key, "batteryMv") == 0)   p->battery_mv = harness_parse_uint(val);
    else if (strcmp(key, "batteryStatus") == 0) p->battery_status = harness_parse_uint(val);
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

    reset_state();

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
        Swc_RzcCom_Init();
    }

    results_buf[0] = '\0';

    for (pi = 0u; pi < phase_count; pi++) {
        const Phase* p = &phases[pi];
        int n;

        switch (p->op) {
            case OP_INIT:
                Swc_RzcCom_Init();
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
                    ret = Swc_RzcCom_E2eProtect((uint8)p->pdu_id,
                                                p->data_is_null ? NULL_PTR : buf,
                                                p->data_is_null ? p->data_len : p->data_len);
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
                    ret = Swc_RzcCom_E2eCheck(
                        (uint8)p->pdu_id,
                        p->data_is_null ? NULL_PTR : p->data,
                        p->data_is_null ? 0u : p->data_len);
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"e2eCheck\",\"ret\":%d}",
                             (rb > 0u) ? "," : "",
                             (ret == E_OK) ? 0 : 1);
                break;
            }

            case OP_RECEIVE: {
                uint32_t i;
                mock_rte_signals[RZC_SIG_ESTOP_ACTIVE] = p->estop;
                mock_rte_signals[RZC_SIG_VEHICLE_STATE] = p->vehicle_state;
                mock_rte_signals[RZC_SIG_TORQUE_CMD] = (uint32_t)p->torque_cmd;
                for (i = 0u; i < p->cycles; i++) {
                    Swc_RzcCom_Receive();
                }
                n = snprintf(results_buf + rb, sizeof(results_buf) - rb,
                             "%s{\"op\":\"receive\",\"torqueCmd\":%u,\"estopActive\":%u,\"demBusOff\":%d}",
                             (rb > 0u) ? "," : "",
                             (unsigned)mock_rte_signals[RZC_SIG_TORQUE_CMD],
                             (unsigned)mock_rte_signals[RZC_SIG_ESTOP_ACTIVE],
                             (int)mock_dem_status[RZC_DTC_CAN_BUS_OFF]);
                break;
            }

            case OP_TX: {
                uint32_t i;
                mock_rte_signals[RZC_SIG_VEHICLE_STATE]  = p->vehicle_state;
                mock_rte_signals[RZC_SIG_FAULT_MASK]     = p->fault_mask;
                mock_rte_signals[RZC_SIG_TORQUE_ECHO]    = p->torque_echo;
                mock_rte_signals[RZC_SIG_ENCODER_SPEED]  = p->speed_rpm;
                mock_rte_signals[RZC_SIG_MOTOR_DIR]      = p->motor_dir;
                mock_rte_signals[RZC_SIG_MOTOR_ENABLE]   = p->motor_enable;
                mock_rte_signals[RZC_SIG_MOTOR_FAULT]    = p->motor_fault;
                mock_rte_signals[RZC_SIG_CURRENT_MA]     = p->current_ma;
                mock_rte_signals[RZC_SIG_OVERCURRENT]    = p->overcurrent;
                mock_rte_signals[RZC_SIG_TEMP1_DC]       = p->temp1_dc;
                mock_rte_signals[RZC_SIG_TEMP2_DC]       = p->temp2_dc;
                mock_rte_signals[RZC_SIG_DERATING_PCT]   = p->derating_pct;
                mock_rte_signals[RZC_SIG_BATTERY_MV]     = p->battery_mv;
                mock_rte_signals[RZC_SIG_BATTERY_STATUS] = p->battery_status;
                for (i = 0u; i < p->cycles; i++) {
                    Swc_RzcCom_TransmitSchedule();
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
           "\"torqueCmd\":%u,\"estopActive\":%u,\"demBusOff\":%d,"
           "\"hbEcuId\":%u,\"hbFaultStatus\":%u,"
           "\"mstatTorqueEcho\":%u,\"mstatSpeedRpm\":%u,\"mstatDirection\":%u,"
           "\"mstatEnable\":%u,\"mstatFault\":%u,"
           "\"curMa\":%u,\"curDirReverse\":%u,\"curEnable\":%u,"
           "\"curOvercurrent\":%u,\"curTorqueEcho\":%u,"
           "\"temp1C\":%u,\"temp2C\":%u,\"deratingPct\":%u,"
           "\"batteryMv\":%u,\"batteryLevel\":%u}\n",
           results_buf,
           (unsigned)mock_rte_signals[RZC_SIG_TORQUE_CMD],
           (unsigned)mock_rte_signals[RZC_SIG_ESTOP_ACTIVE],
           (int)mock_dem_status[RZC_DTC_CAN_BUS_OFF],
           (unsigned)mock_com_signals[RZC_COM_SIG_RZC_HEARTBEAT_ECU_ID],
           (unsigned)mock_com_signals[RZC_COM_SIG_RZC_HEARTBEAT_FAULT_STATUS],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_STATUS_TORQUE_ECHO],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_STATUS_MOTOR_SPEED_RPM],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_STATUS_MOTOR_DIRECTION],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_STATUS_MOTOR_ENABLE],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_CURRENT_PHASE_M_A],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_CURRENT_DIR_IS_REVERSE],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_CURRENT_MOTOR_ENABLE],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_CURRENT_OVERCURRENT_FLAG],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_CURRENT_TORQUE_ECHO],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C],
           (unsigned)mock_com_signals[RZC_COM_SIG_MOTOR_TEMPERATURE_DERATING_PERCENT],
           (unsigned)mock_com_signals[RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V],
           (unsigned)mock_com_signals[RZC_COM_SIG_BATTERY_STATUS_LEVEL]);

    return 0;
}
