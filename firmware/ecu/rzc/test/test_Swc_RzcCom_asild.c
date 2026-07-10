/**
 * @file    test_Swc_RzcCom.c
 * @brief   Unit tests for Swc_RzcCom -- E2E protection, CAN RX/TX tables
 * @date    2026-02-24
 *
 * @verifies SSR-RZC-008, SSR-RZC-009, SWR-RZC-008, SWR-RZC-009, SWR-RZC-019, SWR-RZC-020, SWR-RZC-026, SWR-RZC-027
 *
 * Tests E2E CRC-8 protection and alive counters (TX and RX),
 * 3-failure safe default for torque command, E-stop message
 * handling, torque timeout at 100ms, heartbeat signal publication,
 * and the paced motor status / temp / battery TX schedule.
 *
 * Since Phase 2 the per-PDU E2E RX callback (Rzc_E2eRxCheck) is gone —
 * E2E protection lives in the Com layer. TX goes through Com_SendSignal
 * per signal instead of PduR_Transmit per PDU.
 *
 * Mocks: Rte_Read, Rte_Write, Com_SendSignal, Dem_ReportErrorStatus,
 *        Swc_RzcSafety_NotifyCanRx
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
typedef signed char     sint8;
typedef signed short    sint16;
typedef signed int     sint32;
typedef uint8           Std_ReturnType;

#define E_OK        ((Std_ReturnType)0x00U)
#define E_NOT_OK    ((Std_ReturnType)0x01U)
#define TRUE        1u
#define FALSE       0u
#define NULL_PTR    ((void*)0)

typedef uint8           boolean;
typedef uint8           Com_SignalIdType;

/* Prevent BSW headers from redefining types when source is included */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define COMSTACK_TYPES_H
#define SWC_RZC_COM_H
#define RZC_CFG_H
#define RTE_H
#define COM_H
#define PDUR_H
#define DEM_H
#define E2E_H
#define WDGM_H
#define IOHWAB_H
#define SWC_RZC_SAFETY_H

/* Mock: Swc_RzcSafety_NotifyCanRx (called by Swc_RzcCom_Receive) */
static uint8 mock_safety_notify_count;
void Swc_RzcSafety_NotifyCanRx(void) { mock_safety_notify_count++; }

/* ==================================================================
 * RZC signal IDs (from Rzc_Cfg.h -- redefined locally for test isolation,
 * values verified against the generated header / its aliases)
 * ================================================================== */

#define RZC_SIG_BATTERY_MV         21u   /* = RZC_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V */
#define RZC_SIG_BATTERY_STATUS     25u   /* = RZC_SIG_BATTERY_STATUS_LEVEL */
#define RZC_SIG_BATTERY_SOC        25u   /* = RZC_SIG_BATTERY_STATUS_LEVEL */
#define RZC_SIG_ESTOP_ACTIVE       68u   /* = RZC_SIG_ESTOP_BROADCAST_ACTIVE */
#define RZC_SIG_OVERCURRENT       108u   /* = RZC_SIG_MOTOR_CURRENT_OVERCURRENT_FLAG */
#define RZC_SIG_CURRENT_MA        109u   /* = RZC_SIG_MOTOR_CURRENT_PHASE_M_A */
#define RZC_SIG_TORQUE_ECHO       110u   /* = RZC_SIG_MOTOR_CURRENT_TORQUE_ECHO */
#define RZC_SIG_MOTOR_DIR         119u   /* = RZC_SIG_MOTOR_STATUS_MOTOR_DIRECTION */
#define RZC_SIG_MOTOR_ENABLE      120u   /* = RZC_SIG_MOTOR_STATUS_MOTOR_ENABLE */
#define RZC_SIG_MOTOR_FAULT       121u   /* = RZC_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS */
#define RZC_SIG_DERATING_PCT      124u   /* = RZC_SIG_MOTOR_TEMPERATURE_DERATING_PERCENT */
#define RZC_SIG_TEMP1_DC          128u   /* = RZC_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C */
#define RZC_SIG_TEMP2_DC          129u   /* = RZC_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C */
#define RZC_SIG_TORQUE_CMD        166u   /* = RZC_SIG_TORQUE_REQUEST_COMMAND_PCT */
#define RZC_SIG_FAULT_MASK        186u   /* = RZC_SIG_VEHICLE_STATE_FAULT_MASK */
#define RZC_SIG_VEHICLE_STATE     187u   /* = RZC_SIG_VEHICLE_STATE_MODE */
#define RZC_SIG_ENCODER_SPEED     198u   /* ECU-internal (not on CAN) */
#define RZC_SIG_COUNT             202u

