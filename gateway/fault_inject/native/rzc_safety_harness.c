/**
 * @file    rzc_safety_harness.c
 * @brief   Native test harness for Swc_RzcSafety (RZC local safety SWC)
 * @date    2026-08-18
 *
 * @details Links the REAL production Swc_RzcSafety.c and drives its
 *          Init / MainFunction / GetStatus / NotifyCanRx APIs through a
 *          phase script read from stdin. Each phase is one line of
 *          whitespace-separated key=value tokens. The harness pins the RTE
 *          fault / state signals (overcurrent, overtemp, direction, stall,
 *          battery, self-test result, e-stop, vehicle state) and the CAN
 *          controller error state, and observes the SWC's external behavior
 *          (fault mask / safety status RTE outputs, WDI toggle writes,
 *          motor disable writes, WATCHDOG_FAIL DTC reports).
 *
 *          All internal state is observed through the public API and the
 *          mocked BSW interfaces — NO production-code getters are needed.
 *
 *          Phase keys:
 *            cycles        uint32   (default 1) Swc_RzcSafety_MainFunction calls
 *            skipInit      0|1      skip Swc_RzcSafety_Init (uninitialized guard)
 *            reinit        0|1      call Swc_RzcSafety_Init again at phase start
 *                                    (double-Init state reset)
 *            overcurrent   0|1      RTE RZC_SIG_OVERCURRENT input
 *            overtemp      0|1      RTE RZC_SIG_TEMP_FAULT input
 *            directionFault 0|1     RTE RZC_SIG_ENCODER_DIR input
 *            stallFault    0|1      RTE RZC_SIG_ENCODER_STALL input
 *            batteryFault  0|1      RTE RZC_SIG_BATTERY_STATUS input
 *            selfTestResult uint32  RTE RZC_SIG_SELF_TEST_RESULT input
 *                                   (1=PASS 0=FAIL)
 *            estopActive   0|1      RTE RZC_SIG_ESTOP_ACTIVE input
 *            vehicleState  uint32   RTE RZC_SIG_VEHICLE_STATE input
 *                                   (0=INIT 1=RUN 2=DEGRADED 3=LIMP
 *                                    4=SAFE_STOP 5=SHUTDOWN)
 *            canErrorState uint32   Can_GetControllerErrorState(0)
 *                                   (0=ACTIVE 1=WARNING 2=BUSOFF)
 *            notifyCanRx   0|1      call Swc_RzcSafety_NotifyCanRx before each
 *                                   MainFunction call in this phase (resets the
 *                                   CAN silence counter)
 *
 *          Output is a single JSON object on stdout:
 *            {"status":0,"faultMask":0,"safetyStatus":0,"wdiWrites":0,
 *             "wdiLevel":0,"dioCh5":0,"dioCh6":0,"dioWrites":0,
 *             "demWatchdog":0,"demWatchdogStatus":-1}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Rzc_App.h"
#include "Swc_RzcSafety.h"
#include "Rte.h"
#include "Can.h"
#include "Dio.h"
#include "Dem.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DIO_MAX_CHANNELS 16u
#define MOCK_DEM_MAX_EVENTS  16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8_t  mock_dio_level[MOCK_DIO_MAX_CHANNELS];
static uint32_t mock_dio_write_count = 0u;
static uint32_t mock_dio_wdi_write_count = 0u;   /* writes to WDI channel */
static uint8_t  mock_can_error_state = 0u;       /* 0=ACTIVE 1=WARNING 2=BUSOFF */
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS];
static uint32_t mock_dem_count[MOCK_DEM_MAX_EVENTS];

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

