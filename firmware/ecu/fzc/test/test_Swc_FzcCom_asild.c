/**
 * @file    test_Swc_FzcCom.c
 * @brief   Unit tests for Swc_FzcCom — CAN E2E protection, message RX/TX
 * @date    2026-02-24
 *
 * @verifies SSR-FZC-018, SSR-FZC-019, SSR-FZC-020, SWR-FZC-018, SWR-FZC-019, SWR-FZC-020, SWR-FZC-026, SWR-FZC-027
 *
 * Tests E2E protection (CRC-8 0x1D + alive counter + Data ID) on TX,
 * E2E verification on RX, CAN monitor notification on the RX cycle,
 * and signal-level CAN TX scheduling (heartbeat, steering/brake status,
 * fault, motor cutoff and lidar signals pushed to Com shadow buffers).
 *
 * April 2026 SIL-stability series: RX signal bridging moved to Com RX
 * auto-push (rxSignalConfig[].rteSignalId) and TX pacing moved to Com
 * cycle-time config — Swc_FzcCom_Receive only notifies the CAN monitor,
 * and Swc_FzcCom_TransmitSchedule pushes signal values via Com_SendSignal
 * (no PduR_Transmit; E2E protection applied in Com_MainFunction_Tx).
 *
 * Mocks: Com_ReceiveSignal, Com_SendSignal, Rte_Read, Rte_Write,
 *        Swc_FzcCanMonitor_NotifyRx
 */
#include "unity.h"

/* ==================================================================
 * Local type definitions (avoid BSW header mock conflicts)
 * ================================================================== */

typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int   uint32;
typedef signed short    sint16;
typedef signed int     sint32;
typedef uint8           Std_ReturnType;

#define E_OK        0u
#define E_NOT_OK    1u
#define TRUE        1u
#define FALSE       0u
#define NULL_PTR    ((void*)0)

/* Mirrors Com.h (blocked by COM_H guard below) */
typedef uint8           Com_SignalIdType;

/* ==================================================================
 * FZC Config Constants (from Fzc_Cfg.h — verified against generated header)
 * ================================================================== */

/* RTE signal IDs (generated aliases → DBC-derived signal IDs) */
#define FZC_SIG_BRAKE_CMD           31u  /* FZC_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD */
#define FZC_SIG_BRAKE_FAULT         44u  /* FZC_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS */
#define FZC_SIG_BRAKE_POS           46u  /* FZC_SIG_BRAKE_STATUS_BRAKE_POSITION */
#define FZC_SIG_ESTOP_ACTIVE        68u  /* FZC_SIG_ESTOP_BROADCAST_ACTIVE */
#define FZC_SIG_LIDAR_ZONE          94u  /* FZC_SIG_LIDAR_DISTANCE_OBSTACLE_ZONE */
#define FZC_SIG_LIDAR_DIST          95u  /* FZC_SIG_LIDAR_DISTANCE_RANGE_CM */
#define FZC_SIG_LIDAR_SIGNAL        97u  /* FZC_SIG_LIDAR_DISTANCE_SIGNAL_STRENGTH */
#define FZC_SIG_MOTOR_CUTOFF       115u  /* FZC_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE */
#define FZC_SIG_STEER_ANGLE        153u  /* FZC_SIG_STEERING_STATUS_ACTUAL_ANGLE */
#define FZC_SIG_STEER_FAULT        159u  /* FZC_SIG_STEERING_STATUS_STEER_FAULT_STATUS */
#define FZC_SIG_VEHICLE_STATE      187u  /* FZC_SIG_VEHICLE_STATE_MODE */
#define FZC_SIG_FAULT_MASK         204u
#define FZC_SIG_COUNT              205u