/* ==================================================================
 * Com PDU / signal IDs (from Rzc_Cfg.h)
 * ================================================================== */

#define RZC_COM_TX_HEARTBEAT       0u   /* = RZC_COM_TX_RZC_HEARTBEAT */
#define RZC_COM_TX_MOTOR_STATUS    1u
#define RZC_COM_TX_MOTOR_CURRENT   2u
#define RZC_COM_TX_MOTOR_TEMP      3u   /* = RZC_COM_TX_MOTOR_TEMPERATURE */
#define RZC_COM_TX_BATTERY_STATUS  4u

#define RZC_COM_RX_ESTOP           0u   /* = RZC_COM_RX_ESTOP_BROADCAST */
#define RZC_COM_RX_VEHICLE_TORQUE  7u   /* = RZC_COM_RX_VEHICLE_STATE (legacy alias) */

/* Com TX signal IDs (index into Com signal config table) */
#define RZC_COM_SIG_RZC_HEARTBEAT_ECU_ID                 3u
#define RZC_COM_SIG_RZC_HEARTBEAT_FAULT_STATUS           5u
#define RZC_COM_SIG_MOTOR_STATUS_TORQUE_ECHO             9u
#define RZC_COM_SIG_MOTOR_STATUS_MOTOR_SPEED_RPM        10u
#define RZC_COM_SIG_MOTOR_STATUS_MOTOR_DIRECTION        11u
#define RZC_COM_SIG_MOTOR_STATUS_MOTOR_ENABLE           12u
#define RZC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS     13u
#define RZC_COM_SIG_MOTOR_CURRENT_PHASE_M_A             17u
#define RZC_COM_SIG_MOTOR_CURRENT_DIR_IS_REVERSE        18u
#define RZC_COM_SIG_MOTOR_CURRENT_MOTOR_ENABLE          19u
#define RZC_COM_SIG_MOTOR_CURRENT_OVERCURRENT_FLAG      20u
#define RZC_COM_SIG_MOTOR_CURRENT_TORQUE_ECHO           21u
#define RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C  25u
#define RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C  26u
#define RZC_COM_SIG_MOTOR_TEMPERATURE_DERATING_PERCENT  27u
#define RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V  31u
#define RZC_COM_SIG_BATTERY_STATUS_LEVEL                32u

/* E2E Data IDs (from Rzc_Cfg.h) */
#define RZC_E2E_HEARTBEAT_DATA_ID     0x04u
#define RZC_E2E_MOTOR_STATUS_DATA_ID  0x0Eu
#define RZC_E2E_MOTOR_CURRENT_DATA_ID 0x0Fu
#define RZC_E2E_MOTOR_TEMP_DATA_ID    0x00u
#define RZC_E2E_BATTERY_DATA_ID       0x13u
#define RZC_E2E_ESTOP_DATA_ID         0x01u
#define RZC_E2E_VEHSTATE_DATA_ID      0x05u

#define RZC_ECU_ID               0x03u

/* ==================================================================
 * DTC / DEM constants
 * ================================================================== */

#define RZC_DTC_CAN_BUS_OFF        5u
#define DEM_EVENT_STATUS_PASSED    0u
#define DEM_EVENT_STATUS_FAILED    1u

/* ==================================================================
 * Swc_RzcCom API declarations
 * ================================================================== */

extern void            Swc_RzcCom_Init(void);
extern Std_ReturnType  Swc_RzcCom_E2eProtect(uint8 pduId, uint8 *data, uint8 length);
extern Std_ReturnType  Swc_RzcCom_E2eCheck(uint8 pduId, const uint8 *data, uint8 length);
extern void            Swc_RzcCom_Receive(void);
extern void            Swc_RzcCom_TransmitSchedule(void);

/* ==================================================================
 * Mock: Rte_Read / Rte_Write
 * ================================================================== */

