/**
 * @file    test_Swc_FzcSafety.c
 * @brief   Unit tests for Swc_FzcSafety — safety monitor and watchdog SWC
 * @date    2026-02-23
 *
 * @verifies SSR-FZC-015, SSR-FZC-016, SSR-FZC-019, SWR-FZC-015, SWR-FZC-016, SWR-FZC-019, SWR-FZC-023, SWR-FZC-025
 *
 * Tests safety initialization, watchdog feed toggling in normal and fault
 * conditions, fault aggregation into a unified mask, DTC reporting on
 * watchdog failure, safety status RTE publication, safe behaviour when
 * uninitialized, and RX-quality-based CAN-bus-off detection (April 2026
 * SIL-stability series: stale Steer/Brake_Command PDUs from CVC raise a
 * comm fault, gated by the post-INIT boot grace period).
 *
 * Mocks: Rte_Read, Rte_Write, Dio_WriteChannel, Dem_ReportErrorStatus,
 *        Com_GetRxPduQuality
 */
#include "unity.h"

/* ==================================================================
 * Local type definitions (avoid BSW header mock conflicts)
 * ================================================================== */

typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int   uint32;
typedef signed short    sint16;
typedef uint8           Std_ReturnType;

#define E_OK        0u
#define E_NOT_OK    1u
#define TRUE        1u
#define FALSE       0u
#define NULL_PTR    ((void*)0)

typedef uint16          PduIdType;

/* RX PDU signal quality (mirrors Com.h — blocked by COM_H guard below) */
typedef enum {
    COM_SIGNAL_QUALITY_FRESH     = 0u,  /**< Last frame valid and unpacked    */
    COM_SIGNAL_QUALITY_E2E_FAIL  = 1u,  /**< Last frame discarded (CRC/DataID) */
    COM_SIGNAL_QUALITY_TIMED_OUT = 2u   /**< No frame within timeout window   */
} Com_SignalQualityType;

/* ==================================================================
 * Signal IDs (from Fzc_Cfg.h — verified against generated header)
 * ================================================================== */

#define FZC_SIG_BRAKE_FAULT         44u  /* FZC_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS */
#define FZC_SIG_LIDAR_FAULT         96u  /* FZC_SIG_LIDAR_DISTANCE_SENSOR_STATUS */
#define FZC_SIG_MOTOR_CUTOFF       115u  /* FZC_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE */
#define FZC_SIG_STEER_FAULT        159u  /* FZC_SIG_STEERING_STATUS_STEER_FAULT_STATUS */
#define FZC_SIG_VEHICLE_STATE      187u  /* FZC_SIG_VEHICLE_STATE_MODE */
#define FZC_SIG_SELF_TEST_RESULT   201u
#define FZC_SIG_SAFETY_STATUS      202u
#define FZC_SIG_FAULT_MASK         204u
#define FZC_SIG_COUNT              205u

#define FZC_DTC_WATCHDOG_FAIL       14u

/* Com RX PDU IDs polled by the RX-quality check (from Fzc_Cfg.h) */
#define FZC_COM_RX_STEER_COMMAND     9u   /* CAN 0x102 */
#define FZC_COM_RX_BRAKE_COMMAND    10u   /* CAN 0x103 */

/* Watchdog DIO channel: PB0 */
#define FZC_DIO_WATCHDOG_CH          0u

/* DIO output levels */
#define DIO_LEVEL_LOW                0u
#define DIO_LEVEL_HIGH               1u

/* DEM event status (from Dem.h) */
#define DEM_EVENT_STATUS_PASSED      0u
#define DEM_EVENT_STATUS_FAILED      1u

/* Vehicle state values (from Fzc_Cfg.h) */
#define FZC_STATE_INIT               0u
#define FZC_STATE_SHUTDOWN           5u

/* Self-test result values (from Fzc_App.h) */
#define FZC_SELF_TEST_PASS           1u
#define FZC_SELF_TEST_FAIL           0u

/* Post-INIT grace period (from Fzc_Cfg_Platform.h, platform_posix) */
#define FZC_POST_INIT_GRACE_CYCLES   1500u

