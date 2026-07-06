/**
 * @file    test_Swc_Heartbeat.c
 * @brief   Unit tests for Heartbeat TX/RX monitoring SWC
 * @date    2026-02-21
 *
 * @verifies SSR-CVC-017, SSR-FZC-018, SSR-RZC-010, SWR-CVC-017, SWR-CVC-021, SWR-CVC-022, SWR-FZC-018, SWR-RZC-010
 *
 * Tests heartbeat TX timing (50ms boundary), alive counter wrap, the
 * OperatingMode RTE write auto-pulled by Com TX, WdgM alive checkpoints,
 * RX indication flags, and the post-INIT-grace comm status reset.
 *
 * NOTE (April 2026 SIL-stability series): comm timeout monitoring and DTC
 * reporting moved from this SWC into the Com_MainFunction_Rx deadline
 * monitor (CommStatusRteSignalId in the RX PDU config). Heartbeat frame
 * assembly moved to Swc_CvcCom / Com auto-pull. This harness therefore
 * asserts only the behavior that remains in Swc_Heartbeat.c.
 */
#include "unity.h"

/* ====================================================================
 * Local type definitions (self-contained test — no BSW headers)
 * ==================================================================== */

typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef uint8          Std_ReturnType;
typedef uint8          boolean;

#define E_OK      0u
#define E_NOT_OK  1u
#define TRUE      1u
#define FALSE     0u
#define NULL_PTR  ((void*)0)
#define STD_HIGH  0x01u
#define STD_LOW   0x00u

/* Prevent BSW headers from redefining types */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define SWC_HEARTBEAT_H
#define CVC_CFG_H

/* Prevent real Dem.h / E2E.h / WdgM.h from being pulled in through Swc_Heartbeat.c —
 * the mocks below provide the required function definitions. */
#define DEM_H
#define E2E_H
#define WDGM_H
#define PDUR_H
#define COMSTACK_TYPES_H
#define CANIF_H

/* PduR / ComStack types needed by Com.h (included by Swc_Heartbeat.c) */
typedef uint16 PduIdType;
typedef uint16 PduLengthType;
typedef struct {
    uint8          *SduDataPtr;
    PduLengthType   SduLength;
} PduInfoType;

/* E2E types needed by E2E_Sm.h (E2E.h itself is blocked) */
typedef struct { uint8 DataId; uint8 MaxDeltaCounter; uint16 DataLength; } E2E_ConfigType;
typedef struct { uint8 Counter; } E2E_StateType;
typedef enum {
    E2E_STATUS_OK          = 0u,
    E2E_STATUS_REPEATED    = 1u,
    E2E_STATUS_WRONG_SEQ   = 2u,
    E2E_STATUS_ERROR       = 3u,
    E2E_STATUS_NO_NEW_DATA = 4u
} E2E_CheckStatusType;

/* WdgM type needed by Swc_Heartbeat.c */
typedef uint8 WdgM_SupervisedEntityIdType;

/* Signal IDs (must match Cvc_Cfg.h / Cvc_App.h aliases) */
#define CVC_SIG_FZC_COMM_STATUS   23u
#define CVC_SIG_RZC_COMM_STATUS   24u
#define CVC_SIG_VEHICLE_STATE     20u
/* CVC_Heartbeat.OperatingMode — generated value from Cvc_Cfg.h.
 * Written at each TX boundary; auto-pulled by Com_MainFunction_Tx. */
#define CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE  56u
#define CVC_ECU_ID_CVC          0x01u
#define CVC_ECU_ID_FZC          0x02u
#define CVC_ECU_ID_RZC          0x03u
#define CVC_RTE_PERIOD_MS        10u
#define CVC_HB_TX_PERIOD_MS      50u
#define CVC_HB_ALIVE_MAX         15u
#define CVC_COMM_OK                0u
#define CVC_COMM_TIMEOUT           1u