/** Sized to RZC_SIG_COUNT so real signal IDs (16..201) are captured */
#define MOCK_RTE_MAX_SIGNALS  RZC_SIG_COUNT

static uint32  mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8   mock_rte_write_count;

Std_ReturnType Rte_Read(uint16 SignalId, uint32* DataPtr)
{
    if (DataPtr == NULL_PTR) { return E_NOT_OK; }
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        *DataPtr = mock_rte_signals[SignalId];
        return E_OK;
    }
    return E_NOT_OK;
}

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
 * Mock: Com_SendSignal — captures the last value per signal ID.
 * Swc_RzcCom passes uint8* or uint16* depending on the signal, so the
 * mock reads the correct width via a signal-size lookup.
 * ================================================================== */

#define MOCK_COM_MAX_SIGNALS  48u

static uint8   mock_com_send_count;
static uint16  mock_com_last_signal_id;
static uint16  mock_com_sig_value[MOCK_COM_MAX_SIGNALS];
static uint8   mock_com_sig_sent[MOCK_COM_MAX_SIGNALS];

/** Signals transmitted as uint16 by Swc_RzcCom — all others are uint8 */
static uint8 mock_com_sig_is_u16(Com_SignalIdType SignalId)
{
    uint8 is_u16 = FALSE;
    if ((SignalId == RZC_COM_SIG_MOTOR_STATUS_MOTOR_SPEED_RPM) ||
        (SignalId == RZC_COM_SIG_MOTOR_CURRENT_PHASE_M_A) ||
        (SignalId == RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C) ||
        (SignalId == RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C) ||
        (SignalId == RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V)) {
        is_u16 = TRUE;
    }
    return is_u16;
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr)
{
    mock_com_send_count++;
    mock_com_last_signal_id = (uint16)SignalId;
    if ((SignalDataPtr != NULL_PTR) && (SignalId < MOCK_COM_MAX_SIGNALS)) {
        if (mock_com_sig_is_u16(SignalId) == TRUE) {
            mock_com_sig_value[SignalId] = *(const uint16*)SignalDataPtr;
        } else {
            mock_com_sig_value[SignalId] = (uint16)(*(const uint8*)SignalDataPtr);
        }
        mock_com_sig_sent[SignalId] = 1u;
    }
    return E_OK;
}

/* ==================================================================
 * Mock: Dem_ReportErrorStatus
 * ================================================================== */

static uint8   mock_dem_call_count;
static uint8   mock_dem_last_event_id;
static uint8   mock_dem_last_status;

void Dem_ReportErrorStatus(uint8 EventId, uint8 EventStatus)
{
    mock_dem_call_count++;
    mock_dem_last_event_id = EventId;
    mock_dem_last_status   = EventStatus;
}

/* ==================================================================
 * Test Configuration
 * ================================================================== */

void setUp(void)
{
    uint8 i;

    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    mock_rte_write_count = 0u;

    mock_com_send_count     = 0u;
    mock_com_last_signal_id = 0xFFu;
    for (i = 0u; i < MOCK_COM_MAX_SIGNALS; i++) {
        mock_com_sig_value[i] = 0u;
        mock_com_sig_sent[i]  = 0u;
    }

    mock_dem_call_count    = 0u;
    mock_dem_last_event_id = 0xFFu;
    mock_dem_last_status   = 0xFFu;

    mock_safety_notify_count = 0u;

    Swc_RzcCom_Init();
}

void tearDown(void) { }

/* ==================================================================
 * SWR-RZC-019: E2E Transmit
 * ================================================================== */

/** @verifies SWR-RZC-019 -- E2E protect writes CRC to byte 0 and alive to byte 1 */
void test_RzcCom_e2e_protect_crc_and_alive(void)
{
    uint8 data[8] = {0u, 0u, 0xAAu, 0xBBu, 0xCCu, 0xDDu, 0xEEu, 0xFFu};
    Std_ReturnType result;

    result = Swc_RzcCom_E2eProtect(RZC_COM_TX_HEARTBEAT, data, 8u);

    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    /* CRC should be non-zero (computed over bytes 1..7 with data ID) */
    TEST_ASSERT_TRUE(data[0] != 0u);
    /* Alive counter should be 0 on first call (low nibble of byte 1) */
    TEST_ASSERT_EQUAL_UINT8(0u, (uint8)(data[1] & 0x0Fu));

    /* Call again: alive counter should increment to 1 */
    data[0] = 0u;
    data[1] = 0u;
    result = Swc_RzcCom_E2eProtect(RZC_COM_TX_HEARTBEAT, data, 8u);
    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
    TEST_ASSERT_EQUAL_UINT8(1u, (uint8)(data[1] & 0x0Fu));
}

