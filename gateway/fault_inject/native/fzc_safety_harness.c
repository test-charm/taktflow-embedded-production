/**
 * @file    fzc_safety_harness.c
 * @brief   Native test harness for Swc_FzcSafety (FZC local safety SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_FzcSafety.c and drives its
 *          Init / MainFunction / GetStatus APIs through a phase script
 *          read from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens. The harness pins the RTE fault signals
 *          (steer/brake/lidar), vehicle state and self-test result, plus
 *          the Com RX PDU quality of the CVC Steer/Brake_Command PDUs,
 *          and exposes the SWC's internal safety state (initialized flag,
 *          grace counter, self-test-done flag, WDI toggle) via the
 *          UNIT_TEST-only observation getters.
 *
 *          Phase keys:
 *            cycles        uint32   (default 1) Swc_FzcSafety_MainFunction calls
 *            skipInit      0|1      skip Swc_FzcSafety_Init (uninitialized guard)
 *            reinit        0|1      call Swc_FzcSafety_Init again at phase start
 *                                    (double-Init state reset)
 *            steerFault    uint32   RTE FZC_SIG_STEER_FAULT input (0 = no fault)
 *            brakeFault    uint32   RTE FZC_SIG_BRAKE_FAULT input (0 = no fault)
 *            lidarFault    uint32   RTE FZC_SIG_LIDAR_FAULT input (0 = no fault)
 *            vehicleState  uint32   RTE FZC_SIG_VEHICLE_STATE input
 *                                   (0=INIT 1=RUN 2=DEGRADED 3=LIMP
 *                                    4=SAFE_STOP 5=SHUTDOWN)
 *            selfTestResult uint32  RTE FZC_SIG_SELF_TEST_RESULT input
 *                                   (1=PASS 0=FAIL)
 *            selfTestDone  0|1      set Safety_SelfTestDone (self-test completed)
 *            steerCmdQuality uint32 Com_GetRxPduQuality(FZC_COM_RX_STEER_COMMAND)
 *                                   (0=FRESH 2=TIMED_OUT)
 *            brakeCmdQuality uint32 Com_GetRxPduQuality(FZC_COM_RX_BRAKE_COMMAND)
 *
 *          Output is a single JSON object on stdout:
 *            {"status":0,"initialized":1,"graceCounter":1500,"selfTestDone":0,
 *             "wdiToggle":0,"faultMask":0,"safetyStatus":0,"motorCutoff":0,
 *             "dtcReported":0,"comQueries":0,"wdiWrites":0}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Fzc_App.h"
#include "Swc_FzcSafety.h"
#include "Rte.h"
#include "Com.h"
#include "Dio.h"
#include "Dem.h"

#define MOCK_RTE_MAX_SIGNALS 256u

/* FZC_COM_RX_* PDU range 0..36 (Fzc_Cfg.h) */
#define MOCK_COM_MAX_RX_PDUS  37u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8_t  mock_dio_last_channel;
static uint8_t  mock_dio_last_level;
static uint32_t mock_dio_write_count;
static uint8_t  mock_dtc_reported;   /* Dem_ReportErrorStatus(FZC_DTC_WATCHDOG_FAIL, FAILED) count */
static uint32_t mock_com_query_count; /* Com_GetRxPduQuality call count */

/* Mocked Com RX PDU quality, per-PDU */
static uint8_t mock_com_pdu_quality[MOCK_COM_MAX_RX_PDUS];

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

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    mock_dio_last_channel = ChannelId;
    mock_dio_last_level   = Level;
    mock_dio_write_count++;
}

Com_SignalQualityType Com_GetRxPduQuality(PduIdType RxPduId)
{
    mock_com_query_count++;
    if (RxPduId < MOCK_COM_MAX_RX_PDUS) {
        return (Com_SignalQualityType)mock_com_pdu_quality[RxPduId];
    }
    return COM_SIGNAL_QUALITY_FRESH;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if ((EventId == FZC_DTC_WATCHDOG_FAIL) && (EventStatus == DEM_EVENT_STATUS_FAILED)) {
        mock_dtc_reported++;
    }
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 0);
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  reinit;
    uint32_t steer_fault;
    uint32_t brake_fault;
    uint32_t lidar_fault;
    uint32_t vehicle_state;
    uint32_t self_test_result;
    uint8_t  self_test_done;
    uint32_t steer_cmd_quality;
    uint32_t brake_cmd_quality;
} Phase;