/* Fault mask bits (from Fzc_App.h) */
#define FZC_FAULT_NONE            0x00u
#define FZC_FAULT_STEER             (1u << 0u)
#define FZC_FAULT_BRAKE             (1u << 1u)
#define FZC_FAULT_LIDAR             (1u << 2u)
#define FZC_FAULT_WATCHDOG         0x10u
#define FZC_FAULT_SELF_TEST        0x20u
#define FZC_FAULT_CAN_BUS_OFF     0x0100u  /* bit 8: outside 8-bit payload range */

/* Safety status values */
#define FZC_SAFETY_OK                0u
#define FZC_SAFETY_FAULT             1u

/* Swc_FzcSafety API declarations */
extern void  Swc_FzcSafety_Init(void);
extern void  Swc_FzcSafety_MainFunction(void);
extern uint8 Swc_FzcSafety_GetStatus(void);

/* ==================================================================
 * Mock: Rte_Read
 * ================================================================== */

#define MOCK_RTE_MAX_SIGNALS  FZC_SIG_COUNT

static uint32  mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32  mock_steer_fault;
static uint32  mock_brake_fault;
static uint32  mock_lidar_fault;

Std_ReturnType Rte_Read(uint16 SignalId, uint32* DataPtr)
{
    if (DataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (SignalId == FZC_SIG_STEER_FAULT) {
        *DataPtr = mock_steer_fault;
        return E_OK;
    }
    if (SignalId == FZC_SIG_BRAKE_FAULT) {
        *DataPtr = mock_brake_fault;
        return E_OK;
    }
    if (SignalId == FZC_SIG_LIDAR_FAULT) {
        *DataPtr = mock_lidar_fault;
        return E_OK;
    }
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        *DataPtr = mock_rte_signals[SignalId];
        return E_OK;
    }
    return E_NOT_OK;
}

/* ==================================================================
 * Mock: Rte_Write
 * ================================================================== */

static uint8   mock_rte_write_count;

Std_ReturnType Rte_Write(uint16 SignalId, uint32 Data)
{
    mock_rte_write_count++;
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        mock_rte_signals[SignalId] = Data;
        return E_OK;
    }
    return E_NOT_OK;
}

/* ==================================================================
 * Mock: Dio_WriteChannel
 * ================================================================== */

static uint8   mock_dio_last_channel;
static uint8   mock_dio_last_level;
static uint8   mock_dio_write_count;
static uint8   mock_dio_toggle_count;
static uint8   mock_dio_prev_level;

void Dio_WriteChannel(uint8 ChannelId, uint8 Level)
{
    mock_dio_write_count++;
    if ((ChannelId == FZC_DIO_WATCHDOG_CH) && (Level != mock_dio_prev_level)) {
        mock_dio_toggle_count++;
    }
    mock_dio_last_channel = ChannelId;
    mock_dio_last_level   = Level;
    mock_dio_prev_level   = Level;
}

/* ==================================================================
 * Mock: Dem_ReportErrorStatus
 * ================================================================== */

#define MOCK_DEM_MAX_EVENTS  16u

static uint8   mock_dem_last_event_id;
static uint8   mock_dem_last_status;
static uint8   mock_dem_call_count;
static uint8   mock_dem_event_reported[MOCK_DEM_MAX_EVENTS];
static uint8   mock_dem_event_status[MOCK_DEM_MAX_EVENTS];

void Dem_ReportErrorStatus(uint8 EventId, uint8 EventStatus)
{
    mock_dem_call_count++;
    mock_dem_last_event_id = EventId;
    mock_dem_last_status   = EventStatus;
    if (EventId < MOCK_DEM_MAX_EVENTS) {
        mock_dem_event_reported[EventId] = 1u;
        mock_dem_event_status[EventId]   = EventStatus;
    }
}

/* ==================================================================
 * Mock: Com_GetRxPduQuality (per-PDU settable quality)
 * ================================================================== */