/* Com Signal IDs used by Swc_FzcCom.c (from Fzc_Cfg.h) */
#define FZC_COM_SIG_FZC_HEARTBEAT_ECU_ID                 3u
#define FZC_COM_SIG_FZC_HEARTBEAT_OPERATING_MODE         4u
#define FZC_COM_SIG_FZC_HEARTBEAT_FAULT_STATUS           5u
#define FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE         9u
#define FZC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS  11u
#define FZC_COM_SIG_BRAKE_STATUS_BRAKE_POSITION         17u
#define FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE              25u
#define FZC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE       31u
#define FZC_COM_SIG_LIDAR_DISTANCE_RANGE_CM             36u
#define FZC_COM_SIG_LIDAR_DISTANCE_SIGNAL_STRENGTH      37u
#define FZC_COM_SIG_LIDAR_DISTANCE_OBSTACLE_ZONE        38u
#define FZC_COM_SIG_COUNT                              188u

/* E2E Data IDs (from Fzc_Cfg.h) */
#define FZC_E2E_HEARTBEAT_DATA_ID    0x03u
#define FZC_E2E_BRAKE_CMD_DATA_ID    0x08u

/* Application constants (from Fzc_App.h) */
#define FZC_BRAKE_NO_FAULT          0u
#define FZC_LIDAR_ZONE_CLEAR        0u
#define FZC_LIDAR_ZONE_BRAKING      2u
#define FZC_ECU_ID                0x02u
#define FZC_STATE_RUN               1u

/* ==================================================================
 * Swc_FzcCom API declarations
 * ================================================================== */

extern void            Swc_FzcCom_Init(void);
extern Std_ReturnType  Swc_FzcCom_E2eProtect(uint8* data, uint8 length, uint8 dataId);
extern Std_ReturnType  Swc_FzcCom_E2eCheck(const uint8* data, uint8 length, uint8 dataId);
extern void            Swc_FzcCom_Receive(void);
extern void            Swc_FzcCom_TransmitSchedule(void);

/* ==================================================================
 * Mock: Rte_Read / Rte_Write
 * ================================================================== */

#define MOCK_RTE_MAX_SIGNALS  FZC_SIG_COUNT

static uint32  mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
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

Std_ReturnType Rte_Read(uint16 SignalId, uint32* DataPtr)
{
    if (DataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        *DataPtr = mock_rte_signals[SignalId];
        return E_OK;
    }
    return E_NOT_OK;
}

/* ==================================================================
 * Mock: Com_ReceiveSignal
 *
 * RX signal bridging moved to Com RX auto-push — Swc_FzcCom_Receive
 * must NOT call Com_ReceiveSignal any more.  The mock only counts
 * calls so the tests can assert the migration holds.
 * ================================================================== */

static uint16  mock_com_rx_call_count;

Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* DataPtr)
{
    mock_com_rx_call_count++;
    (void)SignalId;
    (void)DataPtr;
    return E_NOT_OK;
}

/* ==================================================================
 * Mock: Com_SendSignal
 *
 * Signal-level TX: stores the last value sent per Com signal ID.
 * Widths mirror the SWC call sites: ACTUAL_ANGLE (sint16) and
 * RANGE_CM (uint16) are 2-byte reads, all other TX signals are uint8.
 * ================================================================== */

#define MOCK_COM_MAX_SIGNALS  FZC_COM_SIG_COUNT

typedef struct {
    uint32  value;        /* Last sent value (zero-extended) */
    uint8   sent;
} MockComTxSignalType;

static MockComTxSignalType mock_com_tx_signals[MOCK_COM_MAX_SIGNALS];
static uint16  mock_com_tx_count;
static uint8   mock_com_tx_last_sig;

