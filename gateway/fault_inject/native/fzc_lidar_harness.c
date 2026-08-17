/**
 * @file    fzc_lidar_harness.c
 * @brief   Native test harness for Swc_Lidar (FZC TFMini-S lidar SWC)
 * @date    2026-08-16
 *
 * @details Links the REAL production Swc_Lidar.c and drives
 *          Swc_Lidar_MainFunction() through a phase script read from
 *          stdin. Each phase is one line of whitespace-separated key=value
 *          tokens. The harness feeds TFMini-S UART frames through the
 *          Uart_ReadRxData mock, and records RTE / DEM outputs.
 *
 *          Phase keys:
 *            cycles        uint32  (default 1) MainFunction calls
 *            skipInit      0|1     skip Swc_Lidar_Init (uninitialized guard)
 *            initNull      0|1     call Swc_Lidar_Init(NULL) (NULL-config guard)
 *            distCm        uint32  distance in cm for the injected frame
 *            signal        uint32  signal strength for the injected frame
 *            noFrame       0|1     feed no UART bytes (timeout path)
 *            badChecksum   0|1     corrupt the frame checksum byte
 *            garbageHeader 0|1     feed 32 non-header bytes (sync fail)
 *            partialFrame  0|1     feed header + 3 bytes only (incomplete)
 *            uartFailAt    uint32  (default 0=never) fail UART reads at/after
 *                                  this call index: 1=fail sync scan read,
 *                                  3=fail the 7-byte payload read (driver error)
 *            getDist       0|1     call Swc_Lidar_GetDistance at end of phase
 *            getDistNull   0|1     call Swc_Lidar_GetDistance(NULL) at end
 *
 *          Output is a single JSON object on stdout:
 *            {"distance":..,"signal":..,"zone":..,"fault":..,
 *             "demTimeout":..,"demChecksum":..,"demStuck":..,"demSignalLow":..,
 *             "getDistStatus":..,"getDist":..}
 *          dem* values: 0=PASSED, 1=FAILED, -1=not reported this run.
 *          getDistStatus: 0=E_OK, 1=E_NOT_OK.
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Fzc_Cfg.h"
#include "Swc_Lidar.h"
#include "Uart.h"
#include "Rte.h"
#include "Dem.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_DEM_MAX_EVENTS  32u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static int8_t   mock_dem_status[MOCK_DEM_MAX_EVENTS]; /* per DTC id, -1 = not reported */

/* UART circular-buffer mock (matches test_Swc_Lidar_asilc.c semantics) */
static uint8_t  mock_uart_buf[UART_RX_BUF_SIZE];
static uint8_t  mock_uart_available;  /* total bytes loaded */
static uint8_t  mock_uart_pos;        /* next unread byte index */
static uint32_t mock_uart_read_call;  /* read call counter (1-based) */
static uint32_t mock_uart_fail_at;    /* fail reads at/after this call (0=never) */

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

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    if (EventId < MOCK_DEM_MAX_EVENTS) {
        mock_dem_status[EventId] = (int8_t)EventStatus;
    }
}

Std_ReturnType Uart_ReadRxData(uint8* Buffer, uint8 Length, uint8* BytesRead)
{
    uint8 i;
    uint8 to_read;
    uint8 remaining;

    mock_uart_read_call++;

    if ((Buffer == NULL_PTR) || (BytesRead == NULL_PTR)) {
        return E_NOT_OK;
    }

    if ((mock_uart_fail_at != 0u) && (mock_uart_read_call >= mock_uart_fail_at)) {
        *BytesRead = 0u;
        return E_NOT_OK;
    }

    if (mock_uart_pos < mock_uart_available) {
        remaining = (uint8)(mock_uart_available - mock_uart_pos);
    } else {
        remaining = 0u;
    }

    to_read = (Length < remaining) ? Length : remaining;
    for (i = 0u; i < to_read; i++) {
        Buffer[i] = mock_uart_buf[mock_uart_pos + i];
    }
    mock_uart_pos = (uint8)(mock_uart_pos + to_read);
    *BytesRead = to_read;

    return E_OK;
}

/* ==================================================================
 * Helpers
 * ================================================================== */

/* Build a TFMini-S 9-byte frame in the mock UART buffer */
static void build_tfmini_frame(uint16 dist_cm, uint16 strength, uint8 bad_checksum)
{
    uint8 checksum = 0u;
    uint8 i;

    mock_uart_buf[0] = FZC_LIDAR_HEADER_BYTE;  /* Header byte 1 */
    mock_uart_buf[1] = FZC_LIDAR_HEADER_BYTE;  /* Header byte 2 */
    mock_uart_buf[2] = (uint8)(dist_cm & 0xFFu);         /* Dist_L */
    mock_uart_buf[3] = (uint8)((dist_cm >> 8u) & 0xFFu); /* Dist_H */
    mock_uart_buf[4] = (uint8)(strength & 0xFFu);         /* Strength_L */
    mock_uart_buf[5] = (uint8)((strength >> 8u) & 0xFFu); /* Strength_H */
    mock_uart_buf[6] = 0x00u;  /* Reserved / Temp_L */
    mock_uart_buf[7] = 0x00u;  /* Reserved / Temp_H */

    /* Checksum: low byte of sum of bytes 0-7 */
    for (i = 0u; i < 8u; i++) {
        checksum = (uint8)(checksum + mock_uart_buf[i]);
    }
    mock_uart_buf[8] = (bad_checksum != 0u) ? (uint8)(checksum + 1u) : checksum;

    mock_uart_available = FZC_LIDAR_FRAME_SIZE;
    mock_uart_pos       = 0u;
}

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  init_null;
    uint32_t dist_cm;
    uint32_t signal;
    uint8_t  no_frame;
    uint8_t  bad_checksum;
    uint8_t  garbage_header;
    uint8_t  partial_frame;
    uint32_t uart_fail_at;
    uint8_t  get_dist;
    uint8_t  get_dist_null;
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
    mock_uart_available = 0u;
    mock_uart_pos       = 0u;
    mock_uart_read_call = 0u;
    mock_uart_fail_at   = 0u;
}

