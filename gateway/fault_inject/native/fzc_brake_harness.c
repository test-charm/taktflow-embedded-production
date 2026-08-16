/**
 * @file    fzc_brake_harness.c
 * @brief   Native test harness for Swc_Brake (FZC brake servo SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Brake.c and drives
 *          Swc_Brake_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness pins the RTE command + E-stop signals, the
 *          IoHwAb ADC feedback, and records PWM / RTE / DEM outputs.
 *
 *          Phase keys:
 *            cycles        uint32  (default 1) MainFunction calls
 *            skipInit      0|1     skip Swc_Brake_Init (uninitialized guard)
 *            initNull      0|1     call Swc_Brake_Init(NULL) (NULL-config guard)
 *            cmdBrake      uint32  commanded brake force 0-100+ (RTE FZC_SIG_BRAKE_CMD)
 *            rteReadFail   0|1     Rte_Read returns E_NOT_OK (timeout path)
 *            estop         0|1     RTE FZC_SIG_ESTOP_ACTIVE (immediate 100% brake)
 *            actualPos     uint32  IoHwAb ADC feedback 0-1000 (0-100% in 10ths)
 *            actualTrack   0|1     feedback tracks the commanded brake (healthy)
 *            posReadFail   0|1     IoHwAb_ReadBrakePosition returns E_NOT_OK
 *            getPos        0|1     call Swc_Brake_GetPosition at end of phase
 *            getPosNull    0|1     call Swc_Brake_GetPosition(NULL) at end of phase
 *
 *          Output is a single JSON object on stdout:
 *            {"brakePosition":..,"faultStatus":..,"pwmDuty":..,
 *             "motorCutoff":..,"pwmDisable":..,
 *             "demPwmFail":..,"demTimeout":..,"demOsc":..,"demBrakeFault":..,
 *             "getPosStatus":..,"getPos":..}
 *          dem* values: 0=PASSED, 1=FAILED, -1=not reported this run.
 *          getPosStatus: 0=E_OK, 1=E_NOT_OK.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_Brake.h"
#include "IoHwAb.h"
#include "Rte.h"
#include "Dem.h"
#include "Pwm.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS  32u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8_t  mock_rte_read_fail = 0u;
static uint16_t mock_iohwab_raw = 0u;         /* ADC counts 0-1000 = 0-100% */
static uint8_t  mock_iohwab_fail = 0u;
static uint16_t mock_pwm_duty = 0u;
static uint8_t  mock_pwm_calls = 0u;
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS]; /* per DTC id, -1 = not reported */

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

Std_ReturnType IoHwAb_ReadBrakePosition(uint16* Position)
{
    if (Position == NULL_PTR) {
        return E_NOT_OK;
    }
    if (mock_iohwab_fail != 0u) {
        return E_NOT_OK;
    }
    *Position = mock_iohwab_raw;
    return E_OK;
}