static uint8 MockCom_SignalIs16Bit(Com_SignalIdType SignalId)
{
    return (uint8)(((SignalId == FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE) ||
                    (SignalId == FZC_COM_SIG_LIDAR_DISTANCE_RANGE_CM)) ? TRUE : FALSE);
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* DataPtr)
{
    if (DataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    mock_com_tx_count++;
    mock_com_tx_last_sig = SignalId;
    if (SignalId < MOCK_COM_MAX_SIGNALS) {
        mock_com_tx_signals[SignalId].sent = TRUE;
        if (MockCom_SignalIs16Bit(SignalId) == TRUE) {
            mock_com_tx_signals[SignalId].value = (uint32)(*(const uint16*)DataPtr);
        } else {
            mock_com_tx_signals[SignalId].value = (uint32)(*(const uint8*)DataPtr);
        }
    }
    return E_OK;
}

/* ==================================================================
 * Mock: Swc_FzcCanMonitor_NotifyRx (called by Swc_FzcCom_Receive)
 * ================================================================== */

static uint16  mock_canmon_notify_count;

void Swc_FzcCanMonitor_NotifyRx(void)
{
    mock_canmon_notify_count++;
}

/* ==================================================================
 * Test Configuration
 * ================================================================== */

void setUp(void)
{
    uint16 i;

    mock_rte_write_count = 0u;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    mock_rte_signals[FZC_SIG_VEHICLE_STATE] = FZC_STATE_RUN;

    mock_com_rx_call_count = 0u;

    mock_com_tx_count    = 0u;
    mock_com_tx_last_sig = 0xFFu;
    for (i = 0u; i < MOCK_COM_MAX_SIGNALS; i++) {
        mock_com_tx_signals[i].value = 0u;
        mock_com_tx_signals[i].sent  = FALSE;
    }

    mock_canmon_notify_count = 0u;

    Swc_FzcCom_Init();
}

void tearDown(void) { }

/* ==================================================================
 * SWR-FZC-019: E2E Transmit (2 tests)
 * ================================================================== */

/** @verifies SWR-FZC-019 — E2E protect computes CRC and inserts alive counter */
void test_FzcCom_e2e_protect_crc_and_alive(void)
{
    uint8 data[8] = {0u, 0u, 0xAAu, 0x55u, 0u, 0u, 0u, 0u};
    Std_ReturnType ret;

    ret = Swc_FzcCom_E2eProtect(data, 8u, FZC_E2E_HEARTBEAT_DATA_ID);

    /* assert: success */
    TEST_ASSERT_EQUAL_UINT8(E_OK, ret);

    /* assert: CRC in byte[0] is non-zero (computed over data + Data ID) */
    TEST_ASSERT_TRUE(data[0] != 0u);

    /* assert: alive counter in byte[1] bits [3:0] starts at 0 (first call after init) */
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8)(data[1] & 0x0Fu));

    /* Call again: alive counter should increment to 1 */
    data[0] = 0u;
    data[1] = 0u;
    ret = Swc_FzcCom_E2eProtect(data, 8u, FZC_E2E_HEARTBEAT_DATA_ID);
    TEST_ASSERT_EQUAL_UINT8(E_OK, ret);
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8)(data[1] & 0x0Fu));
}

/** @verifies SWR-FZC-019 — E2E protect rejects NULL data */
void test_FzcCom_e2e_protect_null_rejected(void)
{
    Std_ReturnType ret = Swc_FzcCom_E2eProtect(NULL_PTR, 8u, FZC_E2E_HEARTBEAT_DATA_ID);
    TEST_ASSERT_EQUAL_UINT8(E_NOT_OK, ret);
}

/* ==================================================================
 * SWR-FZC-020: E2E Receive + RX safety (3 tests)
 * ================================================================== */

/** @verifies SWR-FZC-020 — E2E check accepts valid protected message */
void test_FzcCom_e2e_check_valid(void)
{
    uint8 data[8] = {0u, 0u, 0xBBu, 0u, 0u, 0u, 0u, 0u};

    /* Protect it first */
    (void)Swc_FzcCom_E2eProtect(data, 8u, FZC_E2E_BRAKE_CMD_DATA_ID);

    /* Now check: should pass */
    Std_ReturnType ret = Swc_FzcCom_E2eCheck(data, 8u, FZC_E2E_BRAKE_CMD_DATA_ID);
    TEST_ASSERT_EQUAL_UINT8(E_OK, ret);
}

