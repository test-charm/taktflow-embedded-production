/**
 * @file    cvc_vehiclestate_harness.c
 * @brief   Native test harness for Swc_VehicleState (CVC vehicle state machine)
 * @date    2026-08-13
 *
 * @details Links the REAL production Swc_VehicleState.c and drives it through a
 *          phase script read from stdin. Each phase is one line of whitespace-
 *          separated key=value tokens. For each phase the harness pins the RTE
 *          fault signals, optionally injects the SELF_TEST_PASS event, then runs
 *          Swc_VehicleState_MainFunction() `cycles` times.
 *
 *          Phase keys:
 *            cycles              uint32  (default 0)
 *            selfTestPass        0|1     (inject CVC_EVT_SELF_TEST_PASS once)
 *            estop               0|1
 *            scRelayEnergized    0|1     (1 = relay energized / OK)
 *            fzcComm             0|1     (0=OK, 1=TIMEOUT)
 *            rzcComm             0|1
 *            pedalFault          0|1
 *            motorCutoff         0|1
 *            brakeFault          0|1
 *            steeringFault       0|1
 *            batteryStatus       0..4   (2=NORMAL)
 *            motorFaultRzc       0|1
 *            motorSpeed          uint32  RPM
 *            torqueRequest       uint32  percent
 *            pedalPosition       uint32
 *            pedalFaultDual      0|1     inject EVT_PEDAL_FAULT_DUAL (no prod trigger)
 *            comBrakeFault       -1|0|1  override Com shadow for brake (default -1=follow)
 *            comMotorCutoff      -1|0|1  override Com shadow for motor cutoff (default -1=follow)
 *            motorPduTimedOut    0|1     Motor_Status PDU quality = TIMED_OUT
 *
 *          Output is a single JSON object on stdout:
 *            {"vehicleState":"RUN","bswmMode":"RUN",
 *             "dtcNames":"BRAKE_FAULT_RX","stateTrace":"INIT,RUN"}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_VehicleState.h"
#include "Rte.h"
#include "BswM.h"
#include "Dem.h"
#include "Com.h"

#define MOCK_RTE_MAX_SIGNALS  256u
#define MOCK_COM_MAX_SIGNALS  256u
#define MAX_TRACE_CHARS       512u
#define MAX_DTC_NAMES         32u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];

/* ---- captures ---- */
static uint8_t  captured_bswm_mode = 0xFFu;
static uint8_t  bswm_called = 0u;

static uint8_t  dtc_seen[64];
static char     dtc_names[MAX_DTC_NAMES][24];
static int      dtc_count = 0;

static char     state_trace[MAX_TRACE_CHARS];
static int      state_trace_len = 0;
static uint8_t  last_traced_state = 0xFFu;

/* ==================================================================
 * Name maps
 * ================================================================== */

static const char* state_name(uint8_t s)
{
    switch (s) {
        case CVC_STATE_INIT:      return "INIT";
        case CVC_STATE_RUN:       return "RUN";
        case CVC_STATE_DEGRADED:  return "DEGRADED";
        case CVC_STATE_LIMP:      return "LIMP";
        case CVC_STATE_SAFE_STOP: return "SAFE_STOP";
        case CVC_STATE_SHUTDOWN:  return "SHUTDOWN";
        default:                  return "UNKNOWN";
    }
}

static const char* bswm_name(uint8_t m)
{
    switch (m) {
        case 0u: return "STARTUP";
        case 1u: return "RUN";
        case 2u: return "DEGRADED";
        case 3u: return "SAFE_STOP";
        case 4u: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}

static const char* dtc_name(uint8_t id)
{
    switch (id) {
        case 8u:  return "MOTOR_OVERCURRENT";
        case 10u: return "BATT_UNDERVOLT";
        case 20u: return "CREEP_FAULT";
        case 24u: return "MOTOR_CUTOFF_RX";
        case 25u: return "BRAKE_FAULT_RX";
        case 26u: return "STEERING_FAULT_RX";
        default:  return "UNKNOWN_DTC";
    }
}

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

Std_ReturnType Rte_Write(Rte_SignalIdType SignalId, uint32 Data)
{
    if (SignalId >= MOCK_RTE_MAX_SIGNALS) {
        return E_NOT_OK;
    }
    mock_rte_signals[SignalId] = Data;
    return E_OK;
}

Std_ReturnType BswM_RequestMode(BswM_RequesterIdType RequesterId, BswM_ModeType RequestedMode)
{
    (void)RequesterId;
    captured_bswm_mode = (uint8_t)RequestedMode;
    bswm_called = 1u;
    return E_OK;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if ((EventStatus == DEM_EVENT_STATUS_FAILED) && (EventId < 64u)) {
        if ((dtc_seen[EventId] == 0u) && (dtc_count < (int)MAX_DTC_NAMES)) {
            dtc_seen[EventId] = 1u;
            (void)strncpy(dtc_names[dtc_count], dtc_name(EventId), 23);
            dtc_names[dtc_count][23] = '\0';
            dtc_count++;
        }
    }
}

Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    *(uint8_t*)SignalDataPtr = (uint8_t)mock_com_signals[SignalId];
    return E_OK;
}