static int run_phase(const Phase* p)
{
    uint32_t i;

    if (p->reinit != 0u) {
        Swc_FzcSafety_Init();
    }

    /* Pin the RTE fault/state inputs read at the top of MainFunction */
    mock_rte_signals[FZC_SIG_STEER_FAULT]    = p->steer_fault;
    mock_rte_signals[FZC_SIG_BRAKE_FAULT]    = p->brake_fault;
    mock_rte_signals[FZC_SIG_LIDAR_FAULT]    = p->lidar_fault;
    mock_rte_signals[FZC_SIG_VEHICLE_STATE]  = p->vehicle_state;
    mock_rte_signals[FZC_SIG_SELF_TEST_RESULT] = p->self_test_result;

    /* Pin Com RX PDU quality */
    mock_com_pdu_quality[FZC_COM_RX_STEER_COMMAND] = (uint8_t)p->steer_cmd_quality;
    mock_com_pdu_quality[FZC_COM_RX_BRAKE_COMMAND] = (uint8_t)p->brake_cmd_quality;

    /* Inject self-test-done flag (drives the self-test-fail branches) */
    Swc_FzcSafety_SetSelfTestDone(p->self_test_done);

    for (i = 0u; i < p->cycles; i++) {
        Swc_FzcSafety_MainFunction();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    char line[1024];
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    /* Defaults */
    memset(mock_com_pdu_quality, COM_SIGNAL_QUALITY_FRESH, sizeof(mock_com_pdu_quality));
    mock_rte_signals[FZC_SIG_VEHICLE_STATE] = FZC_STATE_RUN;

    /* ---- parse all phases first (skipInit is decided on phase[0]) ---- */
    while (fgets(line, sizeof(line), stdin) != NULL) {
        Phase p;
        char* token;
        char* saveptr = NULL;

        if (phase_count >= sizeof(phases) / sizeof(phases[0])) {
            fprintf(stderr, "too many phases (max 64)\n");
            return 2;
        }

        memset(&p, 0, sizeof(p));
        p.cycles = 1u;
        p.vehicle_state = FZC_STATE_RUN;
        p.self_test_result = FZC_SELF_TEST_PASS;
        p.steer_cmd_quality = COM_SIGNAL_QUALITY_FRESH;
        p.brake_cmd_quality = COM_SIGNAL_QUALITY_FRESH;

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    uint32_t val = parse_uint(eq + 1);
                    if (strcmp(key, "cycles") == 0)        p.cycles = val;
                    else if (strcmp(key, "skipInit") == 0) p.skip_init = (uint8_t)val;
                    else if (strcmp(key, "reinit") == 0)   p.reinit = (uint8_t)val;
                    else if (strcmp(key, "steerFault") == 0) p.steer_fault = val;
                    else if (strcmp(key, "brakeFault") == 0) p.brake_fault = val;
                    else if (strcmp(key, "lidarFault") == 0) p.lidar_fault = val;
                    else if (strcmp(key, "vehicleState") == 0) p.vehicle_state = val;
                    else if (strcmp(key, "selfTestResult") == 0) p.self_test_result = val;
                    else if (strcmp(key, "selfTestDone") == 0) p.self_test_done = (uint8_t)val;
                    else if (strcmp(key, "steerCmdQuality") == 0) p.steer_cmd_quality = val;
                    else if (strcmp(key, "brakeCmdQuality") == 0) p.brake_cmd_quality = val;
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guard in MainFunction). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_FzcSafety_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    printf("{\"status\":%u,\"initialized\":%u,\"graceCounter\":%u,"
           "\"selfTestDone\":%u,\"wdiToggle\":%u,"
           "\"faultMask\":%u,\"safetyStatus\":%u,\"motorCutoff\":%u,"
           "\"dtcReported\":%u,\"comQueries\":%u,\"wdiWrites\":%u}\n",
           (unsigned)Swc_FzcSafety_GetStatus(),
           (unsigned)Swc_FzcSafety_GetInitialized(),
           (unsigned)Swc_FzcSafety_GetGraceCounter(),
           (unsigned)Swc_FzcSafety_GetSelfTestDone(),
           (unsigned)Swc_FzcSafety_GetWdiToggle(),
           (unsigned)mock_rte_signals[FZC_SIG_FAULT_MASK],
           (unsigned)mock_rte_signals[FZC_SIG_SAFETY_STATUS],
           (unsigned)mock_rte_signals[FZC_SIG_MOTOR_CUTOFF],
           (unsigned)mock_dtc_reported,
           (unsigned)mock_com_query_count,
           (unsigned)mock_dio_write_count);

    return 0;
}