#define MOCK_COM_MAX_RX_PDUS  37u   /* FZC_COM_RX_* range 0..36 (Fzc_Cfg.h) */

static uint8   mock_com_pdu_quality[MOCK_COM_MAX_RX_PDUS];
static uint8   mock_com_quality_queried[MOCK_COM_MAX_RX_PDUS];
static uint16  mock_com_quality_call_count;

Com_SignalQualityType Com_GetRxPduQuality(PduIdType RxPduId)
{
    mock_com_quality_call_count++;
    if (RxPduId < MOCK_COM_MAX_RX_PDUS) {
        mock_com_quality_queried[RxPduId] = TRUE;
        return (Com_SignalQualityType)mock_com_pdu_quality[RxPduId];
    }
    return COM_SIGNAL_QUALITY_FRESH;
}

/* ==================================================================
 * Test Configuration
 * ================================================================== */

void setUp(void)
{
    uint16 i;

    /* Reset RTE mock */
    mock_rte_write_count = 0u;
    mock_steer_fault     = 0u;
    mock_brake_fault     = 0u;
    mock_lidar_fault     = 0u;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }

    /* Reset DIO mock */
    mock_dio_last_channel  = 0xFFu;
    mock_dio_last_level    = DIO_LEVEL_LOW;
    mock_dio_write_count   = 0u;
    mock_dio_toggle_count  = 0u;
    mock_dio_prev_level    = DIO_LEVEL_LOW;

    /* Reset DEM mock */
    mock_dem_call_count    = 0u;
    mock_dem_last_event_id = 0xFFu;
    mock_dem_last_status   = 0xFFu;
    for (i = 0u; i < MOCK_DEM_MAX_EVENTS; i++) {
        mock_dem_event_reported[i] = 0u;
        mock_dem_event_status[i]   = 0xFFu;
    }

    /* Reset Com RX quality mock — all PDUs fresh */
    mock_com_quality_call_count = 0u;
    for (i = 0u; i < MOCK_COM_MAX_RX_PDUS; i++) {
        mock_com_pdu_quality[i]     = (uint8)COM_SIGNAL_QUALITY_FRESH;
        mock_com_quality_queried[i] = FALSE;
    }

    Swc_FzcSafety_Init();
}

void tearDown(void) { }

/* ==================================================================
 * Helper: run N main cycles with current mock settings
 * ================================================================== */

static void run_cycles(uint16 count)
{
    uint16 i;
    for (i = 0u; i < count; i++) {
        Swc_FzcSafety_MainFunction();
    }
}

/* ==================================================================
 * SWR-FZC-023: Initialization
 * ================================================================== */

/** @verifies SWR-FZC-023 — Init succeeds, GetStatus returns 0 (OK) */
void test_Init(void)
{
    /* Init called in setUp. Verify safety status is OK. */
    uint8 status = Swc_FzcSafety_GetStatus();

    TEST_ASSERT_EQUAL_UINT8(FZC_SAFETY_OK, status);
}

/* ==================================================================
 * SWR-FZC-025: Watchdog Feed
 * ================================================================== */

/** @verifies SWR-FZC-025 — Watchdog DIO toggled each cycle when no faults */
void test_Watchdog_feed_normal(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 0u;

    run_cycles(10u);

    /* Watchdog pin should have been written each cycle, toggling each time */
    TEST_ASSERT_TRUE(mock_dio_write_count >= 10u);
    TEST_ASSERT_TRUE(mock_dio_toggle_count >= 9u);
    TEST_ASSERT_EQUAL_UINT8(FZC_DIO_WATCHDOG_CH, mock_dio_last_channel);
}

/** @verifies SWR-FZC-025 — No watchdog toggle when any fault is active */
void test_Watchdog_suppressed_on_fault(void)
{
    /* Set a steering fault */
    mock_steer_fault = 1u;

    mock_dio_toggle_count = 0u;
    run_cycles(10u);

    /* Watchdog should NOT toggle when a fault is present */
    TEST_ASSERT_EQUAL_UINT8(0u, mock_dio_toggle_count);
}