static uint8_t mock_motor_pdu_timed_out = 0u;

Com_SignalQualityType Com_GetRxPduQuality(PduIdType RxPduId)
{
    if ((RxPduId == CVC_COM_RX_MOTOR_STATUS) && (mock_motor_pdu_timed_out != 0u)) {
        return COM_SIGNAL_QUALITY_TIMED_OUT;
    }
    return COM_SIGNAL_QUALITY_FRESH;
}

void Swc_Heartbeat_ResetCommStatus(void)
{
    /* no-op in harness — comm status is driven directly by phase script */
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  self_test_pass;
    uint32_t estop;
    uint32_t sc_relay_energized;
    uint32_t fzc_comm;
    uint32_t rzc_comm;
    uint32_t pedal_fault;
    uint32_t motor_cutoff;
    uint32_t brake_fault;
    uint32_t steering_fault;
    uint32_t battery_status;
    uint32_t motor_fault_rzc;
    uint32_t motor_speed;
    uint32_t torque_request;
    uint32_t pedal_position;
    uint8_t  pedal_fault_dual;    /* inject EVT_PEDAL_FAULT_DUAL (no production trigger) */
    int32_t  com_brake_fault;     /* -1 = follow RTE brake_fault, 0/1 = override Com shadow */
    int32_t  com_motor_cutoff;    /* -1 = follow RTE motor_cutoff, 0/1 = override Com shadow */
    uint8_t  motor_pdu_timed_out; /* Com_GetRxPduQuality(MOTOR_STATUS) = TIMED_OUT */
} Phase;

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 10);
}

static int32_t parse_int(const char* s)
{
    return (int32_t)strtol(s, NULL, 10);
}

static void apply_phase(const Phase* p)
{
    mock_rte_signals[CVC_SIG_PEDAL_FAULT]     = p->pedal_fault;
    mock_rte_signals[CVC_SIG_ESTOP_ACTIVE]    = p->estop;
    mock_rte_signals[CVC_SIG_FZC_COMM_STATUS] = p->fzc_comm;
    mock_rte_signals[CVC_SIG_RZC_COMM_STATUS] = p->rzc_comm;
    mock_rte_signals[CVC_SIG_MOTOR_CUTOFF]    = p->motor_cutoff;
    mock_rte_signals[CVC_SIG_BRAKE_FAULT]     = p->brake_fault;
    mock_rte_signals[CVC_SIG_STEERING_FAULT]  = p->steering_fault;
    mock_rte_signals[CVC_SIG_SC_RELAY_KILL]   = p->sc_relay_energized;
    mock_rte_signals[CVC_SIG_BATTERY_STATUS]  = p->battery_status;
    mock_rte_signals[CVC_SIG_MOTOR_FAULT_RZC] = p->motor_fault_rzc;
    mock_rte_signals[CVC_SIG_MOTOR_SPEED]     = p->motor_speed;
    mock_rte_signals[CVC_SIG_TORQUE_REQUEST]  = p->torque_request;
    mock_rte_signals[CVC_SIG_PEDAL_POSITION]  = p->pedal_position;

    /* Mirror the CAN-origin faults into the Com shadow used by the
     * ISO 26262 confirmation-read in Swc_VehicleState_ConfirmFault().
     * comBrakeFault/comMotorCutoff override the shadow (to exercise the
     * "Com disagrees" branch); -1 (default) follows the RTE signal. */
    mock_com_signals[CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS] =
        (p->com_brake_fault >= 0) ? (uint32_t)p->com_brake_fault : p->brake_fault;
    mock_com_signals[CVC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE] =
        (p->com_motor_cutoff >= 0) ? (uint32_t)p->com_motor_cutoff : p->motor_cutoff;

    /* Motor_Status PDU timeout: Com zeroes motor_fault_rzc on timeout, and
     * Swc_VehicleState treats a timed-out signal as fault (fail-closed).
     * Simulate the Com timeout default = fault present so the confirmation
     * read is consistent with the forced RTE value. */
    mock_motor_pdu_timed_out = p->motor_pdu_timed_out;
    if (p->motor_pdu_timed_out != 0u) {
        mock_com_signals[CVC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS] = 1u;
    }
}

