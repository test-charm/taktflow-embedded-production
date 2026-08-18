/**
 * @file    rzc_selftest_harness.c
 * @brief   Native test harness for Swc_RzcSelfTest (RZC startup self-test SWC)
 * @date    2026-08-18
 *
 * @details Links the REAL production Swc_RzcSelfTest.c and drives its
 *          Init / Startup / GetResultMask APIs through a phase script read
 *          from stdin. Each phase is one line of whitespace-separated
 *          key=value tokens that pins the result of each of the eight
 *          injected hardware diagnostic callbacks and runs
 *          Swc_RzcSelfTest_Startup once.
 *
 *          Phase keys (per diagnostic item, default 1 = pass):
 *            bts7960  0|1|2   BTS7960 enable-pin toggle
 *                            (0=E_NOT_OK fail, 1=E_OK pass, 2=NULL callback)
 *            acs723   0|1|2   ACS723 baseline calibration
 *            ntc      0|1|2   NTC temperature range check
 *            encoder  0|1|2   Encoder connectivity
 *            can      0|1|2   CAN loopback
 *            mpu      0|1|2   MPU region verify
 *            canary   0|1|2   Stack canary plant
 *            ram      0|1|2   RAM pattern test
 *
 *          Execution-control keys:
 *            skipInit  0|1   skip Swc_RzcSelfTest_Init entirely
 *                            (uninitialized guard; Startup returns FAIL)
 *            initNull  0|1   call Swc_RzcSelfTest_Init(NULL_PTR)
 *                            (NULL-config guard; stays uninitialized)
 *
 *          The hardware callbacks are injected through the config passed to
 *          Init. A value of 2 pins the config slot to NULL so Startup's
 *          `pfn != NULL_PTR` guard takes its false branch. Multi-phase
 *          scripts reuse the config built from the first phase; each phase
 *          only re-pins the mock return values before running Startup once.
 *
 *          Output is a single JSON object on stdout:
 *            {"result":1,"resultMask":255,"preMask":0,
 *             "demTotal":0,"demSelfTestFail":0,"demZeroCal":0,
 *             "demEncoder":0,"demCanBusOff":0,
 *             "dioCh5":1,"dioCh6":1,"dioWrites":0,
 *             "pwmDir":0,"pwmDuty":0}
 *          where dem* fields are counts of Dem_ReportErrorStatus calls for
 *          each DTC that Swc_RzcSelfTest.c can report, dioCh5/dioCh6 are
 *          the last levels written to the R_EN/L_EN motor channels (0 after
 *          SelfTest_DisableMotor) and pwmDir/pwmDuty are the last
 *          IoHwAb_SetMotorPWM arguments.
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
#include "Swc_RzcSelfTest.h"
#include "IoHwAb.h"
#include "Dem.h"

#include "harness_common.h"

/* ==================================================================
 * Hardware check mocks — return E_OK / E_NOT_OK as pinned by the phase
 * ================================================================== */

static uint8_t mock_check_bts7960 = 1u;
static uint8_t mock_check_acs723  = 1u;
static uint8_t mock_check_ntc     = 1u;
static uint8_t mock_check_encoder = 1u;
static uint8_t mock_check_can     = 1u;
static uint8_t mock_check_mpu     = 1u;
static uint8_t mock_check_canary  = 1u;
static uint8_t mock_check_ram     = 1u;