/* E2E SM per-ECU configurations (must match Cvc_Cfg.h) */
#define CVC_E2E_SM_FZC_WINDOW        4u
#define CVC_E2E_SM_FZC_MIN_OK_INIT   2u
#define CVC_E2E_SM_FZC_MAX_ERR_VALID 1u
#define CVC_E2E_SM_FZC_MIN_OK_INV    3u
#define CVC_E2E_SM_RZC_WINDOW        6u
#define CVC_E2E_SM_RZC_MIN_OK_INIT   3u
#define CVC_E2E_SM_RZC_MAX_ERR_VALID 2u
#define CVC_E2E_SM_RZC_MIN_OK_INV    3u

/* ====================================================================
 * Mock: WdgM_CheckpointReached
 * ==================================================================== */

static uint8 mock_wdgm_checkpoint_count;
static uint8 mock_wdgm_last_se_id;

Std_ReturnType WdgM_CheckpointReached(WdgM_SupervisedEntityIdType SEId)
{
    mock_wdgm_checkpoint_count++;
    mock_wdgm_last_se_id = SEId;
    return E_OK;
}

/* ====================================================================
 * Mock: Rte_Write
 * ==================================================================== */

static uint16 mock_rte_write_sig_ids[64];
static uint32 mock_rte_write_vals[64];
static uint8  mock_rte_write_count;

Std_ReturnType Rte_Write(uint16 SignalId, uint32 Data)
{
    if (mock_rte_write_count < 64u) {
        mock_rte_write_sig_ids[mock_rte_write_count] = SignalId;
        mock_rte_write_vals[mock_rte_write_count]    = Data;
    }
    mock_rte_write_count++;
    return E_OK;
}

/* ====================================================================
 * Mock: Rte_Read
 * ==================================================================== */

static uint32 mock_rte_vehicle_state;

