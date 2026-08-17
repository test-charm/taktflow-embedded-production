/**
 * @file    cvc_estop_harness.c
 * @brief   Native test harness for Swc_EStop (CVC emergency stop SWC)
 * @date    2026-08-15
 *
 * @details Links the REAL production Swc_EStop.c + Swc_CvcCom.c and drives
 *          the E-stop SWC through a phase script read from stdin. Each phase
 *          is one line of whitespace-separated key=value tokens. The harness
 *          pins the IoHwAb E-stop button state (LOW/HIGH), optionally forces
 *          an IoHwAb read failure (fail-safe = active), then runs
 *          Swc_EStop_MainFunction() `cycles` times. After the script, the
 *          harness calls Swc_CvcCom_TransmitSchedule() so the EStop_Broadcast
 *          (0x001) Com signals reflect the latched E-stop state — proving the
 *          full chain button → SWC → RTE → CvcCom → Com signals.
 *
 *          Phase keys:
 *            cycles      uint32   (default 0)
 *            pin         0|1      E-stop button: 0=LOW(released), 1=HIGH(pressed)
 *            readFail    0|1      IoHwAb_ReadEStop returns E_NOT_OK (fail-safe active)
 *            skipInit    0|1      skip Swc_EStop_Init() for this phase
 *                                 (used to test uninitialized no-op guard)
 *
 *          Output is a single JSON object on stdout:
 *            {"isActive":1,"rteEstopActive":1,"demEventId":7,"demEventStatus":1,
 *             "demReportCount":5,"rteWriteCount":5,
 *             "broadcastActive":1,"broadcastSource":1}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_EStop.h"
#include "Swc_CvcCom.h"
#include "Rte.h"
#include "Dem.h"
#include "Com.h"
#include "IoHwAb.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_COM_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];

/* ---- E-stop pin + read-failure injection ---- */
static uint8_t mock_estop_pin = STD_LOW;
static uint8_t mock_estop_read_fail = 0u;

/* ---- Dem capture ---- */
static uint8_t  captured_dem_event_id = 0xFFu;
static uint8_t  captured_dem_status = 0xFFu;
static uint32_t dem_report_count = 0u;

/* ---- Rte_Write capture (only counts CVC_SIG_ESTOP_ACTIVE) ---- */
static uint32_t estop_rte_write_count = 0u;

/* ==================================================================
 * BSW stubs
 * ================================================================== */

Std_ReturnType IoHwAb_ReadEStop(uint8* State)
{
    if (State == NULL_PTR) {
        return E_NOT_OK;
    }
    if (mock_estop_read_fail != 0u) {
        *State = STD_HIGH;   /* fail-safe default even before overwrite */
        return E_NOT_OK;
    }
    *State = mock_estop_pin;
    return E_OK;
}

Std_ReturnType Rte_Write(Rte_SignalIdType SignalId, uint32 Data)
{
    if (SignalId >= MOCK_RTE_MAX_SIGNALS) {
        return E_NOT_OK;
    }
    mock_rte_signals[SignalId] = Data;
    if (SignalId == CVC_SIG_ESTOP_ACTIVE) {
        estop_rte_write_count++;
    }
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

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    captured_dem_event_id = (uint8_t)EventId;
    captured_dem_status = (uint8_t)EventStatus;
    dem_report_count++;
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    switch (SignalId) {
        case CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK:
            mock_com_signals[SignalId] = *(const uint16*)SignalDataPtr;
            break;
        case CVC_COM_SIG_STEER_COMMAND_STEER_ANGLE_CMD:
            mock_com_signals[SignalId] = (uint32)*(const sint16*)SignalDataPtr;
            break;
        default:
            mock_com_signals[SignalId] = *(const uint8*)SignalDataPtr;
            break;
    }
    return E_OK;
}

Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    *(uint8_t*)SignalDataPtr = (uint8_t)mock_com_signals[SignalId];
    return E_OK;
}

uint8 Swc_VehicleState_GetState(void)
{
    /* Harness stub: E-stop chain does not depend on VSM state. */
    return CVC_STATE_RUN;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  pin;
    uint8_t  read_fail;
    uint8_t  skip_init;
} Phase;

static void apply_phase(const Phase* p)
{
    mock_estop_pin = (p->pin != 0u) ? STD_HIGH : STD_LOW;
    mock_estop_read_fail = p->read_fail;
}

static int run_phase(const Phase* p)
{
    uint32_t i;

    apply_phase(p);

    for (i = 0u; i < p->cycles; i++) {
        Swc_EStop_MainFunction();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->skip_init = 0u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "cycles") == 0)       p->cycles = val;
    else if (strcmp(key, "pin") == 0)     p->pin = (uint8_t)val;
    else if (strcmp(key, "readFail") == 0) p->read_fail = (uint8_t)val;
    else if (strcmp(key, "skipInit") == 0) p->skip_init = (uint8_t)val;
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;

    /* Default: button released, read OK */
    mock_estop_pin = STD_LOW;
    mock_estop_read_fail = 0u;

    /* ---- parse all phases first (skipInit is decided on phase[0]) ---- */
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
    if ((phase_count == 0u) || (phases[0].skip_init == 0u)) {
        Swc_EStop_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    /* ---- Bridge E-stop state to Com signals (0x001 broadcast) ---- */
    Swc_CvcCom_Init();
    Swc_CvcCom_TransmitSchedule(0u);

    printf("{\"isActive\":%u,\"rteEstopActive\":%u,"
           "\"demEventId\":%u,\"demEventStatus\":%u,"
           "\"demReportCount\":%u,\"rteWriteCount\":%u,"
           "\"broadcastActive\":%u,\"broadcastSource\":%u}\n",
           (unsigned)(Swc_EStop_IsActive() ? 1u : 0u),
           (unsigned)mock_rte_signals[CVC_SIG_ESTOP_ACTIVE],
           (unsigned)captured_dem_event_id,
           (unsigned)captured_dem_status,
           (unsigned)dem_report_count,
           (unsigned)estop_rte_write_count,
           (unsigned)mock_com_signals[CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE],
           (unsigned)mock_com_signals[CVC_COM_SIG_ESTOP_BROADCAST_SOURCE]);

    return 0;
}