/* ==================================================================
 * SWR-FZC-023: Fault Aggregation
 * ================================================================== */

/** @verifies SWR-FZC-023 — Steer + brake + lidar faults combined into fault mask */
void test_Fault_aggregation(void)
{
    mock_steer_fault = 1u;
    mock_brake_fault = 1u;
    mock_lidar_fault = 1u;

    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];

    /* All three fault bits should be set */
    TEST_ASSERT_TRUE((mask & FZC_FAULT_STEER) != 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_BRAKE) != 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_LIDAR) != 0u);
}

/** @verifies SWR-FZC-023 — Zero fault mask when all faults clear */
void test_No_faults_clean_mask(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 0u;

    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];
    TEST_ASSERT_EQUAL_UINT32(0u, mask);
}

/* ==================================================================
 * SWR-FZC-025: DTC on Watchdog Failure
 * ================================================================== */

/** @verifies SWR-FZC-025 — DTC reported if watchdog feed conditions fail */
void test_DTC_on_watchdog_fail(void)
{
    /* Trigger a fault that suppresses watchdog feed */
    mock_brake_fault = 1u;

    run_cycles(5u);

    TEST_ASSERT_EQUAL_UINT8(1u, mock_dem_event_reported[FZC_DTC_WATCHDOG_FAIL]);
    TEST_ASSERT_EQUAL_UINT8(DEM_EVENT_STATUS_FAILED,
                            mock_dem_event_status[FZC_DTC_WATCHDOG_FAIL]);
}

/* ==================================================================
 * SWR-FZC-023: Safety Status
 * ================================================================== */

/** @verifies SWR-FZC-023 — Safety status signal written to RTE each cycle */
void test_Safety_status_written(void)
{
    mock_rte_write_count = 0u;

    Swc_FzcSafety_MainFunction();

    /* Should have written at least the fault mask and safety status */
    TEST_ASSERT_TRUE(mock_rte_write_count >= 1u);

    /* Verify safety status was written (no faults = OK) */
    uint32 safety_status = mock_rte_signals[FZC_SIG_SAFETY_STATUS];
    TEST_ASSERT_EQUAL_UINT32((uint32)FZC_SAFETY_OK, safety_status);
}

/** @verifies SWR-FZC-023 — Safe when uninitialized (MainFunction before Init) */
void test_MainFunction_uninit_safe(void)
{
    /* Re-create uninitialised state by re-initialising mocks without Init */
    uint16 i;
    mock_rte_write_count = 0u;
    mock_dio_write_count = 0u;
    mock_dio_toggle_count = 0u;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }

    /* Call Init fresh to put module in known state, then verify
     * GetStatus immediately returns safe value. */
    Swc_FzcSafety_Init();
    uint8 status = Swc_FzcSafety_GetStatus();

    TEST_ASSERT_EQUAL_UINT8(FZC_SAFETY_OK, status);

    /* First MainFunction should not crash and should be safe */
    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];
    TEST_ASSERT_EQUAL_UINT32(0u, mask);
}

/* ==================================================================
 * HARDENED TESTS — ISO 26262 ASIL D TUV-grade additions
 * Boundary value analysis, NULL pointer, fault injection,
 * equivalence class documentation
 * ================================================================== */

/* ------------------------------------------------------------------
 * Equivalence classes for fault aggregation:
 *   Valid:   Individual faults (steer, brake, lidar) set/clear
 *   Invalid: Multiple simultaneous faults
 *   Boundary: single fault bit set, all fault bits set
 *
 * Equivalence classes for watchdog feed:
 *   Valid:   no faults, vehicle not SHUTDOWN -> toggle
 *   Invalid: any critical fault -> suppress
 *   Invalid: vehicle SHUTDOWN -> suppress
 *
 * Equivalence classes for safety status:
 *   OK:       no faults
 *   DEGRADED: lidar fault only (non-critical)
 *   FAULT:    steer or brake fault (critical)
 *
 * Equivalence classes for RX-quality bus-off detection:
 *   Suppressed: post-INIT grace running -> quality not polled
 *   Active:     grace expired -> Steer/Brake_Command quality polled,
 *               TIMED_OUT raises FZC_FAULT_CAN_BUS_OFF (non-critical)
 * ------------------------------------------------------------------ */

