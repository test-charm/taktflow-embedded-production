/**
 * @file    rzc_motor_harness.c
 * @brief   Native test harness for Swc_Motor (RZC H-bridge motor SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Motor.c and drives
 *          Swc_Motor_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness pins the RTE command / vehicle-state / e-stop
 *          / derating / external-fault signals and records PWM / Dio / RTE /
 *          DEM outputs.
 *
 *          Phase keys:
 *            cycles        uint32  (default 1) MainFunction calls
 *            skipInit      0|1     skip Swc_Motor_Init (uninitialized guard)
 *            vehicleState  int     RZC_SIG_VEHICLE_STATE (0=INIT 1=RUN
 *                                  2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN)
 *            estop         int     RZC_SIG_ESTOP_ACTIVE (0/1)
 *            torqueCmd     int     RZC_SIG_TORQUE_CMD (sint16 % , e.g. -100..100)
 *            derating      int     RZC_SIG_DERATING_PCT (0..100+, clamped to 100)
 *            overcurrent   int     RZC_SIG_OVERCURRENT (0/1)
 *            tempFault     int     RZC_SIG_TEMP_FAULT (0/1)
 *
 *          Output is a single JSON object on stdout:
 *            {"torqueEcho":..,"motorDir":..,"motorEnable":..,"motorFault":..,
 *             "pwmDuty":..,"pwmDir":..,"dioCh5":..,"dioCh6":..,
 *             "demTimeout":..}
 *          demTimeout: 0=PASSED, 1=FAILED, -1=not reported this run.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Rzc_Cfg.h"
#include "Swc_Motor.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"
#include "Dio.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint16_t mock_pwm_duty = 0u;
static uint8_t  mock_pwm_dir  = 0xFFu;
static uint8_t  mock_dio_level[16u];
static int8_t   mock_dem_status[8u];   /* per DTC event id, -1 = not reported */

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

Std_ReturnType IoHwAb_SetMotorPWM(uint8 Direction, uint16 DutyCycle)
{
    mock_pwm_dir  = Direction;
    mock_pwm_duty = DutyCycle;
    return E_OK;
}

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    if (ChannelId < 16u) {
        mock_dio_level[ChannelId] = Level;
    }
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if (EventId < 8u) {
        mock_dem_status[EventId] = (int8_t)EventStatus;
    }
}

/* ==================================================================
 * Helpers
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    int32_t  vehicle_state;
    int32_t  estop;
    int32_t  torque_cmd;
    int32_t  derating;
    int32_t  overcurrent;
    int32_t  temp_fault;
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
    for (i = 0u; i < 8u; i++) {
        mock_dem_status[i] = (int8_t)-1;
    }
    mock_pwm_duty = 0u;
    mock_pwm_dir  = 0xFFu;

    /* Production defaults: RUN state, no e-stop, full derating */
    mock_rte_signals[RZC_SIG_VEHICLE_STATE] = (uint32_t)RZC_STATE_RUN;
    mock_rte_signals[RZC_SIG_DERATING_PCT]  = 100u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    mock_rte_signals[RZC_SIG_VEHICLE_STATE] = (uint32_t)p->vehicle_state;
    mock_rte_signals[RZC_SIG_ESTOP_ACTIVE]  = (uint32_t)p->estop;
    mock_rte_signals[RZC_SIG_TORQUE_CMD]    = (uint32_t)(uint16_t)(int16_t)p->torque_cmd;
    mock_rte_signals[RZC_SIG_DERATING_PCT]  = (uint32_t)p->derating;
    mock_rte_signals[RZC_SIG_OVERCURRENT]   = (uint32_t)p->overcurrent;
    mock_rte_signals[RZC_SIG_TEMP_FAULT]    = (uint32_t)p->temp_fault;

    for (i = 0u; i < p->cycles; i++) {
        Swc_Motor_MainFunction();
    }
}


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles        = 1u;
    p->vehicle_state = RZC_STATE_RUN;
    p->derating      = 100;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    if (strcmp(key, "cycles") == 0)            p->cycles        = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)     p->skip_init     = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "vehicleState") == 0) p->vehicle_state = harness_parse_int(value);
    else if (strcmp(key, "estop") == 0)        p->estop         = harness_parse_int(value);
    else if (strcmp(key, "torqueCmd") == 0)    p->torque_cmd    = harness_parse_int(value);
    else if (strcmp(key, "derating") == 0)     p->derating      = harness_parse_int(value);
    else if (strcmp(key, "overcurrent") == 0)  p->overcurrent   = harness_parse_int(value);
    else if (strcmp(key, "tempFault") == 0)    p->temp_fault    = harness_parse_int(value);
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
        Swc_Motor_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    printf("{\"torqueEcho\":%u,\"motorDir\":%u,\"motorEnable\":%u,\"motorFault\":%u,"
           "\"pwmDuty\":%u,\"pwmDir\":%u,\"dioCh5\":%u,\"dioCh6\":%u,"
           "\"demTimeout\":%d}\n",
           (unsigned)mock_rte_signals[RZC_SIG_TORQUE_ECHO],
           (unsigned)mock_rte_signals[RZC_SIG_MOTOR_DIR],
           (unsigned)mock_rte_signals[RZC_SIG_MOTOR_ENABLE],
           (unsigned)mock_rte_signals[RZC_SIG_MOTOR_FAULT],
           (unsigned)mock_pwm_duty,
           (unsigned)mock_pwm_dir,
           (unsigned)mock_dio_level[5u],
           (unsigned)mock_dio_level[6u],
           (int)mock_dem_status[RZC_DTC_CMD_TIMEOUT]);

    return 0;
}
