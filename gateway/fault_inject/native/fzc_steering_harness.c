/**
 * @file    fzc_steering_harness.c
 * @brief   Native test harness for Swc_Steering (FZC steering servo SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Steering.c and drives
 *          Swc_Steering_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness pins the RTE command signal, the IoHwAb SPI
 *          feedback, and records PWM / Dio / RTE / DEM outputs.
 *
 *          Phase keys:
 *            cycles        uint32  (default 1) MainFunction calls
 *            skipInit      0|1     skip Swc_Steering_Init (uninitialized guard)
 *            initNull      0|1     call Swc_Steering_Init(NULL) (NULL-config guard)
 *            cmdAngle      int     commanded angle in degrees (RTE FZC_SIG_STEER_CMD)
 *            rteReadFail   0|1     Rte_Read returns E_NOT_OK (timeout path)
 *            actualAngle   int     IoHwAb feedback in degrees (14-bit SPI raw)
 *            actualTrack   0|1     feedback tracks previous RTE output (healthy)
 *            spiFail       0|1     IoHwAb_ReadSteeringAngle returns E_NOT_OK
 *            getAngle      0|1     call Swc_Steering_GetAngle at end of phase
 *            getAngleNull  0|1     call Swc_Steering_GetAngle(NULL) at end of phase
 *
 *          Output is a single JSON object on stdout:
 *            {"currentAngle":..,"faultStatus":..,"pwmDisableLevel":..,
 *             "pwmDuty":..,"dioCh10":..,"dioCh11":..,
 *             "demPlaus":..,"demRange":..,"demTimeout":..,"demSpi":..,
 *             "getAngleStatus":..,"getAngle":..}
 *          dem* values: 0=PASSED, 1=FAILED, -1=not reported this run.
 *          getAngleStatus: 0=E_OK, 1=E_NOT_OK.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_Steering.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"
#include "Pwm.h"
#include "Dio.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8_t  mock_rte_read_fail = 0u;
static uint16_t mock_iohwab_raw = 8191U;   /* 14-bit center = 0 deg */
static uint8_t  mock_iohwab_fail = 0u;
static uint16_t mock_pwm_duty = 0u;
static uint8_t  mock_pwm_calls = 0u;
static uint8_t  mock_dio_level[16u];
static int8_t   mock_dem_status[8u];       /* per DTC event id, -1 = not reported */

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
    if (mock_rte_read_fail != 0u) {
        return E_NOT_OK;
    }
    *DataPtr = mock_rte_signals[SignalId];
    return E_OK;
}

Std_ReturnType IoHwAb_ReadSteeringAngle(uint16* Angle)
{
    if (Angle == NULL_PTR) {
        return E_NOT_OK;
    }
    if (mock_iohwab_fail != 0u) {
        return E_NOT_OK;
    }
    *Angle = mock_iohwab_raw;
    return E_OK;
}