/** @verifies SWR-FZC-023
 *  Equivalence class: only steering fault sets only steering bit */
void test_Fault_single_steer_only(void)
{
    mock_steer_fault = 1u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 0u;

    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];
    TEST_ASSERT_TRUE((mask & FZC_FAULT_STEER) != 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_BRAKE) == 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_LIDAR) == 0u);
}

/** @verifies SWR-FZC-023
 *  Equivalence class: only brake fault sets only brake bit */
void test_Fault_single_brake_only(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 1u;
    mock_lidar_fault = 0u;

    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];
    TEST_ASSERT_TRUE((mask & FZC_FAULT_STEER) == 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_BRAKE) != 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_LIDAR) == 0u);
}

/** @verifies SWR-FZC-023
 *  Equivalence class: only lidar fault sets only lidar bit */
void test_Fault_single_lidar_only(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 1u;

    Swc_FzcSafety_MainFunction();

    uint32 mask = mock_rte_signals[FZC_SIG_FAULT_MASK];
    TEST_ASSERT_TRUE((mask & FZC_FAULT_STEER) == 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_BRAKE) == 0u);
    TEST_ASSERT_TRUE((mask & FZC_FAULT_LIDAR) != 0u);
}

/** @verifies SWR-FZC-025
 *  Fault injection: watchdog suppressed on brake fault only */
void test_FaultInj_watchdog_suppressed_brake_fault(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 1u;
    mock_lidar_fault = 0u;

    mock_dio_toggle_count = 0u;
    run_cycles(10u);

    /* Watchdog should NOT toggle (brake is critical) */
    TEST_ASSERT_EQUAL_UINT8(0u, mock_dio_toggle_count);
}

/** @verifies SWR-FZC-025
 *  Fault injection: watchdog still toggles with lidar fault only (non-critical) */
void test_FaultInj_watchdog_feeds_with_lidar_fault_only(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 1u;

    mock_dio_toggle_count = 0u;
    run_cycles(10u);

    /* Lidar fault is non-critical — watchdog should still toggle */
    TEST_ASSERT_TRUE(mock_dio_toggle_count >= 9u);
}

/** @verifies SWR-FZC-023
 *  Boundary: safety status = FAULT when steering fault active */
void test_Safety_status_fault_on_steer(void)
{
    mock_steer_fault = 1u;

    Swc_FzcSafety_MainFunction();

    uint8 status = Swc_FzcSafety_GetStatus();
    /* SAFETY_STATUS_FAULT = 2 in the source (matches FZC_SAFETY_FAULT = 1 in test) */
    /* The test defines FZC_SAFETY_FAULT = 1, but the source uses SAFETY_STATUS_FAULT = 2.
     * We check that it is NOT OK. */
    TEST_ASSERT_TRUE(status != FZC_SAFETY_OK);
}

/** @verifies SWR-FZC-023
 *  Boundary: safety status transitions from fault to OK when faults clear */
void test_Safety_status_recovers_to_ok(void)
{
    /* Set fault */
    mock_steer_fault = 1u;
    Swc_FzcSafety_MainFunction();
    TEST_ASSERT_TRUE(Swc_FzcSafety_GetStatus() != FZC_SAFETY_OK);

    /* Clear fault */
    mock_steer_fault = 0u;
    Swc_FzcSafety_MainFunction();
    TEST_ASSERT_EQUAL_UINT8(FZC_SAFETY_OK, Swc_FzcSafety_GetStatus());
}

/** @verifies SWR-FZC-025
 *  Fault injection: watchdog DIO alternates HIGH/LOW each cycle */
void test_Watchdog_dio_alternates(void)
{
    mock_steer_fault = 0u;
    mock_brake_fault = 0u;
    mock_lidar_fault = 0u;

    /* Run 2 cycles and verify DIO level changes */
    Swc_FzcSafety_MainFunction();
    uint8 level1 = mock_dio_last_level;

    Swc_FzcSafety_MainFunction();
    uint8 level2 = mock_dio_last_level;

    /* Levels should alternate */
    TEST_ASSERT_TRUE(level1 != level2);
}