/** @verifies SWR-FZC-020 — Receive performs no RTE writes (Com auto-push owns routing) */
void test_FzcCom_receive_no_rte_writes(void)
{
    /* Set RTE to sentinel value to detect if it gets overwritten */
    mock_rte_signals[FZC_SIG_BRAKE_CMD] = 0xDEADu;

    mock_rte_write_count = 0u;
    Swc_FzcCom_Receive();

    /* assert: no RTE writes from the RX cycle */
    TEST_ASSERT_EQUAL_UINT8(0u, mock_rte_write_count);
    /* assert: brake command sentinel preserved (no stale bridging) */
    TEST_ASSERT_EQUAL_UINT32(0xDEADu, mock_rte_signals[FZC_SIG_BRAKE_CMD]);
}

/** @verifies SWR-FZC-020 — RX cycle injects no fault-triggering safe defaults */
void test_FzcCom_receive_no_signals_no_estop(void)
{
    Swc_FzcCom_Receive();

    /* assert: E-stop stays at 0 (initial), NOT set to safe default 1 */
    TEST_ASSERT_EQUAL_UINT32(0u, mock_rte_signals[FZC_SIG_ESTOP_ACTIVE]);
    /* assert: Brake command stays at 0, NOT set to safe default 100% */
    TEST_ASSERT_EQUAL_UINT32(0u, mock_rte_signals[FZC_SIG_BRAKE_CMD]);
}

/* ==================================================================
 * SWR-FZC-026: CAN Message Reception (2 tests)
 * ================================================================== */

/** @verifies SWR-FZC-026 — Receive notifies CAN monitor each RX cycle
 *  (resets the silence counter — prevents false CAN-loss latch) */
void test_FzcCom_receive_notifies_can_monitor(void)
{
    Swc_FzcCom_Receive();
    Swc_FzcCom_Receive();
    Swc_FzcCom_Receive();

    TEST_ASSERT_EQUAL_UINT16(3u, mock_canmon_notify_count);
}

/** @verifies SWR-FZC-026 — RX bridging moved to Com auto-push:
 *  Receive makes no Com_ReceiveSignal calls */
void test_FzcCom_receive_no_com_signal_reads(void)
{
    Swc_FzcCom_Receive();

    TEST_ASSERT_EQUAL_UINT16(0u, mock_com_rx_call_count);
}

/* ==================================================================
 * SWR-FZC-027: CAN Message Transmission (4 tests)
 * ================================================================== */

/** @verifies SWR-FZC-027 — Heartbeat signals (ECU ID, mode, fault nibble)
 *  pushed to Com each cycle (50ms pacing enforced by Com config) */
void test_FzcCom_transmit_heartbeat_signals(void)
{
    mock_rte_signals[FZC_SIG_VEHICLE_STATE] = FZC_STATE_RUN;
    mock_rte_signals[FZC_SIG_FAULT_MASK]    = 0x05u;

    Swc_FzcCom_TransmitSchedule();

    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_FZC_HEARTBEAT_ECU_ID].sent);
    TEST_ASSERT_EQUAL_UINT32((uint32)FZC_ECU_ID,
        mock_com_tx_signals[FZC_COM_SIG_FZC_HEARTBEAT_ECU_ID].value);
    TEST_ASSERT_EQUAL_UINT32((uint32)FZC_STATE_RUN,
        mock_com_tx_signals[FZC_COM_SIG_FZC_HEARTBEAT_OPERATING_MODE].value);
    TEST_ASSERT_EQUAL_UINT32(0x05u,
        mock_com_tx_signals[FZC_COM_SIG_FZC_HEARTBEAT_FAULT_STATUS].value);
}

