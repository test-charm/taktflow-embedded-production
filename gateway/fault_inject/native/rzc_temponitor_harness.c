/**
 * @file    rzc_temponitor_harness.c
 * @brief   Native test harness for Swc_TempMonitor (RZC temperature monitor SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_TempMonitor.c and drives
 *          Swc_TempMonitor_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness pins the raw NTC temperatures injected via the
 *          IoHwAb_ReadMotorTemp / IoHwAb_ReadMotorTemp2 mocks and records the
 *          RTE / DEM outputs.
 *
 *          Phase keys:
 *            cycles      uint32 (default 1) MainFunction calls
 *            skipInit    0|1    skip Swc_TempMonitor_Init (uninitialized guard)
 *            tempDc      int    NTC1 temperature (deci-degrees C) injected via
 *                               IoHwAb_ReadMotorTemp mock
 *            temp2Dc     int    NTC2 temperature (deci-degrees C) injected via
 *                               IoHwAb_ReadMotorTemp2 mock (default = tempDc)
 *            ioFault     0|1    IoHwAb_ReadMotorTemp returns E_NOT_OK
 *            temp2Fail   0|1    IoHwAb_ReadMotorTemp2 returns E_NOT_OK
 *
 *          Output is a single JSON object on stdout:
 *            {"temp1Dc":..,"temp2Dc":..,"deratingPct":..,"tempFault":..,
 *             "demOvertemp":..}
 *          temp1Dc:     RTE RZC_SIG_TEMP1_DC (selected/hot reading, deci-deg C)
 *          temp2Dc:     RTE RZC_SIG_TEMP2_DC (second NTC reading)
 *          deratingPct: RTE RZC_SIG_DERATING_PCT (100/75/50/0)
 *          tempFault:   RTE RZC_SIG_TEMP_FAULT (0/1)
 *          demOvertemp: 1=FAILED reported on RZC_DTC_OVERTEMP, -1=not reported.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_TempMonitor.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS 16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static int32_t  mock_temp_dc = 0;     /* NTC1 (deci-degrees C) */
static int32_t  mock_temp2_dc = 0;    /* NTC2 (deci-degrees C) */
static uint8_t  mock_io_fault = 0;    /* IoHwAb_ReadMotorTemp returns E_NOT_OK */
static uint8_t  mock_temp2_fail = 0;  /* IoHwAb_ReadMotorTemp2 returns E_NOT_OK */
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

Std_ReturnType IoHwAb_ReadMotorTemp(uint16* Temp_dC)
{
    if (Temp_dC == NULL_PTR) {
        return E_NOT_OK;
    }
    if (mock_io_fault != 0u) {
        return E_NOT_OK;
    }
    *Temp_dC = (uint16)(uint16_t)mock_temp_dc;
    return E_OK;
}

Std_ReturnType IoHwAb_ReadMotorTemp2(uint16* Temp_dC)
{
    if (Temp_dC == NULL_PTR) {
        return E_NOT_OK;
    }
    if (mock_temp2_fail != 0u) {
        return E_NOT_OK;
    }
    *Temp_dC = (uint16)(uint16_t)mock_temp2_dc;
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
    int32_t  temp_dc;
    int32_t  temp2_dc;
    uint8_t  io_fault;
    uint8_t  temp2_fail;
    uint8_t  temp2_set;   /* 1 when temp2Dc was explicitly provided */
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
    mock_temp_dc    = 0;
    mock_temp2_dc   = 0;
    mock_io_fault   = 0u;
    mock_temp2_fail = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    mock_temp_dc    = p->temp_dc;
    mock_temp2_dc   = p->temp2_dc;
    mock_io_fault   = p->io_fault;
    mock_temp2_fail = p->temp2_fail;

    for (i = 0u; i < p->cycles; i++) {
        Swc_TempMonitor_MainFunction();
    }
}


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles  = 1u;
    p->temp_dc = 0;
    p->temp2_dc = 0;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "cycles") == 0)         p->cycles     = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)  p->skip_init  = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "tempDc") == 0)    p->temp_dc    = harness_parse_int(value);
    else if (strcmp(key, "temp2Dc") == 0)   { p->temp2_dc  = harness_parse_int(value); p->temp2_set = 1; }
    else if (strcmp(key, "ioFault") == 0)   p->io_fault   = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "temp2Fail") == 0) p->temp2_fail = (uint8_t)harness_parse_uint(value);
    return 0;
}

static int finish_phase(void* phase, size_t index)
{
    Phase* p = (Phase*)phase;
    (void)index;
    /* If temp2Dc not specified, sensors agree with NTC1 (default) */
    if (p->temp2_set == 0) {
        p->temp2_dc = p->temp_dc;
    }
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
                                    set_phase_field, finish_phase);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_TempMonitor_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"temp1Dc\":%d,\"temp2Dc\":%d,\"deratingPct\":%u,\"tempFault\":%u,\"demOvertemp\":%d}\n",
           (int)(int16_t)mock_rte_signals[RZC_SIG_TEMP1_DC],
           (int)(int16_t)mock_rte_signals[RZC_SIG_TEMP2_DC],
           (unsigned)mock_rte_signals[RZC_SIG_DERATING_PCT],
           (unsigned)mock_rte_signals[RZC_SIG_TEMP_FAULT],
           (int)mock_dem_status[RZC_DTC_OVERTEMP]);

    return 0;
}