static void record_state(void)
{
    uint8_t s = Swc_VehicleState_GetState();
    if (s != last_traced_state) {
        const char* name = state_name(s);
        int need_comma = (state_trace_len > 0) ? 1 : 0;
        if (state_trace_len + need_comma + (int)strlen(name) < (int)MAX_TRACE_CHARS) {
            if (need_comma) {
                state_trace[state_trace_len++] = ',';
            }
            (void)strcpy(&state_trace[state_trace_len], name);
            state_trace_len += (int)strlen(name);
        }
        last_traced_state = s;
    }
}

/* ==================================================================
 * Phase script loop
 * ================================================================== */

static int run_phase(const Phase* p)
{
    uint32_t i;

    apply_phase(p);

    if (p->self_test_pass != 0u) {
        Swc_VehicleState_OnEvent(CVC_EVT_SELF_TEST_PASS);
    }

    /* Dual pedal fault has no production trigger in MainFunction (only the
     * single pedal fault is derived). Inject it directly, mirroring how
     * Swc_Pedal would report a dual-sensor plausibility fault. */
    if (p->pedal_fault_dual != 0u) {
        Swc_VehicleState_OnEvent(CVC_EVT_PEDAL_FAULT_DUAL);
    }

    for (i = 0u; i < p->cycles; i++) {
        Swc_VehicleState_MainFunction();
        record_state();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    char line[2048];

    Swc_VehicleState_Init();
    record_state();

    while (fgets(line, sizeof(line), stdin) != NULL) {
        Phase p;
        char* token;
        char* saveptr = NULL;

        memset(&p, 0, sizeof(p));
        /* defaults matching production pre-read expectations */
        p.sc_relay_energized = 1u;
        p.fzc_comm           = CVC_COMM_OK;
        p.rzc_comm           = CVC_COMM_OK;
        p.battery_status     = 2u;   /* NORMAL */
        p.motor_fault_rzc    = 0u;
        p.com_brake_fault    = -1;   /* follow RTE brake_fault */
        p.com_motor_cutoff   = -1;   /* follow RTE motor_cutoff */

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    uint32_t val = parse_uint(eq + 1);
                    if (strcmp(key, "cycles") == 0)          p.cycles = val;
                    else if (strcmp(key, "selfTestPass") == 0) p.self_test_pass = (uint8_t)val;
                    else if (strcmp(key, "estop") == 0)        p.estop = val;
                    else if (strcmp(key, "scRelayEnergized") == 0) p.sc_relay_energized = val;
                    else if (strcmp(key, "fzcComm") == 0)      p.fzc_comm = val;
                    else if (strcmp(key, "rzcComm") == 0)      p.rzc_comm = val;
                    else if (strcmp(key, "pedalFault") == 0)   p.pedal_fault = val;
                    else if (strcmp(key, "motorCutoff") == 0)  p.motor_cutoff = val;
                    else if (strcmp(key, "brakeFault") == 0)   p.brake_fault = val;
                    else if (strcmp(key, "steeringFault") == 0) p.steering_fault = val;
                    else if (strcmp(key, "batteryStatus") == 0) p.battery_status = val;
                    else if (strcmp(key, "motorFaultRzc") == 0) p.motor_fault_rzc = val;
                    else if (strcmp(key, "motorSpeed") == 0)    p.motor_speed = val;
                    else if (strcmp(key, "torqueRequest") == 0) p.torque_request = val;
                    else if (strcmp(key, "pedalPosition") == 0) p.pedal_position = val;
                    else if (strcmp(key, "pedalFaultDual") == 0) p.pedal_fault_dual = (uint8_t)val;
                    else if (strcmp(key, "comBrakeFault") == 0) p.com_brake_fault = parse_int(eq + 1);
                    else if (strcmp(key, "comMotorCutoff") == 0) p.com_motor_cutoff = parse_int(eq + 1);
                    else if (strcmp(key, "motorPduTimedOut") == 0) p.motor_pdu_timed_out = (uint8_t)val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        if (run_phase(&p) != 0) {
            return 2;
        }
    }

    /* ---- serialize result ---- */
    {
        uint8_t state = Swc_VehicleState_GetState();
        char dts[256];
        int i;
        int pos = 0;

        dts[0] = '\0';
        for (i = 0; i < dtc_count; i++) {
            if (i > 0) {
                pos += snprintf(&dts[pos], sizeof(dts) - (size_t)pos, ",");
            }
            pos += snprintf(&dts[pos], sizeof(dts) - (size_t)pos, "%s", dtc_names[i]);
        }

        printf("{\"vehicleState\":\"%s\",\"bswmMode\":\"%s\",\"dtcNames\":\"%s\",\"stateTrace\":\"%s\"}\n",
               state_name(state),
               bswm_called ? bswm_name(captured_bswm_mode) : "UNKNOWN",
               dts,
               state_trace_len > 0 ? state_trace : "");
    }

    return 0;
}