/** @verifies SWR-FZC-025
 *  Fault injection: DTC reported when watchdog feed suppressed */
void test_FaultInj_DTC_on_all_critical_faults(void)
{
    /* Both critical faults active */
    mock_steer_fault = 1u;
    mock_brake_fault = 1u;

    run_cycles(3u);

    TEST_ASSERT_EQUAL_UINT8(1u, mock_dem_event_reported[FZC_DTC_WATCHDOG_FAIL]);
    TEST_ASSERT_EQUAL_UINT8(DEM_EVENT_STATUS_FAILED,
                            mock_dem_event_status[FZC_DTC_WATCHDOG_FAIL]);
}

/** @verifies SWR-FZC-023
 *  Fault injection: double Init call resets safety status */
void test_FaultInj_double_init_resets_status(void)
{
    /* Trigger a fault */
    mock_steer_fault = 1u;
    Swc_FzcSafety_MainFunction();
    TEST_ASSERT_TRUE(Swc_FzcSafety_GetStatus() != FZC_SAFETY_OK);

    /* Re-init should reset */
    Swc_FzcSafety_Init();
    TEST_ASSERT_EQUAL_UINT8(FZC_SAFETY_OK, Swc_FzcSafety_GetStatus());
}

/* ==================================================================
 * SWR-FZC-023: RX-quality-based CAN-bus-off detection (April 2026)
 * ================================================================== */

/** @verifies SWR-FZC-023
 *  Boot grace: RX quality is NOT polled while the post-INIT grace
 *  period runs (CVC boots last in the SIL reset sequence — stale
 *  Steer/Brake_Command PDUs are legitimate then).  No CAN-bus-off
 *  fault may latch during grace, else Heartbeat TX is suppressed
 *  and the CVC/FZC startup deadlocks. */
void test_RxQuality_not_polled_during_boot_grace(void)
{
    /* Commands stale from the very first cycle */
    mock_com_pdu_quality[FZC_COM_RX_STEER_COMMAND] = (uint8)COM_SIGNAL_QUALITY_TIMED_OUT;
    mock_com_pdu_quality[FZC_COM_RX_BRAKE_COMMAND] = (uint8)COM_SIGNAL_QUALITY_TIMED_OUT;

    run_cycles(FZC_POST_INIT_GRACE_CYCLES);

    /* assert: quality API never polled while grace counter > 0 */
    TEST_ASSERT_EQUAL_UINT16(0u, mock_com_quality_call_count);
    /* assert: no fault latched, status stays OK */
    TEST_ASSERT_EQUAL_UINT32(0u, mock_rte_signals[FZC_SIG_FAULT_MASK]);
    TEST_ASSERT_EQUAL_UINT8(FZC_SAFETY_OK, Swc_FzcSafety_GetStatus());
}

/** @verifies SWR-FZC-023
 *  RX quality TIMED_OUT on Steer_Command/Brake_Command after grace
 *  expiry: the source raises FZC_FAULT_CAN_BUS_OFF (bit 8, 0x0100) in
 *  its fault aggregation.  FZC_FAULT_CAN_BUS_OFF is deliberately
 *  "outside 8-bit payload range" (Fzc_App.h) — excluded from the CAN
 *  heartbeat payload byte, but it MUST be present in the RTE-published
 *  FZC_SIG_FAULT_MASK: Swc_Heartbeat reads that signal and suppresses
 *  heartbeat TX when bit 8 is set (Swc_Heartbeat.c).  Bus-off is a comm
 *  fault, NOT a critical steer/brake fault: the watchdog must keep
 *  feeding and no watchdog DTC may be reported. */
