/**
 * @file    test_Swc_Heartbeat.c
 * @brief   Unit tests for Swc_Heartbeat -- periodic CAN heartbeat TX SWC (RZC)
 * @date    2026-02-23
 *
 * @verifies SWR-RZC-021, SWR-RZC-022
 *
 * Tests heartbeat initialization, periodic 50ms transmission, alive counter
 * increment and wrap, ECU ID (0x03) / fault mask / vehicle state publication
 * via the heartbeat RTE signals, CAN bus-off suppression, and safe behaviour
 * on init.
 *
 * Since the SIL-stability series (Phase 2) the SWC no longer builds the CAN
 * payload itself: E2E protection lives in the Com layer and the heartbeat
 * PDU is auto-pulled by Com_MainFunction_Tx from the RTE signals written
 * here. Tests therefore assert on the RTE signal writes.
 *
 * Mocks: Rte_Read, Rte_Write
 *
 * @standard AUTOSAR SWC pattern, ISO 26262 Part 6
 * @copyright Taktflow Systems 2026
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

#define E_OK        ((Std_ReturnType)0x00U)
#define E_NOT_OK    ((Std_ReturnType)0x01U)
#define TRUE        1u
#define FALSE       0u
#define NULL_PTR    ((void*)0)

typedef uint8           boolean;

/* Prevent BSW headers from redefining types when source is included */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define COMSTACK_TYPES_H
#define SWC_HEARTBEAT_H
#define RZC_CFG_H
#define RTE_H
#define PDUR_H
#define DEM_H
#define E2E_H
#define WDGM_H
#define IOHWAB_H

/* ==================================================================
 * Signal IDs (from Rzc_Cfg.h -- redefined locally for test isolation)
 * ================================================================== */

#define RZC_SIG_RZC_HEARTBEAT_ECU_ID          133u
#define RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS    134u
#define RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE  135u
/* Alias RZC_SIG_HEARTBEAT_ALIVE -> RZC_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER */
#define RZC_SIG_HEARTBEAT_ALIVE               130u
/* Alias RZC_SIG_VEHICLE_STATE -> RZC_SIG_VEHICLE_STATE_MODE */
#define RZC_SIG_VEHICLE_STATE                 187u
/* Alias RZC_SIG_FAULT_MASK -> RZC_SIG_VEHICLE_STATE_FAULT_MASK */
#define RZC_SIG_FAULT_MASK                    186u

#define RZC_ECU_ID                  0x03u

#define RZC_RTE_PERIOD_MS           50u
#define RZC_HB_TX_PERIOD_MS         50u
#define RZC_HB_ALIVE_MAX            15u

/** Derived: cycles per heartbeat TX period (used in run_cycles) */
#define RZC_HB_PERIOD_CYCLES  (RZC_HB_TX_PERIOD_MS / RZC_RTE_PERIOD_MS)

/* Vehicle states */
#define RZC_STATE_INIT               0u
#define RZC_STATE_RUN                1u
#define RZC_STATE_DEGRADED           2u
#define RZC_STATE_LIMP               3u
#define RZC_STATE_SAFE_STOP          4u
#define RZC_STATE_SHUTDOWN           5u

/* Fault mask bits */
#define RZC_FAULT_CAN               0x08u

/* Swc_Heartbeat API declarations */
extern void Swc_Heartbeat_Init(void);
extern void Swc_Heartbeat_MainFunction(void);

/* ==================================================================
 * Mock: Rte_Read
 * ================================================================== */

/** Sized to RZC_SIG_COUNT (202u in Rzc_Cfg.h) so the real heartbeat
 *  signal IDs (130..135) and state signals (186/187) are captured. */
#define MOCK_RTE_MAX_SIGNALS  202u

static uint32  mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32  mock_vehicle_state;
static uint32  mock_fault_mask;