Std_ReturnType Rte_Read(uint16 SignalId, uint32* DataPtr)
{
    if (DataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (SignalId == CVC_SIG_VEHICLE_STATE) {
        *DataPtr = mock_rte_vehicle_state;
    } else {
        *DataPtr = 0u;
    }
    return E_OK;
}

/* ====================================================================
 * Include E2E SM + SWC under test (source inclusion for test build)
 * ==================================================================== */

#include "../../../bsw/services/E2E/src/E2E_Sm.c"
#include "../src/Swc_Heartbeat.c"

/* ====================================================================
 * Helper: run N MainFunction cycles
 * ==================================================================== */

static void run_cycles(uint8 n)
{
    uint8 i;
    for (i = 0u; i < n; i++) {
        Swc_Heartbeat_MainFunction();
    }
}

/* ====================================================================
 * Helper: count RTE writes of a given signal, remember last value
 * ==================================================================== */

static uint8 count_rte_writes(uint16 sigId, uint32* lastVal)
{
    uint8 i;
    uint8 hits = 0u;
    for (i = 0u; i < mock_rte_write_count; i++) {
        if (i < 64u) {
            if (mock_rte_write_sig_ids[i] == sigId) {
                hits++;
                if (lastVal != NULL_PTR) {
                    *lastVal = mock_rte_write_vals[i];
                }
            }
        }
    }
    return hits;
}

/* ====================================================================
 * setUp / tearDown
 * ==================================================================== */

void setUp(void)
{
    uint8 i;

    mock_wdgm_checkpoint_count = 0u;
    mock_wdgm_last_se_id   = 0xFFu;
    mock_rte_write_count   = 0u;
    mock_rte_vehicle_state = 1u; /* CVC_STATE_RUN */

    for (i = 0u; i < 64u; i++) {
        mock_rte_write_sig_ids[i] = 0xFFu;
        mock_rte_write_vals[i]    = 0xFFu;
    }

    Swc_Heartbeat_Init();
}

void tearDown(void) { }

/* ====================================================================
 * SWR-CVC-021: Heartbeat TX timing (50ms boundary via WdgM checkpoint)
 * ==================================================================== */

/** @verifies SWR-CVC-021
 *  After init, both comm statuses default to TIMEOUT (safe default —
 *  heartbeats must be received before declaring OK). Timeout supervision
 *  itself now lives in the Com RX deadline monitor. */
void test_Heartbeat_Init_comm_status_timeout(void)
{
    TEST_ASSERT_EQUAL(CVC_COMM_TIMEOUT, fzc_comm_status);
    TEST_ASSERT_EQUAL(CVC_COMM_TIMEOUT, rzc_comm_status);
    TEST_ASSERT_EQUAL(0u, alive_counter);
    TEST_ASSERT_EQUAL(0u, tx_timer);
}

/** @verifies SWR-CVC-021 */
void test_Heartbeat_TX_every_50ms(void)
{
    /* 50ms = 5 calls at 10ms each — exactly one TX boundary */
    run_cycles(5u);

    TEST_ASSERT_EQUAL(1u, mock_wdgm_checkpoint_count);

    /* OperatingMode written exactly once at the boundary */
    TEST_ASSERT_EQUAL(1u,
        count_rte_writes(CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE, NULL_PTR));
}

/** @verifies SWR-CVC-021 */
void test_Heartbeat_No_TX_before_50ms(void)
{
    /* Only 4 cycles (40ms) — no TX boundary, no writes, no checkpoint */
    run_cycles(4u);

    TEST_ASSERT_EQUAL(0u, mock_wdgm_checkpoint_count);
    TEST_ASSERT_EQUAL(0u, mock_rte_write_count);
}

/** @verifies SWR-CVC-021
 *  Equivalence class: VALID — boundary: exactly 5 cycles = 50ms, TX fires
 *  Boundary: tx_timer = 5 (exactly at threshold) */
void test_Heartbeat_TX_at_exactly_5_cycles(void)
{
    run_cycles(5u);

    TEST_ASSERT_EQUAL(1u, mock_wdgm_checkpoint_count);
    TEST_ASSERT_EQUAL(0u, tx_timer);   /* reset at boundary */
}

/** @verifies SWR-CVC-021 */
void test_Heartbeat_Alive_counter_increments(void)
{
    /* First TX at cycle 5 — alive_counter increments after each TX */
    run_cycles(5u);
    uint8 first_alive = alive_counter;

    /* Second TX at cycle 10 */
    run_cycles(5u);
    uint8 second_alive = alive_counter;

    TEST_ASSERT_EQUAL(1u, first_alive);
    TEST_ASSERT_EQUAL(first_alive + 1u, second_alive);
}

/** @verifies SWR-CVC-021
 *  Equivalence class: VALID — alive counter boundary: wraps from 15 to 0
 *  Boundary: alive_counter = CVC_HB_ALIVE_MAX (15), next TX wraps to 0 */
void test_Heartbeat_Alive_counter_wraps_at_15(void)
{
    uint8 cycle;

    /* 15 TX periods: alive counter reaches exactly CVC_HB_ALIVE_MAX */
    for (cycle = 0u; cycle < 15u; cycle++) {
        run_cycles(5u);
    }
    TEST_ASSERT_EQUAL(CVC_HB_ALIVE_MAX, alive_counter);

    /* 16th TX: 16 > CVC_HB_ALIVE_MAX -> wrap to 0 */
    run_cycles(5u);
    TEST_ASSERT_EQUAL(0u, alive_counter);
}

/* ====================================================================
 * SWR-CVC-021: OperatingMode RTE write (auto-pulled by Com TX)
 * ==================================================================== */

/** @verifies SWR-CVC-021
 *  At the TX boundary the current vehicle state is mirrored into the
 *  CVC_Heartbeat.OperatingMode RTE slot for Com auto-pull. */
void test_Heartbeat_TX_writes_operating_mode(void)
{
    uint32 op_mode = 0xFFFFFFFFu;

    mock_rte_vehicle_state = 2u; /* CVC_STATE_DEGRADED */
    run_cycles(5u);

    TEST_ASSERT_EQUAL(1u,
        count_rte_writes(CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE, &op_mode));
    TEST_ASSERT_EQUAL_UINT32(2u, op_mode);
}

/** @verifies SWR-CVC-021 — OperatingMode tracks vehicle state changes */
void test_Heartbeat_TX_operating_mode_tracks_state(void)
{
    uint32 op_mode = 0xFFFFFFFFu;

    mock_rte_vehicle_state = 1u; /* RUN */
    run_cycles(5u);

    mock_rte_vehicle_state = 4u; /* SAFE_STOP */
    run_cycles(5u);

    /* Last recorded OperatingMode write carries the new state */
    TEST_ASSERT_EQUAL(2u,
        count_rte_writes(CVC_SIG_CVC_HEARTBEAT_OPERATING_MODE, &op_mode));
    TEST_ASSERT_EQUAL_UINT32(4u, op_mode);
}

/* ====================================================================
 * SWR-CVC-022: RX indication flags
 * ==================================================================== */

/** @verifies SWR-CVC-022 — FZC RX indication latches the FZC flag */
void test_Heartbeat_FZC_RxIndication_sets_flag(void)
{
    TEST_ASSERT_EQUAL(FALSE, fzc_rx_flag);

    Swc_Heartbeat_RxIndication(CVC_ECU_ID_FZC);

    TEST_ASSERT_EQUAL(TRUE, fzc_rx_flag);
    TEST_ASSERT_EQUAL(FALSE, rzc_rx_flag);
}

/** @verifies SWR-CVC-022 — RZC RX indication latches the RZC flag */
void test_Heartbeat_RZC_RxIndication_sets_flag(void)
{
    TEST_ASSERT_EQUAL(FALSE, rzc_rx_flag);

    Swc_Heartbeat_RxIndication(CVC_ECU_ID_RZC);

    TEST_ASSERT_EQUAL(TRUE, rzc_rx_flag);
    TEST_ASSERT_EQUAL(FALSE, fzc_rx_flag);
}

/** @verifies SWR-CVC-022
 *  Equivalence class: INVALID — unknown ECU ID in RxIndication
 *  Boundary: ecuId != CVC_ECU_ID_FZC and != CVC_ECU_ID_RZC */
void test_Heartbeat_RxIndication_unknown_ecu_ignored(void)
{
    Swc_Heartbeat_RxIndication(0xFFu);

    TEST_ASSERT_EQUAL(FALSE, fzc_rx_flag);
    TEST_ASSERT_EQUAL(FALSE, rzc_rx_flag);
}

/** @verifies SWR-CVC-022
 *  Equivalence class: INVALID — ECU ID boundary: 0 (not a valid ECU)
 *  Boundary: ecuId = 0 */
void test_Heartbeat_RxIndication_ecu_id_zero_ignored(void)
{
    Swc_Heartbeat_RxIndication(0u);

    TEST_ASSERT_EQUAL(FALSE, fzc_rx_flag);
    TEST_ASSERT_EQUAL(FALSE, rzc_rx_flag);
}

/* ====================================================================
 * SWR-CVC-022: Post-INIT grace comm status reset
 * ==================================================================== */

/** @verifies SWR-CVC-022
 *  ResetCommStatus clears the boot-transient TIMEOUT: statuses go OK,
 *  RTE comm signals are written OK, and the E2E SMs are forced VALID so
 *  the next 10ms CAN timeout check does not see INIT -> TIMEOUT. */
void test_Heartbeat_ResetCommStatus_forces_ok(void)
{
    uint32 fzc_val = 0xFFFFFFFFu;
    uint32 rzc_val = 0xFFFFFFFFu;

    TEST_ASSERT_EQUAL(CVC_COMM_TIMEOUT, fzc_comm_status);
    TEST_ASSERT_EQUAL(CVC_COMM_TIMEOUT, rzc_comm_status);

    Swc_Heartbeat_ResetCommStatus();

    TEST_ASSERT_EQUAL(CVC_COMM_OK, fzc_comm_status);
    TEST_ASSERT_EQUAL(CVC_COMM_OK, rzc_comm_status);
    TEST_ASSERT_EQUAL(E2E_SM_VALID, fzc_sm_state.Status);
    TEST_ASSERT_EQUAL(E2E_SM_VALID, rzc_sm_state.Status);

    TEST_ASSERT_EQUAL(1u, count_rte_writes(CVC_SIG_FZC_COMM_STATUS, &fzc_val));
    TEST_ASSERT_EQUAL(1u, count_rte_writes(CVC_SIG_RZC_COMM_STATUS, &rzc_val));
    TEST_ASSERT_EQUAL_UINT32(CVC_COMM_OK, fzc_val);
    TEST_ASSERT_EQUAL_UINT32(CVC_COMM_OK, rzc_val);
}

/* ====================================================================
 * SWR-CVC-021: Defensive checks
 * ==================================================================== */

/** @verifies SWR-CVC-021
 *  Equivalence class: INVALID — uninitialized module
 *  Boundary: initialized == FALSE */
void test_Heartbeat_MainFunction_before_init_no_action(void)
{
    /* Reset initialized flag (source included) */
    initialized = FALSE;

    mock_wdgm_checkpoint_count = 0u;
    mock_rte_write_count       = 0u;
    run_cycles(10u);

    /* Should do nothing — not initialized */
    TEST_ASSERT_EQUAL(0u, mock_wdgm_checkpoint_count);
    TEST_ASSERT_EQUAL(0u, mock_rte_write_count);

    /* Restore */
    Swc_Heartbeat_Init();
}

/* ====================================================================
 * PHASE 1: Derived Constants Verification
 * ==================================================================== */

/** @verifies SWR-CVC-021
 *  Verifies HB_TX_CYCLES is derived from CVC_HB_TX_PERIOD_MS / CVC_RTE_PERIOD_MS */
void test_Heartbeat_Derived_TX_cycles_value(void)
{
    /* HB_TX_CYCLES should equal 50ms / 10ms = 5 cycles */
    TEST_ASSERT_EQUAL(5u, HB_TX_CYCLES);
    TEST_ASSERT_EQUAL(CVC_HB_TX_PERIOD_MS / CVC_RTE_PERIOD_MS, HB_TX_CYCLES);
}

/* ====================================================================
 * PHASE 3: WdgM Integration
 * ==================================================================== */

/** @verifies SWR-CVC-021
 *  Phase 3: WdgM checkpoint fires at TX boundary (SE 3) */
void test_Heartbeat_WdgM_checkpoint_at_TX(void)
{
    /* Run one TX period */
    run_cycles(5u);

    TEST_ASSERT_EQUAL(1u, mock_wdgm_checkpoint_count);
    TEST_ASSERT_EQUAL(3u, mock_wdgm_last_se_id);
}

/** @verifies SWR-CVC-021
 *  Phase 3: WdgM gets 2 checkpoints in 100ms (2 TX periods) */
void test_Heartbeat_WdgM_two_checkpoints_in_100ms(void)
{
    /* Run two TX periods = 100ms */
    run_cycles(10u);

    TEST_ASSERT_EQUAL(2u, mock_wdgm_checkpoint_count);
}

/* ====================================================================
 * Test runner
 * ==================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-CVC-021: TX timing */
    RUN_TEST(test_Heartbeat_Init_comm_status_timeout);
    RUN_TEST(test_Heartbeat_TX_every_50ms);
    RUN_TEST(test_Heartbeat_No_TX_before_50ms);
    RUN_TEST(test_Heartbeat_TX_at_exactly_5_cycles);
    RUN_TEST(test_Heartbeat_Alive_counter_increments);
    RUN_TEST(test_Heartbeat_Alive_counter_wraps_at_15);

    /* SWR-CVC-021: OperatingMode RTE write (Com auto-pull) */
    RUN_TEST(test_Heartbeat_TX_writes_operating_mode);
    RUN_TEST(test_Heartbeat_TX_operating_mode_tracks_state);

    /* SWR-CVC-022: RX indication flags */
    RUN_TEST(test_Heartbeat_FZC_RxIndication_sets_flag);
    RUN_TEST(test_Heartbeat_RZC_RxIndication_sets_flag);
    RUN_TEST(test_Heartbeat_RxIndication_unknown_ecu_ignored);
    RUN_TEST(test_Heartbeat_RxIndication_ecu_id_zero_ignored);

    /* SWR-CVC-022: Comm status reset (post-INIT grace) */
    RUN_TEST(test_Heartbeat_ResetCommStatus_forces_ok);

    /* SWR-CVC-021: Defensive checks */
    RUN_TEST(test_Heartbeat_MainFunction_before_init_no_action);

    /* --- PHASE 1: Derived Constants --- */
    RUN_TEST(test_Heartbeat_Derived_TX_cycles_value);

    /* --- PHASE 3: WdgM --- */
    RUN_TEST(test_Heartbeat_WdgM_checkpoint_at_TX);
    RUN_TEST(test_Heartbeat_WdgM_two_checkpoints_in_100ms);

    return UNITY_END();
}