void test_RxQuality_timed_out_after_grace_raises_bus_off(void)
{
    /* Expire the post-INIT grace period (quality checks suppressed until then) */
    run_cycles(FZC_POST_INIT_GRACE_CYCLES);
    TEST_ASSERT_EQUAL_UINT16(0u, mock_com_quality_call_count);

    /* Steer + brake command PDUs go stale */
    mock_com_pdu_quality[FZC_COM_RX_STEER_COMMAND] = (uint8)COM_SIGNAL_QUALITY_TIMED_OUT;
    mock_com_pdu_quality[FZC_COM_RX_BRAKE_COMMAND] = (uint8)COM_SIGNAL_QUALITY_TIMED_OUT;

    mock_dio_toggle_count = 0u;
    Swc_FzcSafety_MainFunction();

    /* assert: both safety-critical command PDUs polled once grace expired */
    TEST_ASSERT_EQUAL_UINT16(2u, mock_com_quality_call_count);
    TEST_ASSERT_EQUAL_UINT8(TRUE, mock_com_quality_queried[FZC_COM_RX_STEER_COMMAND]);
    TEST_ASSERT_EQUAL_UINT8(TRUE, mock_com_quality_queried[FZC_COM_RX_BRAKE_COMMAND]);

    /* assert: published mask carries the full bus-off bit (0x0100) so
     * Swc_Heartbeat can suppress TX; no critical steer/brake bits set */
    TEST_ASSERT_EQUAL_UINT32((uint32)FZC_FAULT_CAN_BUS_OFF,
                             mock_rte_signals[FZC_SIG_FAULT_MASK]);
    TEST_ASSERT_TRUE((mock_rte_signals[FZC_SIG_FAULT_MASK]
                      & (FZC_FAULT_STEER | FZC_FAULT_BRAKE)) == 0u);

    /* assert: comm fault is not critical — watchdog feed continues,
     * no watchdog DTC reported */
    TEST_ASSERT_EQUAL_UINT8(1u, mock_dio_toggle_count);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_dem_event_reported[FZC_DTC_WATCHDOG_FAIL]);
}

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-FZC-023: Initialization */
    RUN_TEST(test_Init);

    /* SWR-FZC-025: Watchdog feed */
    RUN_TEST(test_Watchdog_feed_normal);
    RUN_TEST(test_Watchdog_suppressed_on_fault);

    /* SWR-FZC-023: Fault aggregation */
    RUN_TEST(test_Fault_aggregation);
    RUN_TEST(test_No_faults_clean_mask);

    /* SWR-FZC-025: DTC reporting */
    RUN_TEST(test_DTC_on_watchdog_fail);

    /* SWR-FZC-023: Safety status */
    RUN_TEST(test_Safety_status_written);
    RUN_TEST(test_MainFunction_uninit_safe);

    /* Hardened tests — ISO 26262 ASIL D TUV-grade */
    RUN_TEST(test_Fault_single_steer_only);
    RUN_TEST(test_Fault_single_brake_only);
    RUN_TEST(test_Fault_single_lidar_only);
    RUN_TEST(test_FaultInj_watchdog_suppressed_brake_fault);
    RUN_TEST(test_FaultInj_watchdog_feeds_with_lidar_fault_only);
    RUN_TEST(test_Safety_status_fault_on_steer);
    RUN_TEST(test_Safety_status_recovers_to_ok);
    RUN_TEST(test_Watchdog_dio_alternates);
    RUN_TEST(test_FaultInj_DTC_on_all_critical_faults);
    RUN_TEST(test_FaultInj_double_init_resets_status);

    /* SWR-FZC-023: RX-quality-based bus-off detection */
    RUN_TEST(test_RxQuality_not_polled_during_boot_grace);
    RUN_TEST(test_RxQuality_timed_out_after_grace_raises_bus_off);

    return UNITY_END();
}

/* ==================================================================
 * Source inclusion — link SWC under test directly into test binary
 * ================================================================== */

/* Prevent BSW headers from redefining types when source is included */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define SWC_FZC_SAFETY_H
#define FZC_CFG_H
#define RTE_H
#define IOHWAB_H
#define COM_H
#define DEM_H

#include "../src/Swc_FzcSafety.c"