void Pwm_SetDutyCycle(uint8 ChannelNumber, uint16 DutyCycle)
{
    mock_pwm_calls++;
    mock_pwm_duty = DutyCycle;
    (void)ChannelNumber;
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

static uint32_t parse_uint(const char* s)
{
    return (uint32_t)strtoul(s, NULL, 10);
}

/* Clamped commanded brake 0-100 -> ADC raw 0-1000 (10 counts per percent) */
static uint16_t brake_to_raw(uint32_t cmd)
{
    if (cmd > 100u) {
        cmd = 100u;
    }
    return (uint16_t)(cmd * 10u);
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  init_null;
    uint32_t cmd_brake;
    uint8_t  rte_read_fail;
    uint8_t  estop;
    uint32_t actual_pos;
    uint8_t  actual_track;
    uint8_t  pos_read_fail;
    uint8_t  get_pos;
    uint8_t  get_pos_null;
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
    mock_rte_read_fail = 0u;
    mock_iohwab_raw = 0u;
    mock_iohwab_fail = 0u;
    mock_pwm_duty = 0u;
    mock_pwm_calls = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    mock_rte_signals[FZC_SIG_BRAKE_CMD] = p->cmd_brake;
    mock_rte_signals[FZC_SIG_ESTOP_ACTIVE] = p->estop;
    mock_rte_read_fail = p->rte_read_fail;
    mock_iohwab_fail = p->pos_read_fail;

    for (i = 0u; i < p->cycles; i++) {
        if (p->actual_track != 0u) {
            /* Healthy feedback: actual position tracks the commanded brake */
            mock_iohwab_raw = brake_to_raw(mock_rte_signals[FZC_SIG_BRAKE_CMD]);
        } else {
            mock_iohwab_raw = (uint16_t)((p->actual_pos > 1000u) ? 1000u : p->actual_pos);
        }
        Swc_Brake_MainFunction();
    }
}

int main(void)
{
    char line[1024];
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;
    uint8_t get_pos_out = 0u;
    Std_ReturnType get_pos_ret = E_NOT_OK;
    Swc_Brake_ConfigType cfg;
    static const Swc_Brake_ConfigType* cfg_ptr = NULL_PTR;

    /* Production brake config (matches main.c / Fzc_App.h) */
    cfg.autoTimeoutMs     = FZC_BRAKE_AUTO_TIMEOUT_MS;
    cfg.pwmFaultThreshold = FZC_BRAKE_PWM_FAULT_THRESH;
    cfg.faultDebounce     = FZC_BRAKE_FAULT_DEBOUNCE;
    cfg.latchClearCycles  = FZC_BRAKE_LATCH_CLEAR_CYCLES;
    cfg.cutoffRepeatCount = FZC_BRAKE_CUTOFF_REPEAT_COUNT;
    cfg_ptr = &cfg;

    reset_state();

    /* ---- parse all phases ---- */
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

        token = strtok_r(line, " \t\r\n", &saveptr);
        while (token != NULL) {
            char* eq = strchr(token, '=');
            if (eq != NULL) {
                *eq = '\0';
                {
                    const char* key = token;
                    if (strcmp(key, "cycles") == 0)         p.cycles = parse_uint(eq + 1);
                    else if (strcmp(key, "skipInit") == 0)  p.skip_init = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "initNull") == 0)  p.init_null = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "cmdBrake") == 0)  p.cmd_brake = parse_uint(eq + 1);
                    else if (strcmp(key, "rteReadFail") == 0) p.rte_read_fail = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "estop") == 0)     p.estop = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "actualPos") == 0) p.actual_pos = parse_uint(eq + 1);
                    else if (strcmp(key, "actualTrack") == 0) p.actual_track = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "posReadFail") == 0) p.pos_read_fail = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "getPos") == 0)    p.get_pos = (uint8_t)parse_uint(eq + 1);
                    else if (strcmp(key, "getPosNull") == 0) p.get_pos_null = (uint8_t)parse_uint(eq + 1);
                }
            }
            token = strtok_r(NULL, " \t\r\n", &saveptr);
        }

        phases[phase_count++] = p;
    }

    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        if (phases[0].init_null != 0u) {
            Swc_Brake_Init(NULL_PTR);
        } else {
            Swc_Brake_Init(cfg_ptr);
        }
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    /* GetPosition evaluated at the end of the LAST phase that requested it.
     * getPosNull forces the NULL-pointer path (E_NOT_OK). */
    for (pi = 0u; pi < phase_count; pi++) {
        if (phases[pi].get_pos_null != 0u) {
            (void)Swc_Brake_GetPosition(NULL_PTR);
        }
        if (phases[pi].get_pos != 0u) {
            get_pos_ret = Swc_Brake_GetPosition(&get_pos_out);
        }
    }

    printf("{\"brakePosition\":%u,\"faultStatus\":%u,\"pwmDuty\":%u,"
           "\"motorCutoff\":%u,\"pwmDisable\":%u,"
           "\"demPwmFail\":%d,\"demTimeout\":%d,\"demOsc\":%d,\"demBrakeFault\":%d,"
           "\"getPosStatus\":%u,\"getPos\":%u}\n",
           (unsigned)mock_rte_signals[FZC_SIG_BRAKE_POS],
           (unsigned)mock_rte_signals[FZC_SIG_BRAKE_FAULT],
           (unsigned)mock_pwm_duty,
           (unsigned)mock_rte_signals[FZC_SIG_MOTOR_CUTOFF],
           (unsigned)mock_rte_signals[FZC_SIG_BRAKE_PWM_DISABLE],
           (int)mock_dem_status[FZC_DTC_BRAKE_PWM_FAIL],
           (int)mock_dem_status[FZC_DTC_BRAKE_TIMEOUT],
           (int)mock_dem_status[FZC_DTC_BRAKE_OSCILLATION],
           (int)mock_dem_status[FZC_DTC_BRAKE_FAULT],
           (unsigned)get_pos_ret,
           (unsigned)get_pos_out);

    return 0;
}