Std_ReturnType mock_hw_bts7960(void)
{
    return mock_check_bts7960 ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_acs723(void)
{
    return mock_check_acs723 ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_ntc(void)
{
    return mock_check_ntc ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_encoder(void)
{
    return mock_check_encoder ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_can(void)
{
    return mock_check_can ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_mpu(void)
{
    return mock_check_mpu ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_canary(void)
{
    return mock_check_canary ? E_OK : E_NOT_OK;
}

Std_ReturnType mock_hw_ram(void)
{
    return mock_check_ram ? E_OK : E_NOT_OK;
}

/* ==================================================================
 * Dem_ReportErrorStatus mock — counts events per DTC id
 * ================================================================== */

static uint32_t dem_total;
static uint32_t dem_self_test_fail;
static uint32_t dem_zero_cal;
static uint32_t dem_encoder;
static uint32_t dem_can_bus_off;

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    (void)EventStatus;
    dem_total++;
    if (EventId == RZC_DTC_SELF_TEST_FAIL) {
        dem_self_test_fail++;
    } else if (EventId == RZC_DTC_ZERO_CAL) {
        dem_zero_cal++;
    } else if (EventId == RZC_DTC_ENCODER) {
        dem_encoder++;
    } else if (EventId == RZC_DTC_CAN_BUS_OFF) {
        dem_can_bus_off++;
    }
}

/* ==================================================================
 * Dio / IoHwAb mocks — observe SelfTest_DisableMotor
 * ================================================================== */

#define MOCK_DIO_MAX_CHANNELS 16u

static uint8_t  mock_dio_level[MOCK_DIO_MAX_CHANNELS];
static uint32_t mock_dio_write_count;
static uint32_t mock_pwm_dir;
static uint32_t mock_pwm_duty;

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    if (ChannelId < MOCK_DIO_MAX_CHANNELS) {
        mock_dio_level[ChannelId] = Level;
    }
    mock_dio_write_count++;
}

Std_ReturnType IoHwAb_SetMotorPWM(uint8 Direction, uint16 DutyCycle)
{
    mock_pwm_dir  = Direction;
    mock_pwm_duty = DutyCycle;
    return E_OK;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint8_t skip_init;
    uint8_t init_null;
    uint8_t bts7960;
    uint8_t acs723;
    uint8_t ntc;
    uint8_t encoder;
    uint8_t can;
    uint8_t mpu;
    uint8_t canary;
    uint8_t ram;
} Phase;

static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->bts7960 = 1u;
    p->acs723  = 1u;
    p->ntc     = 1u;
    p->encoder = 1u;
    p->can     = 1u;
    p->mpu     = 1u;
    p->canary  = 1u;
    p->ram     = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "skipInit") == 0)     p->skip_init = (uint8_t)val;
    else if (strcmp(key, "initNull") == 0) p->init_null = (uint8_t)val;
    else if (strcmp(key, "bts7960") == 0)  p->bts7960 = (uint8_t)val;
    else if (strcmp(key, "acs723") == 0)   p->acs723 = (uint8_t)val;
    else if (strcmp(key, "ntc") == 0)      p->ntc = (uint8_t)val;
    else if (strcmp(key, "encoder") == 0)  p->encoder = (uint8_t)val;
    else if (strcmp(key, "can") == 0)      p->can = (uint8_t)val;
    else if (strcmp(key, "mpu") == 0)      p->mpu = (uint8_t)val;
    else if (strcmp(key, "canary") == 0)   p->canary = (uint8_t)val;
    else if (strcmp(key, "ram") == 0)      p->ram = (uint8_t)val;
    return 0;
}

/* ==================================================================
 * Config building — one callback slot per diagnostic item.
 * Value 2 pins the slot to NULL (exercises the pfn != NULL guard false side).
 * ================================================================== */

static void build_config(const Phase* p, Swc_RzcSelfTest_CfgType* cfg)
{
    cfg->pfnBts7960 = (p->bts7960 == 2u) ? NULL_PTR : mock_hw_bts7960;
    cfg->pfnAcs723  = (p->acs723  == 2u) ? NULL_PTR : mock_hw_acs723;
    cfg->pfnNtc     = (p->ntc     == 2u) ? NULL_PTR : mock_hw_ntc;
    cfg->pfnEncoder = (p->encoder == 2u) ? NULL_PTR : mock_hw_encoder;
    cfg->pfnCan     = (p->can     == 2u) ? NULL_PTR : mock_hw_can;
    cfg->pfnMpu     = (p->mpu     == 2u) ? NULL_PTR : mock_hw_mpu;
    cfg->pfnCanary  = (p->canary  == 2u) ? NULL_PTR : mock_hw_canary;
    cfg->pfnRam     = (p->ram     == 2u) ? NULL_PTR : mock_hw_ram;
}

static void pin_mock_returns(const Phase* p)
{
    mock_check_bts7960 = (p->bts7960 == 1u) ? 1u : 0u;
    mock_check_acs723  = (p->acs723  == 1u) ? 1u : 0u;
    mock_check_ntc     = (p->ntc     == 1u) ? 1u : 0u;
    mock_check_encoder = (p->encoder == 1u) ? 1u : 0u;
    mock_check_can     = (p->can     == 1u) ? 1u : 0u;
    mock_check_mpu     = (p->mpu     == 1u) ? 1u : 0u;
    mock_check_canary  = (p->canary  == 1u) ? 1u : 0u;
    mock_check_ram     = (p->ram     == 1u) ? 1u : 0u;
}

/* ==================================================================
 * main
 * ================================================================== */

int main(void)
{
    Phase phases[HARNESS_MAX_PHASES];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t last_result = 0u;
    uint8_t pre_mask;
    Swc_RzcSelfTest_CfgType cfg;
    uint8_t do_init = 1u;

    /* Snapshot the static initial value before any Init / Startup run. */
    pre_mask = Swc_RzcSelfTest_GetResultMask();

    {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }

    /* Init once per harness run unless the first phase requests a guard
     * scenario. NULL-config and skip-init both leave the module
     * uninitialized so Startup takes its uninitialized guard. */
    if (phase_count > 0u) {
        if (phases[0].skip_init != 0u) {
            do_init = 0u;
        } else if (phases[0].init_null != 0u) {
            Swc_RzcSelfTest_Init(NULL_PTR);
            do_init = 0u;
        }
    }

    if (do_init != 0u) {
        if (phase_count == 0u) {
            return 2;
        }
        build_config(&phases[0], &cfg);
        Swc_RzcSelfTest_Init(&cfg);
    }

    for (pi = 0u; pi < phase_count; pi++) {
        pin_mock_returns(&phases[pi]);
        last_result = Swc_RzcSelfTest_Startup();
    }

    printf("{\"result\":%u,\"resultMask\":%u,\"preMask\":%u,"
           "\"demTotal\":%u,\"demSelfTestFail\":%u,\"demZeroCal\":%u,"
           "\"demEncoder\":%u,\"demCanBusOff\":%u,"
           "\"dioCh5\":%u,\"dioCh6\":%u,\"dioWrites\":%u,"
           "\"pwmDir\":%u,\"pwmDuty\":%u}\n",
           (unsigned)last_result,
           (unsigned)Swc_RzcSelfTest_GetResultMask(),
           (unsigned)pre_mask,
           (unsigned)dem_total,
           (unsigned)dem_self_test_fail,
           (unsigned)dem_zero_cal,
           (unsigned)dem_encoder,
           (unsigned)dem_can_bus_off,
           (unsigned)mock_dio_level[RZC_MOTOR_R_EN_CHANNEL],
           (unsigned)mock_dio_level[RZC_MOTOR_L_EN_CHANNEL],
           (unsigned)mock_dio_write_count,
           (unsigned)mock_pwm_dir,
           (unsigned)mock_pwm_duty);

    return 0;
}