/** @verifies SWR-FZC-027 — Steering status sent via Com_SendSignal every cycle */
void test_FzcCom_transmit_steering_status_10ms(void)
{
    /* Set steering angle and fault in RTE */
    mock_rte_signals[FZC_SIG_STEER_ANGLE] = 45u;
    mock_rte_signals[FZC_SIG_STEER_FAULT] = 0u;

    mock_com_tx_count = 0u;

    /* Single cycle should send steering data via Com_SendSignal */
    Swc_FzcCom_TransmitSchedule();

    /* At least 2 Com_SendSignal calls: steer_angle + steer_fault */
    TEST_ASSERT_TRUE(mock_com_tx_count >= 2u);
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE].sent);
    TEST_ASSERT_EQUAL_UINT32(45u,
        mock_com_tx_signals[FZC_COM_SIG_STEERING_STATUS_ACTUAL_ANGLE].value);
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS].sent);
    TEST_ASSERT_EQUAL_UINT32(0u,
        mock_com_tx_signals[FZC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS].value);
}

/** @verifies SWR-FZC-027 — Brake position sent via Com_SendSignal every cycle */
void test_FzcCom_transmit_brake_position_10ms(void)
{
    /* Set brake position in RTE */
    mock_rte_signals[FZC_SIG_BRAKE_POS] = 75u;

    Swc_FzcCom_TransmitSchedule();

    /* assert: brake position signal was sent with correct value */
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_STATUS_BRAKE_POSITION].sent);
    TEST_ASSERT_EQUAL_UINT32(75u,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_STATUS_BRAKE_POSITION].value);
}

/** @verifies SWR-FZC-027 — Brake fault value pushed each cycle
 *  (TX pacing owned by Com cycle-time config, not the SWC) */
void test_FzcCom_transmit_brake_fault_active(void)
{
    /* Set brake fault active in RTE */
    mock_rte_signals[FZC_SIG_BRAKE_FAULT] = 1u;

    Swc_FzcCom_TransmitSchedule();

    /* assert: brake fault signal was sent with the fault type */
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE].sent);
    TEST_ASSERT_EQUAL_UINT32(1u,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE].value);
}

/* ==================================================================
 * SWR-FZC-027: Cyclic fault TX — always send, even when no fault (3 tests)
 * ================================================================== */

/** @verifies SWR-FZC-027
 *  Equivalence class: brake fault signal always pushed, even when
 *  NO_FAULT. Prevents stale CVC shadow buffer. */
void test_FzcCom_transmit_brake_fault_sent_when_no_fault(void)
{
    /* brake fault = NO_FAULT (0) */
    mock_rte_signals[FZC_SIG_BRAKE_FAULT] = FZC_BRAKE_NO_FAULT;

    Swc_FzcCom_TransmitSchedule();

    /* assert: brake fault signal was pushed (cyclic, not event-driven) */
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE].sent);
    /* assert: value = 0 (no fault) */
    TEST_ASSERT_EQUAL_UINT32(0u,
        mock_com_tx_signals[FZC_COM_SIG_BRAKE_FAULT_FAULT_TYPE].value);
}

/** @verifies SWR-FZC-027
 *  Equivalence class: motor cutoff signal always pushed, even when
 *  inactive. Prevents stale CVC shadow buffer. */
void test_FzcCom_transmit_motor_cutoff_sent_when_inactive(void)
{
    /* motor cutoff = 0 (inactive) */
    mock_rte_signals[FZC_SIG_MOTOR_CUTOFF] = 0u;

    Swc_FzcCom_TransmitSchedule();

    /* assert: motor cutoff signal was pushed (cyclic, not event-driven) */
    TEST_ASSERT_EQUAL_UINT8(TRUE,
        mock_com_tx_signals[FZC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE].sent);
    /* assert: value = 0 (inactive) */
    TEST_ASSERT_EQUAL_UINT32(0u,
        mock_com_tx_signals[FZC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE].value);
}

