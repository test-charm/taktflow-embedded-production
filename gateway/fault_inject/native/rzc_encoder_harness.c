/**
 * @file    rzc_encoder_harness.c
 * @brief   Native test harness for Swc_Encoder (RZC encoder SWC)
 * @date    2026-08-17
 *
 * @details Links the REAL production Swc_Encoder.c and drives
 *          Swc_Encoder_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens.
 *
 *          Phase keys:
 *            cycles         uint32  (default 1) MainFunction calls
 *            skipInit       0|1     skip Swc_Encoder_Init (uninitialized guard)
 *            count          uint32  absolute encoder count before this phase
 *            deltaPerCycle  uint32  count increment applied before each cycle
 *            encoderDir     int     IoHwAb encoder direction (0/1/2)
 *            commandedDir   int     RTE motor direction command (0/1/2)
 *            torqueEcho     int     RTE torque echo % used by stall detection
 *
 *          Output is a single JSON object on stdout:
 *            {"speedRpm":..,"encoderDir":..,"encoderStall":..,
 *             "dioCh5":..,"dioCh6":..,"dioWrites":..,
 *             "demStall":..,"demStallCount":..,
 *             "demDirection":..,"demDirectionCount":..}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_Encoder.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"
#include "Dio.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS  16u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_encoder_count = 0u;
static uint8_t  mock_encoder_dir = RZC_DIR_FORWARD;
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

Std_ReturnType IoHwAb_ReadEncoderCount(uint32* Count)
{
    if (Count == NULL_PTR) {
        return E_NOT_OK;
    }
    *Count = mock_encoder_count;
    return E_OK;
}

Std_ReturnType IoHwAb_ReadEncoderDirection(uint8* Dir)
{
    if (Dir == NULL_PTR) {
        return E_NOT_OK;
    }
    *Dir = mock_encoder_dir;
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
    uint8_t  has_count;
    uint32_t count;
    uint32_t delta_per_cycle;
    int32_t  encoder_dir;
    int32_t  commanded_dir;
    int32_t  torque_echo;
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
    mock_encoder_count = 0u;
    mock_encoder_dir = RZC_DIR_FORWARD;
    mock_dio_write_count = 0u;
    mock_rte_signals[RZC_SIG_MOTOR_DIR] = (uint32_t)RZC_DIR_FORWARD;
    mock_rte_signals[RZC_SIG_TORQUE_ECHO] = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    if (p->has_count != 0u) {
        mock_encoder_count = p->count;
    }
    mock_encoder_dir = (uint8_t)p->encoder_dir;
    mock_rte_signals[RZC_SIG_MOTOR_DIR] = (uint32_t)p->commanded_dir;
    mock_rte_signals[RZC_SIG_TORQUE_ECHO] = (uint32_t)p->torque_echo;

    for (i = 0u; i < p->cycles; i++) {
        mock_encoder_count += p->delta_per_cycle;
        Swc_Encoder_MainFunction();
    }
}

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles = 1u;
    p->encoder_dir = (int32_t)RZC_DIR_FORWARD;
    p->commanded_dir = (int32_t)RZC_DIR_FORWARD;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "cycles") == 0)              p->cycles = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)       p->skip_init = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "count") == 0)          { p->has_count = 1u; p->count = harness_parse_uint(value); }
    else if (strcmp(key, "deltaPerCycle") == 0)  p->delta_per_cycle = harness_parse_uint(value);
    else if (strcmp(key, "encoderDir") == 0)     p->encoder_dir = harness_parse_int(value);
    else if (strcmp(key, "commandedDir") == 0)   p->commanded_dir = harness_parse_int(value);
    else if (strcmp(key, "torqueEcho") == 0)     p->torque_echo = harness_parse_int(value);
    return 0;
}

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
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
        global_skip_init = phases[0].skip_init;
    }

    if (global_skip_init == 0u) {
        Swc_Encoder_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"speedRpm\":%u,\"encoderDir\":%u,\"encoderStall\":%u,"
           "\"dioCh5\":%u,\"dioCh6\":%u,\"dioWrites\":%u,"
           "\"demStall\":%d,\"demStallCount\":%u,"
           "\"demDirection\":%d,\"demDirectionCount\":%u}\n",
           (unsigned)mock_rte_signals[RZC_SIG_ENCODER_SPEED],
           (unsigned)mock_rte_signals[RZC_SIG_ENCODER_DIR],
           (unsigned)mock_rte_signals[RZC_SIG_ENCODER_STALL],
           (unsigned)mock_dio_level[RZC_MOTOR_R_EN_CHANNEL],
           (unsigned)mock_dio_level[RZC_MOTOR_L_EN_CHANNEL],
           (unsigned)mock_dio_write_count,
           (int)mock_dem_status[RZC_DTC_STALL],
           (unsigned)mock_dem_count[RZC_DTC_STALL],
           (int)mock_dem_status[RZC_DTC_DIRECTION],
           (unsigned)mock_dem_count[RZC_DTC_DIRECTION]);

    return 0;
}