Std_ReturnType Can_GetControllerErrorState(uint8 ControllerId, uint8* ErrorStatePtr)
{
    if ((ControllerId != 0u) || (ErrorStatePtr == NULL_PTR)) {
        return E_NOT_OK;
    }
    *ErrorStatePtr = mock_can_error_state;
    return E_OK;
}

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    mock_dio_write_count++;
    if (ChannelId == RZC_SAFETY_WDI_CHANNEL) {
        mock_dio_wdi_write_count++;
    }
    if (ChannelId < MOCK_DIO_MAX_CHANNELS) {
        mock_dio_level[ChannelId] = Level;
    }
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if (EventId < MOCK_DEM_MAX_EVENTS) {
        mock_dem_status[EventId] = (int8_t)EventStatus;
        mock_dem_count[EventId]++;
    }
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  reinit;
    uint32_t overcurrent;
    uint32_t overtemp;
    uint32_t direction_fault;
    uint32_t stall_fault;
    uint32_t battery_fault;
    uint32_t self_test_result;
    uint32_t estop_active;
    uint32_t vehicle_state;
    uint32_t can_error_state;
    uint8_t  notify_can_rx;
} Phase;

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles = 1u;
    p->vehicle_state = RZC_STATE_RUN;
    p->self_test_result = RZC_SELF_TEST_PASS;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "cycles") == 0)          p->cycles = val;
    else if (strcmp(key, "skipInit") == 0)   p->skip_init = (uint8_t)val;
    else if (strcmp(key, "reinit") == 0)     p->reinit = (uint8_t)val;
    else if (strcmp(key, "overcurrent") == 0) p->overcurrent = val;
    else if (strcmp(key, "overtemp") == 0)   p->overtemp = val;
    else if (strcmp(key, "directionFault") == 0) p->direction_fault = val;
    else if (strcmp(key, "stallFault") == 0) p->stall_fault = val;
    else if (strcmp(key, "batteryFault") == 0) p->battery_fault = val;
    else if (strcmp(key, "selfTestResult") == 0) p->self_test_result = val;
    else if (strcmp(key, "estopActive") == 0) p->estop_active = val;
    else if (strcmp(key, "vehicleState") == 0) p->vehicle_state = val;
    else if (strcmp(key, "canErrorState") == 0) p->can_error_state = val;
    else if (strcmp(key, "notifyCanRx") == 0) p->notify_can_rx = (uint8_t)val;
    return 0;
}

static void reset_state(void)
{
    uint32_t i;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    for (i = 0u; i < MOCK_DIO_MAX_CHANNELS; i++) {
        mock_dio_level[i] = 0u;
    }
    for (i = 0u; i < MOCK_DEM_MAX_EVENTS; i++) {
        mock_dem_status[i] = (int8_t)-1;
        mock_dem_count[i] = 0u;
    }
    mock_dio_write_count = 0u;
    mock_dio_wdi_write_count = 0u;
    mock_can_error_state = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    if (p->reinit != 0u) {
        Swc_RzcSafety_Init();
    }

    /* Pin the RTE fault / state inputs read at the top of MainFunction */
    mock_rte_signals[RZC_SIG_OVERCURRENT]     = p->overcurrent;
    mock_rte_signals[RZC_SIG_TEMP_FAULT]      = p->overtemp;
    mock_rte_signals[RZC_SIG_ENCODER_DIR]     = p->direction_fault;
    mock_rte_signals[RZC_SIG_ENCODER_STALL]   = p->stall_fault;
    mock_rte_signals[RZC_SIG_BATTERY_STATUS]  = p->battery_fault;
    mock_rte_signals[RZC_SIG_SELF_TEST_RESULT] = p->self_test_result;
    mock_rte_signals[RZC_SIG_ESTOP_ACTIVE]    = p->estop_active;
    mock_rte_signals[RZC_SIG_VEHICLE_STATE]   = p->vehicle_state;

    /* Pin the CAN controller error state */
    mock_can_error_state = (uint8_t)p->can_error_state;

    for (i = 0u; i < p->cycles; i++) {
        if (p->notify_can_rx != 0u) {
            Swc_RzcSafety_NotifyCanRx();
        }
        Swc_RzcSafety_MainFunction();
    }
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    reset_state();

    /* Defaults */
    mock_rte_signals[RZC_SIG_VEHICLE_STATE]   = RZC_STATE_RUN;
    mock_rte_signals[RZC_SIG_SELF_TEST_RESULT] = RZC_SELF_TEST_PASS;

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guard in MainFunction). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_RzcSafety_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"status\":%u,\"faultMask\":%u,\"safetyStatus\":%u,"
           "\"wdiWrites\":%u,\"wdiLevel\":%u,"
           "\"dioCh5\":%u,\"dioCh6\":%u,\"dioWrites\":%u,"
           "\"demWatchdog\":%d,\"demWatchdogCount\":%u}\n",
           (unsigned)Swc_RzcSafety_GetStatus(),
           (unsigned)mock_rte_signals[RZC_SIG_FAULT_MASK],
           (unsigned)mock_rte_signals[RZC_SIG_SAFETY_STATUS],
           (unsigned)mock_dio_wdi_write_count,
           (unsigned)mock_dio_level[RZC_SAFETY_WDI_CHANNEL],
           (unsigned)mock_dio_level[RZC_MOTOR_R_EN_CHANNEL],
           (unsigned)mock_dio_level[RZC_MOTOR_L_EN_CHANNEL],
           (unsigned)mock_dio_write_count,
           (int)mock_dem_status[RZC_DTC_WATCHDOG_FAIL],
           (unsigned)mock_dem_count[RZC_DTC_WATCHDOG_FAIL]);

    return 0;
}