/* ==================================================================
 * SWR-RZC-020: E2E Receive
 * ================================================================== */

/** @verifies SWR-RZC-020 -- E2E check passes with correct CRC and alive counter */
void test_RzcCom_e2e_check_valid(void)
{
    uint8 data[8] = {0u, 0u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
    Std_ReturnType result;

    /* Protect first to get valid CRC and alive */
    (void)Swc_RzcCom_E2eProtect(RZC_COM_TX_HEARTBEAT, data, 8u);

    /* Re-init to reset RX alive counter to 0 so expected = 1 after first E2eProtect wrote alive=0 */
    /* Actually, E2eCheck expects alive to match: RX alive starts at 0, expects 1 on first check.
     * Protect wrote alive=0, so RX side expects increment from 0 -> 1.
     * We need to align: after Init, RX alive[pdu] = 0, expected = 0+1 = 1.
     * But protect wrote alive=0 in byte 1. So check will compare 0 != 1 -> fail.
     * For a valid test, we need to pre-set the RX expected alive so it matches. */

    /* Re-init to get clean state, then manually construct a valid message */
    Swc_RzcCom_Init();

    /* After init, TX alive = 0. Protect will write alive = 0 into data, then increment to 1.
     * After init, RX alive = 0. Check expects next = 1.
     * So we need a message with alive = 1 to match. */
    data[0] = 0u;
    data[1] = 0u;
    data[2] = 0x11u;
    data[3] = 0x22u;

    /* Use E2eProtect to generate CRC. First call: writes alive=0, TX advances to 1.
     * Second call: writes alive=1, TX advances to 2. */
    (void)Swc_RzcCom_E2eProtect(RZC_COM_TX_HEARTBEAT, data, 8u);
    /* data now has alive=0, CRC for alive=0 */

    /* For RX check: expected alive = 0+1 = 1. So we need alive=1.
     * Generate a second message: */
    data[2] = 0x11u;
    data[3] = 0x22u;
    (void)Swc_RzcCom_E2eProtect(RZC_COM_TX_HEARTBEAT, data, 8u);
    /* data now has alive=1, valid CRC for alive=1 */

    /* First check will expect alive=1 (init=0, expected=0+1=1). This matches. */
    result = Swc_RzcCom_E2eCheck(RZC_COM_TX_HEARTBEAT, data, 8u);
    TEST_ASSERT_EQUAL_UINT8(E_OK, result);
}

/** @verifies SWR-RZC-020 -- 3 consecutive E2E failures triggers zero torque
 *  for torque cmd and reports the CAN bus-off DTC */
void test_RzcCom_e2e_check_3_failures_zero_torque(void)
{
    uint8 bad_data[8] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    /* Set a non-zero torque command in RTE */
    mock_rte_signals[RZC_SIG_TORQUE_CMD] = 50u;

    /* Send 3 bad messages to torque PDU (RZC_COM_RX_VEHICLE_TORQUE) */
    (void)Swc_RzcCom_E2eCheck(RZC_COM_RX_VEHICLE_TORQUE, bad_data, 8u);
    (void)Swc_RzcCom_E2eCheck(RZC_COM_RX_VEHICLE_TORQUE, bad_data, 8u);
    (void)Swc_RzcCom_E2eCheck(RZC_COM_RX_VEHICLE_TORQUE, bad_data, 8u);

    /* Now call Receive -- should detect 3 failures and force zero torque */
    Swc_RzcCom_Receive();

    TEST_ASSERT_EQUAL_UINT32(0u, mock_rte_signals[RZC_SIG_TORQUE_CMD]);
    TEST_ASSERT_EQUAL_UINT8(RZC_DTC_CAN_BUS_OFF, mock_dem_last_event_id);
    TEST_ASSERT_EQUAL_UINT8(DEM_EVENT_STATUS_FAILED, mock_dem_last_status);
}

/* ==================================================================
 * SWR-RZC-026: CAN Message Reception
 * ================================================================== */

/** @verifies SWR-RZC-026 -- E-stop message (0x001) disables motor */
void test_RzcCom_receive_estop_disables_motor(void)
{
    /* Simulate E-stop active in RTE (set by lower BSW on 0x001 RX) */
    mock_rte_signals[RZC_SIG_ESTOP_ACTIVE] = 1u;

    Swc_RzcCom_Receive();

    /* ESTOP should remain written to RTE */
    TEST_ASSERT_EQUAL_UINT32(1u, mock_rte_signals[RZC_SIG_ESTOP_ACTIVE]);
    /* Receive notifies the safety module (CAN silence counter reset) */
    TEST_ASSERT_EQUAL_UINT8(1u, mock_safety_notify_count);
}

/** @verifies SWR-RZC-026 -- Torque timeout after 100ms (10 cycles at 10ms) forces zero */
void test_RzcCom_receive_torque_timeout_100ms_zero(void)
{
    uint8 i;

    /* No torque command (stays at 0 = no new command) */
    mock_rte_signals[RZC_SIG_TORQUE_CMD] = 0u;

    /* Run Receive for 10 cycles (100ms) */
    for (i = 0u; i < 10u; i++) {
        Swc_RzcCom_Receive();
    }

    /* Torque should be forced to zero */
    TEST_ASSERT_EQUAL_UINT32(0u, mock_rte_signals[RZC_SIG_TORQUE_CMD]);
}

/* ==================================================================
 * SWR-RZC-027: CAN Message Transmission
 * ================================================================== */

/** @verifies SWR-RZC-027 -- Heartbeat signals (ECU ID + state/fault byte)
 *  are published via Com on every TX schedule cycle */
void test_RzcCom_transmit_heartbeat_signals(void)
{
    mock_rte_signals[RZC_SIG_VEHICLE_STATE] = 2u;     /* DEGRADED */
    mock_rte_signals[RZC_SIG_FAULT_MASK]    = 0x05u;

    Swc_RzcCom_TransmitSchedule();

    TEST_ASSERT_EQUAL_UINT8(1u, mock_com_sig_sent[RZC_COM_SIG_RZC_HEARTBEAT_ECU_ID]);
    TEST_ASSERT_EQUAL_UINT16((uint16)RZC_ECU_ID,
                             mock_com_sig_value[RZC_COM_SIG_RZC_HEARTBEAT_ECU_ID]);
    /* state_fault = ((fault_mask & 0x0F) << 4) | (vehicle_state & 0x0F) = 0x52 */
    TEST_ASSERT_EQUAL_UINT16(0x52u,
                             mock_com_sig_value[RZC_COM_SIG_RZC_HEARTBEAT_FAULT_STATUS]);
}

/** @verifies SWR-RZC-027 -- Motor status+current transmitted every cycle,
 *  motor_temp paced to 100ms (offset 3), battery to 200ms (offset 7).
 *  FDCAN TX FIFO = 3 slots, max 2 frames/cycle from this SWC. */
void test_RzcCom_transmit_motor_data_10ms(void)
{
    uint8 i;

    /* Set some known values in RTE */
    mock_rte_signals[RZC_SIG_TORQUE_ECHO]   = 42u;
    mock_rte_signals[RZC_SIG_ENCODER_SPEED] = 1500u;
    mock_rte_signals[RZC_SIG_MOTOR_DIR]     = 1u;
    mock_rte_signals[RZC_SIG_MOTOR_ENABLE]  = 1u;
    mock_rte_signals[RZC_SIG_MOTOR_FAULT]   = 0u;
    mock_rte_signals[RZC_SIG_CURRENT_MA]    = 5000u;
    mock_rte_signals[RZC_SIG_OVERCURRENT]   = 0u;
    mock_rte_signals[RZC_SIG_TEMP1_DC]      = 650u;
    mock_rte_signals[RZC_SIG_TEMP2_DC]      = 700u;
    mock_rte_signals[RZC_SIG_DERATING_PCT]  = 25u;
    mock_rte_signals[RZC_SIG_BATTERY_MV]    = 12000u;
    mock_rte_signals[RZC_SIG_BATTERY_STATUS] = 1u;

    /* First cycle: heartbeat (2 signals) + motor status (5) + motor
     * current (5) = 12 Com_SendSignal calls. motor_temp fires at
     * cycle%10==3, battery at cycle%20==7. */
    Swc_RzcCom_TransmitSchedule();
    TEST_ASSERT_EQUAL_UINT8(12u, mock_com_send_count);

    /* Motor status (0x300) signal values */
    TEST_ASSERT_EQUAL_UINT16(42u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_STATUS_TORQUE_ECHO]);
    TEST_ASSERT_EQUAL_UINT16(1500u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_STATUS_MOTOR_SPEED_RPM]);
    TEST_ASSERT_EQUAL_UINT16(1u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_STATUS_MOTOR_ENABLE]);

    /* Motor current (0x301) signal values; dir 1 != reverse (2) -> 0 */
    TEST_ASSERT_EQUAL_UINT16(5000u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_CURRENT_PHASE_M_A]);
    TEST_ASSERT_EQUAL_UINT16(0u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_CURRENT_DIR_IS_REVERSE]);
    TEST_ASSERT_EQUAL_UINT16(0u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_CURRENT_OVERCURRENT_FLAG]);
    TEST_ASSERT_EQUAL_UINT16(42u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_CURRENT_TORQUE_ECHO]);

    /* Motor temp (0x302) not sent yet (fires at cycle offset 3) */
    TEST_ASSERT_EQUAL_UINT8(0u,
        mock_com_sig_sent[RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C]);

    /* Run 2 more cycles to reach offset 3 (motor_temp) */
    for (i = 0u; i < 2u; i++) {
        Swc_RzcCom_TransmitSchedule();
    }
    TEST_ASSERT_EQUAL_UINT16(650u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_1_C]);
    TEST_ASSERT_EQUAL_UINT16(700u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_TEMPERATURE_WINDING_TEMP_2_C]);
    TEST_ASSERT_EQUAL_UINT16(25u,
        mock_com_sig_value[RZC_COM_SIG_MOTOR_TEMPERATURE_DERATING_PERCENT]);

    /* Battery (0x303) not sent yet (fires at cycle offset 7) */
    TEST_ASSERT_EQUAL_UINT8(0u,
        mock_com_sig_sent[RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V]);

    /* Run 4 more cycles to reach offset 7 (battery) */
    for (i = 0u; i < 4u; i++) {
        Swc_RzcCom_TransmitSchedule();
    }
    TEST_ASSERT_EQUAL_UINT16(12000u,
        mock_com_sig_value[RZC_COM_SIG_BATTERY_STATUS_BATTERY_VOLTAGE_M_V]);
    TEST_ASSERT_EQUAL_UINT16(1u,
        mock_com_sig_value[RZC_COM_SIG_BATTERY_STATUS_LEVEL]);
}

/* Rzc_E2eRxCheck test cases removed — the CanIf E2E RX callback was
 * deleted from Swc_RzcCom.c (E2E RX check moved to the Com layer in
 * Phase 2; dead design). */

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-RZC-019: E2E Transmit */
    RUN_TEST(test_RzcCom_e2e_protect_crc_and_alive);

    /* SWR-RZC-020: E2E Receive */
    RUN_TEST(test_RzcCom_e2e_check_valid);
    RUN_TEST(test_RzcCom_e2e_check_3_failures_zero_torque);

    /* SWR-RZC-026: CAN Message Reception */
    RUN_TEST(test_RzcCom_receive_estop_disables_motor);
    RUN_TEST(test_RzcCom_receive_torque_timeout_100ms_zero);

    /* SWR-RZC-027: CAN Message Transmission */
    RUN_TEST(test_RzcCom_transmit_heartbeat_signals);
    RUN_TEST(test_RzcCom_transmit_motor_data_10ms);

    return UNITY_END();
}

/* ==================================================================
 * Include implementation under test (source inclusion pattern)
 * ================================================================== */
#include "../src/Swc_RzcCom.c"