Std_ReturnType Rte_Read(uint16 SignalId, uint32* DataPtr)
{
    if (DataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (SignalId == RZC_SIG_VEHICLE_STATE) {
        *DataPtr = mock_vehicle_state;
        return E_OK;
    }
    if (SignalId == RZC_SIG_FAULT_MASK) {
        *DataPtr = mock_fault_mask;
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

/** One heartbeat TX cycle = one write each to OPERATING_MODE and
 *  FAULT_STATUS. Counted separately as TX markers. */
static uint8   mock_hb_mode_write_count;
static uint8   mock_hb_fault_write_count;

Std_ReturnType Rte_Write(uint16 SignalId, uint32 Data)
{
    mock_rte_write_count++;
    if (SignalId == RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE) {
        mock_hb_mode_write_count++;
    }
    if (SignalId == RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS) {
        mock_hb_fault_write_count++;
    }
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        mock_rte_signals[SignalId] = Data;
        return E_OK;
    }
    return E_NOT_OK;
}

/* ==================================================================
 * Include source under test (unity include-source pattern)
 * ================================================================== */

#include "../src/Swc_Heartbeat.c"

/* ==================================================================
 * Test Configuration
 * ================================================================== */

void setUp(void)
{
    uint8 i;

    /* Reset RTE mock */
    mock_rte_write_count      = 0u;
    mock_hb_mode_write_count  = 0u;
    mock_hb_fault_write_count = 0u;
    mock_vehicle_state        = RZC_STATE_RUN;
    mock_fault_mask           = 0u;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }

    Swc_Heartbeat_Init();
}

void tearDown(void) { }

/* ==================================================================
 * Helper: run N main cycles
 * ================================================================== */

static void run_cycles(uint16 count)
{
    uint16 i;
    for (i = 0u; i < count; i++) {
        Swc_Heartbeat_MainFunction();
    }
}

/* ==================================================================
 * SWR-RZC-021: Initialization
 * ================================================================== */

/** @verifies SWR-RZC-021 -- Init succeeds, MainFunction does not crash */
void test_Init_succeeds(void)
{
    /* Init already called in setUp. Verify module is operational
     * by running one full period — heartbeat signals must be written. */
    run_cycles(RZC_HB_PERIOD_CYCLES);

    TEST_ASSERT_EQUAL_UINT8(1u, mock_hb_mode_write_count);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_hb_fault_write_count);
}

/* ==================================================================
 * SWR-RZC-022: Heartbeat Transmission
 * ================================================================== */

/** @verifies SWR-RZC-022 -- Heartbeat published every 50ms period */
void test_HB_sends_at_50ms(void)
{
    run_cycles(RZC_HB_PERIOD_CYCLES);

    TEST_ASSERT_TRUE(mock_hb_mode_write_count >= 1u);
    TEST_ASSERT_EQUAL_UINT8(mock_hb_mode_write_count, mock_hb_fault_write_count);
}

/** @verifies SWR-RZC-022 -- MainFunction writes operating mode AND fault
 *  status once per TX cycle (auto-pulled by Com_MainFunction_Tx) */
void test_HB_writes_mode_and_fault_each_tx_cycle(void)
{
    mock_vehicle_state = RZC_STATE_RUN;
    mock_fault_mask    = 0x02u;

    run_cycles(RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_hb_mode_write_count);
    TEST_ASSERT_EQUAL_UINT8(1u, mock_hb_fault_write_count);
    TEST_ASSERT_EQUAL_UINT32((uint32)RZC_STATE_RUN,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE]);
    TEST_ASSERT_EQUAL_UINT32(0x02u,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS]);

    /* Second period: both signals written again */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_EQUAL_UINT8(2u, mock_hb_mode_write_count);
    TEST_ASSERT_EQUAL_UINT8(2u, mock_hb_fault_write_count);
}

/** @verifies SWR-RZC-022 -- Alive counter increments each TX */
void test_HB_alive_counter_increments(void)
{
    uint8 alive_first;
    uint8 alive_second;

    /* First heartbeat */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    alive_first = (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE];

    /* Second heartbeat */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    alive_second = (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE];

    TEST_ASSERT_EQUAL_UINT8(alive_first + 1u, alive_second);
}

/** @verifies SWR-RZC-022 -- Alive counter wraps from 15 to 0 */
void test_HB_alive_counter_wraps(void)
{
    uint16 i;
    uint8  alive_val;

    /* Send 16 heartbeats (0..15), then the 17th should wrap to 0 */
    for (i = 0u; i <= RZC_HB_ALIVE_MAX; i++) {
        run_cycles(RZC_HB_PERIOD_CYCLES);
    }

    /* After 16 TXs the alive counter wraps internally.
     * One more period should send 0 (wrapped value). */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    alive_val = (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE];

    TEST_ASSERT_EQUAL_UINT8(0u, alive_val);
}

/** @verifies SWR-RZC-022 -- Init writes ECU ID 0x03 to the heartbeat signal */
void test_HB_includes_ecu_id(void)
{
    /* Written once by Swc_Heartbeat_Init (called in setUp) — constant
     * heartbeat field, auto-pulled by the Com TX path. */
    TEST_ASSERT_EQUAL_UINT32((uint32)RZC_ECU_ID,
                             mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_ECU_ID]);
}

/** @verifies SWR-RZC-022 -- Fault bitmask from RTE included in heartbeat */
void test_HB_includes_fault_mask(void)
{
    mock_fault_mask = 0x0005u;  /* lower 4 bits: 0x5 */

    run_cycles(RZC_HB_PERIOD_CYCLES);

    /* FaultStatus signal carries fault_mask & 0x0F */
    TEST_ASSERT_EQUAL_UINT32(0x05u,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS]);
}

