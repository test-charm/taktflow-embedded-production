/**
 * @file    rzc_currentmonitor_harness.c
 * @brief   Native test harness for Swc_CurrentMonitor (RZC current monitor SWC)
 * @date    2026-08-17
 *
 * @details Links the REAL production Swc_CurrentMonitor.c and drives
 *          Swc_CurrentMonitor_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The first phase's currentMa is also used as the input for
 *          the initial Swc_CurrentMonitor_Init() zero-calibration sequence.
 *
 *          Phase keys:
 *            cycles      uint32 (default 1) MainFunction calls
 *            skipInit    0|1    skip Swc_CurrentMonitor_Init (guard test)
 *            currentMa   int    raw motor current (mA) returned by
 *                               IoHwAb_ReadMotorCurrent
 *
 *          Output is a single JSON object on stdout:
 *            {"currentMa":..,"overcurrent":..,"dioCh5":..,"dioCh6":..,
 *             "dioWrites":..,"demOvercurrent":..,"demOvercurrentCount":..,
 *             "demZeroCal":..,"demZeroCalCount":..}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_CurrentMonitor.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"
#include "Dio.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS  16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint16_t mock_current_ma = RZC_CURRENT_ZEROCAL_CENTER;
static uint8_t  mock_dio_level[16u];
static uint32_t mock_dio_write_count = 0u;
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS];
static uint32_t mock_dem_count[MOCK_DEM_MAX_EVENTS];

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

Std_ReturnType IoHwAb_ReadMotorCurrent(uint16* Current_mA)
{
    if (Current_mA == NULL_PTR) {
        return E_NOT_OK;
    }
    *Current_mA = mock_current_ma;
    return E_OK;
}

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    mock_dio_write_count++;
    if (ChannelId < 16u) {
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

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    int32_t  current_ma;
} Phase;

static void reset_state(void)
{
    uint16_t i;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    for (i = 0u; i < 16u; i++) {
        mock_dio_level[i] = 0u;
    }
    for (i = 0u; i < MOCK_DEM_MAX_EVENTS; i++) {
        mock_dem_status[i] = (int8_t)-1;
        mock_dem_count[i] = 0u;
    }
    mock_current_ma = RZC_CURRENT_ZEROCAL_CENTER;
    mock_dio_write_count = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;
    mock_current_ma = (uint16_t)p->current_ma;
    for (i = 0u; i < p->cycles; i++) {
        Swc_CurrentMonitor_MainFunction();
    }
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles = 1u;
    p->current_ma = (int32_t)RZC_CURRENT_ZEROCAL_CENTER;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "cycles") == 0)          p->cycles = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)   p->skip_init = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "currentMa") == 0)  p->current_ma = harness_parse_int(value);
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    reset_state();

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    if (phase_count > 0u) {
        mock_current_ma = (uint16_t)phases[0].current_ma;
        global_skip_init = phases[0].skip_init;
    }

    if (global_skip_init == 0u) {
        Swc_CurrentMonitor_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"currentMa\":%u,\"overcurrent\":%u,\"dioCh5\":%u,\"dioCh6\":%u,"
           "\"dioWrites\":%u,\"demOvercurrent\":%d,\"demOvercurrentCount\":%u,"
           "\"demZeroCal\":%d,\"demZeroCalCount\":%u}\n",
           (unsigned)mock_rte_signals[RZC_SIG_CURRENT_MA],
           (unsigned)mock_rte_signals[RZC_SIG_OVERCURRENT],
           (unsigned)mock_dio_level[RZC_MOTOR_R_EN_CHANNEL],
           (unsigned)mock_dio_level[RZC_MOTOR_L_EN_CHANNEL],
           (unsigned)mock_dio_write_count,
           (int)mock_dem_status[RZC_DTC_OVERCURRENT],
           (unsigned)mock_dem_count[RZC_DTC_OVERCURRENT],
           (int)mock_dem_status[RZC_DTC_ZERO_CAL],
           (unsigned)mock_dem_count[RZC_DTC_ZERO_CAL]);

    return 0;
}