static void run_phase(const Phase* p)
{
    uint32_t i;

    for (i = 0u; i < p->cycles; i++) {
        mock_uart_read_call = 0u;
        mock_uart_fail_at   = p->uart_fail_at;
        if (p->no_frame != 0u) {
            mock_uart_available = 0u;
            mock_uart_pos       = 0u;
        } else if (p->garbage_header != 0u) {
            uint8_t j;
            for (j = 0u; j < 32u; j++) {
                mock_uart_buf[j] = 0x00u;
            }
            mock_uart_available = 32u;
            mock_uart_pos       = 0u;
        } else if (p->partial_frame != 0u) {
            mock_uart_buf[0] = FZC_LIDAR_HEADER_BYTE;
            mock_uart_buf[1] = FZC_LIDAR_HEADER_BYTE;
            mock_uart_buf[2] = 0x01u;
            mock_uart_buf[3] = 0x00u;
            mock_uart_buf[4] = 0x00u;
            mock_uart_available = 5u;  /* header + 3 bytes only */
            mock_uart_pos       = 0u;
        } else {
            build_tfmini_frame((uint16)p->dist_cm, (uint16)p->signal,
                               (uint8_t)(p->bad_checksum != 0u));
        }
        Swc_Lidar_MainFunction();
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
    if (strcmp(key, "cycles") == 0)          p->cycles = harness_parse_uint(value);
    else if (strcmp(key, "skipInit") == 0)   p->skip_init = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "initNull") == 0)   p->init_null = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "distCm") == 0)     p->dist_cm = harness_parse_uint(value);
    else if (strcmp(key, "signal") == 0)     p->signal = harness_parse_uint(value);
    else if (strcmp(key, "noFrame") == 0)    p->no_frame = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "badChecksum") == 0) p->bad_checksum = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "garbageHeader") == 0) p->garbage_header = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "partialFrame") == 0) p->partial_frame = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "uartFailAt") == 0) p->uart_fail_at = harness_parse_uint(value);
    else if (strcmp(key, "getDist") == 0)   p->get_dist = (uint8_t)harness_parse_uint(value);
    else if (strcmp(key, "getDistNull") == 0) p->get_dist_null = (uint8_t)harness_parse_uint(value);
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;
    uint16_t get_dist_out = 0u;
    Std_ReturnType get_dist_ret = E_NOT_OK;
    Swc_Lidar_ConfigType cfg;
    static const Swc_Lidar_ConfigType* cfg_ptr = NULL_PTR;

    /* Production lidar config (matches main.c / Fzc_App.h) */
    cfg.warnDistCm      = FZC_LIDAR_WARN_CM;
    cfg.brakeDistCm     = FZC_LIDAR_BRAKE_CM;
    cfg.emergencyDistCm = FZC_LIDAR_EMERGENCY_CM;
    cfg.timeoutMs       = FZC_LIDAR_TIMEOUT_MS;
    cfg.stuckCycles     = FZC_LIDAR_STUCK_CYCLES;
    cfg.rangeMinCm      = FZC_LIDAR_RANGE_MIN_CM;
    cfg.rangeMaxCm      = FZC_LIDAR_RANGE_MAX_CM;
    cfg.signalMin       = FZC_LIDAR_SIGNAL_MIN;
    cfg.degradeCycles   = FZC_LIDAR_DEGRADE_CYCLES;
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
            Swc_Lidar_Init(NULL_PTR);
        } else {
            Swc_Lidar_Init(cfg_ptr);
        }
    }

    for (pi = 0u; pi < phase_count; pi++) {
        run_phase(&phases[pi]);
    }

    /* GetDistance evaluated at the end of the LAST phase that requested it.
     * getDistNull forces the NULL-pointer path (E_NOT_OK). */
    for (pi = 0u; pi < phase_count; pi++) {
        if (phases[pi].get_dist_null != 0u) {
            (void)Swc_Lidar_GetDistance(NULL_PTR);
        }
        if (phases[pi].get_dist != 0u) {
            get_dist_ret = Swc_Lidar_GetDistance(&get_dist_out);
        }
    }

    printf("{\"distance\":%u,\"signal\":%u,\"zone\":%u,\"fault\":%u,"
           "\"demTimeout\":%d,\"demChecksum\":%d,\"demStuck\":%d,\"demSignalLow\":%d,"
           "\"getDistStatus\":%u,\"getDist\":%u}\n",
           (unsigned)mock_rte_signals[FZC_SIG_LIDAR_DIST],
           (unsigned)mock_rte_signals[FZC_SIG_LIDAR_SIGNAL],
           (unsigned)mock_rte_signals[FZC_SIG_LIDAR_ZONE],
           (unsigned)mock_rte_signals[FZC_SIG_LIDAR_FAULT],
           (int)mock_dem_status[FZC_DTC_LIDAR_TIMEOUT],
           (int)mock_dem_status[FZC_DTC_LIDAR_CHECKSUM],
           (int)mock_dem_status[FZC_DTC_LIDAR_STUCK],
           (int)mock_dem_status[FZC_DTC_LIDAR_SIGNAL_LOW],
           (unsigned)get_dist_ret,
           (unsigned)get_dist_out);

    return 0;
}