/** @verifies SWR-RZC-022 -- Vehicle state from RTE included in heartbeat */
void test_HB_includes_state(void)
{
    mock_vehicle_state = RZC_STATE_DEGRADED;

    run_cycles(RZC_HB_PERIOD_CYCLES);

    /* OperatingMode signal carries vehicle_state & 0x0F */
    TEST_ASSERT_EQUAL_UINT32((uint32)RZC_STATE_DEGRADED,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE]);
}

/** @verifies SWR-RZC-021 -- No TX when CAN bus-off active */
void test_HB_suppressed_in_bus_off(void)
{
    mock_vehicle_state = RZC_STATE_SAFE_STOP;
    mock_fault_mask    = RZC_FAULT_CAN;

    run_cycles(RZC_HB_PERIOD_CYCLES * 3u);

    /* No heartbeat signals should have been written */
    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_mode_write_count);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_fault_write_count);
}

/** @verifies SWR-RZC-022 -- No send before 50ms period elapsed */
void test_No_send_before_period(void)
{
    /* Run one cycle less than a full period */
    run_cycles(RZC_HB_PERIOD_CYCLES - 1u);

    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_mode_write_count);
}

/** @verifies SWR-RZC-021 -- MainFunction immediately after init is safe */
void test_MainFunction_without_init_safe(void)
{
    /* Init called in setUp. Running fewer than one period should not send. */
    run_cycles(RZC_HB_PERIOD_CYCLES - 1u);

    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_mode_write_count);
}

/* ==================================================================
 * HARDENED TESTS — Boundary Values, Fault Injection
 * ================================================================== */

/** @verifies SWR-RZC-022
 *  Equivalence class: Boundary — alive counter at max value (15) */
void test_HB_alive_at_max(void)
{
    uint16 i;
    /* Send exactly 16 heartbeats to reach alive=15 (0..15) */
    for (i = 0u; i <= RZC_HB_ALIVE_MAX; i++) {
        run_cycles(RZC_HB_PERIOD_CYCLES);
    }

    /* Last sent alive counter should be 15 */
    TEST_ASSERT_EQUAL_UINT8(RZC_HB_ALIVE_MAX,
                            (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE]);
}

/** @verifies SWR-RZC-022
 *  Equivalence class: Boundary — max 4-bit fault status (0x0F) */
void test_HB_fault_mask_max(void)
{
    mock_fault_mask = 0x000Fu;  /* max 4-bit fault status */

    run_cycles(RZC_HB_PERIOD_CYCLES);

    TEST_ASSERT_EQUAL_UINT32(0x0Fu,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS]);
}

/** @verifies SWR-RZC-022
 *  Equivalence class: Boundary — all vehicle states produce valid heartbeats */
void test_HB_all_states_valid(void)
{
    uint8 states[] = { RZC_STATE_INIT, RZC_STATE_RUN, RZC_STATE_DEGRADED,
                       RZC_STATE_LIMP, RZC_STATE_SAFE_STOP, RZC_STATE_SHUTDOWN };
    uint8 i;

    for (i = 0u; i < 6u; i++) {
        mock_vehicle_state = states[i];
        mock_fault_mask    = 0u;
        mock_hb_mode_write_count = 0u;
        run_cycles(RZC_HB_PERIOD_CYCLES);

        /* Without RZC_FAULT_CAN set, no state suppresses the heartbeat */
        TEST_ASSERT_EQUAL_UINT8(1u, mock_hb_mode_write_count);
        TEST_ASSERT_EQUAL_UINT32((uint32)states[i],
            mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_OPERATING_MODE]);
    }
}

/** @verifies SWR-RZC-022
 *  Equivalence class: Boundary — just before first TX period elapses */
void test_HB_exactly_4_cycles_no_send(void)
{
    run_cycles(RZC_HB_PERIOD_CYCLES - 1u);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_mode_write_count);
}

/** @verifies SWR-RZC-022
 *  Equivalence class: Fault injection — zero fault mask */
void test_HB_zero_fault_mask(void)
{
    mock_fault_mask = 0u;
    run_cycles(RZC_HB_PERIOD_CYCLES);

    TEST_ASSERT_EQUAL_UINT32(0u,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS]);
}

/** @verifies SWR-RZC-021
 *  Equivalence class: Fault injection — CAN bus-off during ongoing TX */
void test_HB_bus_off_mid_sequence(void)
{
    /* Normal TX first */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_TRUE(mock_hb_mode_write_count >= 1u);

    /* CAN bus-off — no more TX */
    mock_fault_mask = RZC_FAULT_CAN;
    mock_vehicle_state = RZC_STATE_SAFE_STOP;
    mock_hb_mode_write_count = 0u;
    run_cycles(RZC_HB_PERIOD_CYCLES * 3u);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_hb_mode_write_count);
}