/** @verifies SWR-FZC-027
 *  Equivalence class: motor cutoff active (1) propagated to Com signal */
void test_FzcCom_transmit_motor_cutoff_active(void)
{
    mock_rte_signals[FZC_SIG_MOTOR_CUTOFF] = 1u;

    Swc_FzcCom_TransmitSchedule();

    TEST_ASSERT_EQUAL_UINT32(1u,
        mock_com_tx_signals[FZC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE].value);
}

/* ==================================================================
 * SWR-FZC-027: Lidar TX (1 test)
 * ================================================================== */

/** @verifies SWR-FZC-027 — Lidar zone/range/signal-strength pushed each cycle
 *  so all consumers (CVC) always have current data */
void test_FzcCom_transmit_lidar_signals(void)
{
    mock_rte_signals[FZC_SIG_LIDAR_ZONE]   = FZC_LIDAR_ZONE_BRAKING;
    mock_rte_signals[FZC_SIG_LIDAR_DIST]   = 0x0123u;
    mock_rte_signals[FZC_SIG_LIDAR_SIGNAL] = 0xABCDu;

    Swc_FzcCom_TransmitSchedule();

    TEST_ASSERT_EQUAL_UINT32((uint32)FZC_LIDAR_ZONE_BRAKING,
        mock_com_tx_signals[FZC_COM_SIG_LIDAR_DISTANCE_OBSTACLE_ZONE].value);
    TEST_ASSERT_EQUAL_UINT32(0x0123u,
        mock_com_tx_signals[FZC_COM_SIG_LIDAR_DISTANCE_RANGE_CM].value);
    /* Signal strength: SWC sends the high byte of the 16-bit raw value */
    TEST_ASSERT_EQUAL_UINT32(0xABu,
        mock_com_tx_signals[FZC_COM_SIG_LIDAR_DISTANCE_SIGNAL_STRENGTH].value);
}

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-FZC-019: E2E Transmit */
    RUN_TEST(test_FzcCom_e2e_protect_crc_and_alive);
    RUN_TEST(test_FzcCom_e2e_protect_null_rejected);

    /* SWR-FZC-020: E2E Receive + RX safety */
    RUN_TEST(test_FzcCom_e2e_check_valid);
    RUN_TEST(test_FzcCom_receive_no_rte_writes);
    RUN_TEST(test_FzcCom_receive_no_signals_no_estop);

    /* SWR-FZC-026: CAN Message Reception */
    RUN_TEST(test_FzcCom_receive_notifies_can_monitor);
    RUN_TEST(test_FzcCom_receive_no_com_signal_reads);

    /* SWR-FZC-027: CAN Message Transmission */
    RUN_TEST(test_FzcCom_transmit_heartbeat_signals);
    RUN_TEST(test_FzcCom_transmit_steering_status_10ms);
    RUN_TEST(test_FzcCom_transmit_brake_position_10ms);
    RUN_TEST(test_FzcCom_transmit_brake_fault_active);

    /* SWR-FZC-027: Cyclic fault TX — always send */
    RUN_TEST(test_FzcCom_transmit_brake_fault_sent_when_no_fault);
    RUN_TEST(test_FzcCom_transmit_motor_cutoff_sent_when_inactive);
    RUN_TEST(test_FzcCom_transmit_motor_cutoff_active);

    /* SWR-FZC-027: Lidar TX */
    RUN_TEST(test_FzcCom_transmit_lidar_signals);

    return UNITY_END();
}

/* ==================================================================
 * Source inclusion — link SWC under test directly into test binary
 * ================================================================== */

/* Prevent BSW headers from redefining types when source is included */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define COMSTACK_TYPES_H
#define SWC_FZC_COM_H
#define FZC_CFG_H
#define RTE_H
#define COM_H
#define PDUR_H
#define E2E_H
#define SWC_FZC_CAN_MONITOR_H

#include "../src/Swc_FzcCom.c"
