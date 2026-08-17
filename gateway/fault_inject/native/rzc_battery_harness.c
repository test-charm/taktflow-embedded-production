/**
 * @file    rzc_battery_harness.c
 * @brief   Native test harness for Swc_Battery (RZC battery voltage monitor SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Battery.c and drives
 *          Swc_Battery_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness pins the raw battery voltage injected via the
 *          IoHwAb_ReadBatteryVoltage mock and records the RTE / DEM outputs.
 *
 *          Phase keys:
 *            cycles      uint32 (default 1) MainFunction calls
 *            skipInit    0|1    skip Swc_Battery_Init (uninitialized guard)
 *            voltageMv   int    raw battery voltage (mV) injected via
 *                               IoHwAb_ReadBatteryVoltage mock
 *
 *          Output is a single JSON object on stdout:
 *            {"voltageMv":..,"status":..,"demBattery":..}
 *          voltageMv:  4-sample moving average (RTE RZC_SIG_BATTERY_MV)
 *          status:     RTE RZC_SIG_BATTERY_STATUS
 *                      0=DISABLE_LOW 1=WARN_LOW 2=NORMAL 3=WARN_HIGH
 *                      4=DISABLE_HIGH
 *          demBattery: 1=FAILED reported on RZC_DTC_BATTERY, -1=not reported.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_Battery.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS 16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint16_t mock_battery_voltage_mv = 0u;
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS];   /* per DTC event id, -1 = not reported */

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

Std_ReturnType IoHwAb_ReadBatteryVoltage(uint16* Voltage_mV)
{
    if (Voltage_mV == NULL_PTR) {
        return E_NOT_OK;
    }
    *Voltage_mV = mock_battery_voltage_mv;
    return E_OK;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if (EventId < MOCK_DEM_MAX_EVENTS) {
        mock_dem_status[EventId] = (int8_t)EventStatus;
    }
}

/* ==================================================================
 * Helpers
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint32_t voltage_mv;
} Phase;

static void reset_state(void)
{
    uint16_t i;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    for (i = 0u; i < MOCK_DEM_MAX_EVENTS; i++) {
        mock_dem_status[i] = (int8_t)-1;
    }
    mock_battery_voltage_mv = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    mock_battery_voltage_mv = (uint16_t)p->voltage_mv;

    for (i = 0u; i < p->cycles; i++) {
        Swc_Battery_MainFunction();
    }
}


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "cycles") == 0)         p->cycles     = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)  p->skip_init  = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "voltageMv") == 0) p->voltage_mv = (uint32_t)harness_parse_int(value);
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    reset_state();

    /* ---- parse all phases ---- */
        {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_Battery_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"voltageMv\":%u,\"status\":%u,\"demBattery\":%d}\n",
           (unsigned)mock_rte_signals[RZC_SIG_BATTERY_MV],
           (unsigned)mock_rte_signals[RZC_SIG_BATTERY_STATUS],
           (int)mock_dem_status[RZC_DTC_BATTERY]);

    return 0;
}