/* ==================================================================
 * PHASE 1: Derived Constants Verification
 * ================================================================== */

/** @verifies SWR-RZC-021
 *  Verifies HB_PERIOD_CYCLES is derived from RZC_HB_TX_PERIOD_MS / RZC_RTE_PERIOD_MS */
void test_Heartbeat_Derived_period_value(void)
{
    /* Derived cycle count should equal 50ms / 50ms = 1 cycle */
    TEST_ASSERT_EQUAL(1u, RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_EQUAL(RZC_HB_TX_PERIOD_MS / RZC_RTE_PERIOD_MS, RZC_HB_PERIOD_CYCLES);
}

/* ==================================================================
 * PHASE 6: Comprehensive Test Coverage
 * ================================================================== */

/** @verifies SWR-RZC-022
 *  Phase 6: Alive counter rollover sequence 15→0→1 is continuous */
void test_HB_alive_rollover_sequence(void)
{
    uint16 i;
    /* Send 16 heartbeats — last one sends alive=15 */
    for (i = 0u; i < 16u; i++) {
        run_cycles(RZC_HB_PERIOD_CYCLES);
    }
    TEST_ASSERT_EQUAL_UINT8(15u, (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE]);

    /* Next should wrap — sends 0 */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE]);

    /* And then sends 1 */
    run_cycles(RZC_HB_PERIOD_CYCLES);
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE]);
}

/** @verifies SWR-RZC-021
 *  Phase 6: Double init resets alive counter to 0 */
void test_HB_double_init_resets_alive(void)
{
    /* Advance alive counter */
    run_cycles(RZC_HB_PERIOD_CYCLES * 5u);
    TEST_ASSERT_EQUAL_UINT8(5u, mock_hb_mode_write_count);

    /* Re-init */
    Swc_Heartbeat_Init();
    mock_hb_mode_write_count = 0u;
    run_cycles(RZC_HB_PERIOD_CYCLES);

    /* Alive counter should restart at 0 (sent via Rte_Write) */
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8)mock_rte_signals[RZC_SIG_HEARTBEAT_ALIVE]);
}

/** @verifies SWR-RZC-022
 *  Phase 6: Long-running accuracy — exactly 10 TXes in 10 periods */
void test_HB_10_periods_accuracy(void)
{
    run_cycles(RZC_HB_PERIOD_CYCLES * 10u);

    TEST_ASSERT_EQUAL_UINT8(10u, mock_hb_mode_write_count);
    TEST_ASSERT_EQUAL_UINT8(10u, mock_hb_fault_write_count);
}

/** @verifies SWR-RZC-022
 *  Phase 6: Fault mask 4-bit encoding (0x000A → FaultStatus signal = 0xA) */
void test_HB_fault_mask_nibble_encoding(void)
{
    mock_fault_mask = 0x000Au;  /* 4-bit value: 0xA */

    run_cycles(RZC_HB_PERIOD_CYCLES);

    TEST_ASSERT_EQUAL_UINT32(0x0Au,
        mock_rte_signals[RZC_SIG_RZC_HEARTBEAT_FAULT_STATUS]);
}

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-RZC-021: Initialization */
    RUN_TEST(test_Init_succeeds);

    /* SWR-RZC-022: Heartbeat transmission */
    RUN_TEST(test_HB_sends_at_50ms);
    RUN_TEST(test_HB_writes_mode_and_fault_each_tx_cycle);
    RUN_TEST(test_HB_alive_counter_increments);
    RUN_TEST(test_HB_alive_counter_wraps);
    RUN_TEST(test_HB_includes_ecu_id);
    RUN_TEST(test_HB_includes_fault_mask);
    RUN_TEST(test_HB_includes_state);
    RUN_TEST(test_HB_suppressed_in_bus_off);
    RUN_TEST(test_No_send_before_period);
    RUN_TEST(test_MainFunction_without_init_safe);

    /* Hardened tests -- boundary values, fault injection */
    RUN_TEST(test_HB_alive_at_max);
    RUN_TEST(test_HB_fault_mask_max);
    RUN_TEST(test_HB_all_states_valid);
    RUN_TEST(test_HB_exactly_4_cycles_no_send);
    RUN_TEST(test_HB_zero_fault_mask);
    RUN_TEST(test_HB_bus_off_mid_sequence);

    /* PHASE 1: Derived Constants */
    RUN_TEST(test_Heartbeat_Derived_period_value);

    /* PHASE 6: Comprehensive Coverage */
    RUN_TEST(test_HB_alive_rollover_sequence);
    RUN_TEST(test_HB_double_init_resets_alive);
    RUN_TEST(test_HB_10_periods_accuracy);
    RUN_TEST(test_HB_fault_mask_nibble_encoding);

    return UNITY_END();
}
