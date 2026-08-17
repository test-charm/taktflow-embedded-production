/**
 * @file    cvc_selftest_harness.c
 * @brief   Native test harness for Swc_SelfTest (CVC startup self-test SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_SelfTest.c and drives its
 *          Swc_SelfTest_Startup / Swc_SelfTest_GetResults APIs through a
 *          phase script read from stdin. Each phase is one line of
 *          whitespace-separated key=value tokens that pins the pass/fail
 *          result of each of the seven hardware diagnostic checks and
 *          repeats Swc_SelfTest_Startup startupCount times.
 *
 *          Phase keys (each 0|1 = E_NOT_OK|E_OK, default 1 = pass):
 *            spi         SelfTest_Hw_SpiLoopback  (sensor SPI loopback)
 *            can         SelfTest_Hw_CanLoopback  (CAN controller loopback)
 *            nvm         SelfTest_Hw_NvmCheck     (NVM dual-bank CRC)
 *            oled        SelfTest_Hw_OledAck      (OLED I2C ACK, non-critical)
 *            mpu         SelfTest_Hw_MpuVerify    (MPU region verify)
 *            canary      SelfTest_Hw_CanaryCheck  (stack canary)
 *            ram         SelfTest_Hw_RamPattern   (RAM pattern test)
 *
 *          Each phase line runs Swc_SelfTest_Startup once; a multi-run
 *          scenario is expressed as multiple phase lines (the second run
 *          exercises the step-result bitmask reset at the start of Startup).
 *
 *          Output is a single JSON object on stdout:
 *            {"result":1,"stepResults":127,"preResults":0,
 *             "demTotal":0,"demSelfTestFail":0,"demNvmCrcFail":0,
 *             "demDisplayComm":0}
 *          where dem* fields are counts of Dem_ReportErrorStatus calls for
 *          each DTC that Swc_SelfTest.c can report.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_SelfTest.h"
#include "Dem.h"

#include "harness_common.h"

/* ==================================================================
 * Hardware check mocks — return E_OK / E_NOT_OK as pinned by the phase
 * ================================================================== */

static uint8_t mock_check_spi = 1u;
static uint8_t mock_check_can = 1u;
static uint8_t mock_check_nvm = 1u;
static uint8_t mock_check_oled = 1u;
static uint8_t mock_check_mpu = 1u;
static uint8_t mock_check_canary = 1u;
static uint8_t mock_check_ram = 1u;

Std_ReturnType SelfTest_Hw_SpiLoopback(void)
{
    return mock_check_spi ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_CanLoopback(void)
{
    return mock_check_can ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_NvmCheck(void)
{
    return mock_check_nvm ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_OledAck(void)
{
    return mock_check_oled ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_MpuVerify(void)
{
    return mock_check_mpu ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_CanaryCheck(void)
{
    return mock_check_canary ? E_OK : E_NOT_OK;
}

Std_ReturnType SelfTest_Hw_RamPattern(void)
{
    return mock_check_ram ? E_OK : E_NOT_OK;
}

/* ==================================================================
 * Dem_ReportErrorStatus mock — counts events per DTC id
 * ================================================================== */

static uint32_t dem_total;
static uint32_t dem_self_test_fail;
static uint32_t dem_nvm_crc_fail;
static uint32_t dem_display_comm;

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    (void)EventStatus;
    dem_total++;
    if (EventId == CVC_DTC_SELF_TEST_FAIL) {
        dem_self_test_fail++;
    } else if (EventId == CVC_DTC_NVM_CRC_FAIL) {
        dem_nvm_crc_fail++;
    } else if (EventId == CVC_DTC_DISPLAY_COMM) {
        dem_display_comm++;
    }
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint8_t  spi;
    uint8_t  can;
    uint8_t  nvm;
    uint8_t  oled;
    uint8_t  mpu;
    uint8_t  canary;
    uint8_t  ram;
} Phase;

static int run_phase(const Phase* p, int* last_result)
{
    mock_check_spi = p->spi;
    mock_check_can = p->can;
    mock_check_nvm = p->nvm;
    mock_check_oled = p->oled;
    mock_check_mpu = p->mpu;
    mock_check_canary = p->canary;
    mock_check_ram = p->ram;

    *last_result = (int)Swc_SelfTest_Startup();
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->spi = 1u;
    p->can = 1u;
    p->nvm = 1u;
    p->oled = 1u;
    p->mpu = 1u;
    p->canary = 1u;
    p->ram = 1u;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "spi") == 0)         p->spi = (uint8_t)val;
    else if (strcmp(key, "can") == 0)    p->can = (uint8_t)val;
    else if (strcmp(key, "nvm") == 0)    p->nvm = (uint8_t)val;
    else if (strcmp(key, "oled") == 0)   p->oled = (uint8_t)val;
    else if (strcmp(key, "mpu") == 0)    p->mpu = (uint8_t)val;
    else if (strcmp(key, "canary") == 0) p->canary = (uint8_t)val;
    else if (strcmp(key, "ram") == 0)    p->ram = (uint8_t)val;
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    int last_result = 0;
    uint8_t pre_results;

    /* Snapshot the static initial value before any Startup run. */
    pre_results = Swc_SelfTest_GetResults();

    /* ---- parse all phases ---- */
        {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi], &last_result) != 0) {
            return 2;
        }
    }

    printf("{\"result\":%d,\"stepResults\":%u,\"preResults\":%u,"
           "\"demTotal\":%u,\"demSelfTestFail\":%u,\"demNvmCrcFail\":%u,"
           "\"demDisplayComm\":%u}\n",
           last_result,
           (unsigned)Swc_SelfTest_GetResults(),
           (unsigned)pre_results,
           (unsigned)dem_total,
           (unsigned)dem_self_test_fail,
           (unsigned)dem_nvm_crc_fail,
           (unsigned)dem_display_comm);

    return 0;
}