void Pwm_SetDutyCycle(uint8 ChannelNumber, uint16 DutyCycle)
{
    mock_pwm_calls++;
    mock_pwm_duty = DutyCycle;
    (void)ChannelNumber;
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

/* Degrees (-45..+45) -> 14-bit SPI raw (0..16383). Matches plant-sim:
 * raw = ((deg + 45) * 16383) / 90 */
static uint16_t deg_to_raw(int32_t deg)
{
    int32_t raw;
    if (deg < -45) { deg = -45; }
    if (deg > 45)  { deg = 45; }
    raw = ((deg + 45) * 16383) / 90;
    if (raw < 0) { raw = 0; }
    if (raw > 16383) { raw = 16383; }
    return (uint16_t)raw;
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  init_null;
    int32_t  cmd_angle;
    uint8_t  rte_read_fail;
    int32_t  actual_angle;
    uint8_t  actual_track;
    uint8_t  spi_fail;
    uint8_t  get_angle;
    uint8_t  get_angle_null;
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
    mock_rte_read_fail = 0u;
    mock_iohwab_raw = 8191U;
    mock_iohwab_fail = 0u;
    mock_pwm_duty = 0u;
    mock_pwm_calls = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    mock_rte_signals[FZC_SIG_STEER_CMD] = (uint32_t)(uint16_t)(int16_t)p->cmd_angle;
    mock_rte_read_fail = p->rte_read_fail;
    mock_iohwab_fail = p->spi_fail;

    for (i = 0u; i < p->cycles; i++) {
        if (p->actual_track != 0u) {
            /* Healthy feedback: actual follows the previous cycle's RTE output */
            mock_iohwab_raw = deg_to_raw((int32_t)(int16_t)(uint16_t)mock_rte_signals[FZC_SIG_STEER_ANGLE]);
        } else {
            mock_iohwab_raw = deg_to_raw(p->actual_angle);
        }
        Swc_Steering_MainFunction();
    }

    if (p->get_angle_null != 0u) {
        /* evaluated in main() end loop */
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
    if (strcmp(key, "cycles") == 0)         p->cycles = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)  p->skip_init = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "initNull") == 0)  p->init_null = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "cmdAngle") == 0)  p->cmd_angle = harness_parse_int(value);
    else if (strcmp(key, "rteReadFail") == 0) p->rte_read_fail = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "actualAngle") == 0) p->actual_angle = harness_parse_int(value);
    else if (strcmp(key, "actualTrack") == 0) p->actual_track = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "spiFail") == 0)   p->spi_fail = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "getAngle") == 0)  p->get_angle = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "getAngleNull") == 0) p->get_angle_null = (uint8_t)harness_parse_uint(value);
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;
    sint16 get_angle_out = 0;
    Std_ReturnType get_angle_ret = E_NOT_OK;
    Swc_Steering_ConfigType cfg;
    static const Swc_Steering_ConfigType* cfg_ptr = NULL_PTR;

    /* Production steering config (matches main.c / Fzc_App.h) */
    cfg.plausThreshold   = FZC_STEER_PLAUS_THRESHOLD_DEG;
    cfg.plausDebounce    = FZC_STEER_PLAUS_DEBOUNCE;
    cfg.rateLimitDeg10ms = FZC_STEER_RATE_LIMIT_DEG_10MS;
    cfg.cmdTimeoutMs     = FZC_STEER_CMD_TIMEOUT_MS;
    cfg.rtcRateDegS      = FZC_STEER_RTC_RATE_DEG_S;
    cfg.latchClearCycles = FZC_STEER_LATCH_CLEAR_CYCLES;
    cfg_ptr = &cfg;

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
        if (phases[0].init_null != 0u) {
            Swc_Steering_Init(NULL_PTR);
        } else {
            Swc_Steering_Init(cfg_ptr);
        }
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    /* GetAngle is evaluated at the end of the LAST phase that requested it.
     * getAngleNull forces the NULL-pointer path (E_NOT_OK). */
    for (pi = 0u; pi < phase_count; pi++) {
        if (phases[pi].get_angle_null != 0u) {
            (void)Swc_Steering_GetAngle(NULL_PTR);
        }
        if (phases[pi].get_angle != 0u) {
            get_angle_ret = Swc_Steering_GetAngle(&get_angle_out);
        }
    }

    printf("{\"currentAngle\":%d,\"faultStatus\":%u,\"pwmDisableLevel\":%u,"
           "\"pwmDuty\":%u,\"dioCh10\":%u,\"dioCh11\":%u,"
           "\"demPlaus\":%d,\"demRange\":%d,\"demTimeout\":%d,\"demSpi\":%d,"
           "\"getAngleStatus\":%u,\"getAngle\":%d}\n",
           (int)(int16_t)(uint16_t)mock_rte_signals[FZC_SIG_STEER_ANGLE],
           (unsigned)mock_rte_signals[FZC_SIG_STEER_FAULT],
           (unsigned)mock_rte_signals[FZC_SIG_STEER_PWM_DISABLE],
           (unsigned)mock_pwm_duty,
           (unsigned)mock_dio_level[10u],
           (unsigned)mock_dio_level[11u],
           (int)mock_dem_status[FZC_DTC_STEER_PLAUSIBILITY],
           (int)mock_dem_status[FZC_DTC_STEER_RANGE],
           (int)mock_dem_status[FZC_DTC_STEER_TIMEOUT],
           (int)mock_dem_status[FZC_DTC_STEER_SPI_FAIL],
           (unsigned)get_angle_ret,
           (int)get_angle_out);

    return 0;
}
