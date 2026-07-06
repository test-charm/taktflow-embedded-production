/**
 * @file    test_Swc_CvcCom.c
 * @brief   Unit tests for Swc_CvcCom — TX signal scheduling and RX-to-RTE bridge
 * @date    2026-02-24
 *
 * @verifies SSR-CVC-014, SSR-CVC-015, SSR-CVC-016, SWR-CVC-014, SWR-CVC-015, SWR-CVC-016, SWR-CVC-017
 *
 * Tests: heartbeat signal TX (ECU ID + operating mode), Vehicle_State
 * signal packing (mode, fault mask, torque limit), state-gated safe-state
 * actuation (max brake / center steer in SAFE_STOP+), E-stop broadcast
 * bridging, Body_Control_Cmd refresh, torque forwarding, and the
 * Com-shadow-to-RTE fault bridge (brake-fault source merge 0x210/0x201,
 * heartbeat alive counters, SC relay, battery, steering, motor faults).
 *
 * NOTE (Phase 2 / April 2026 SIL-stability series): E2E protect/check and
 * CAN-ID RX routing moved from this SWC into Com_MainFunction_Tx/Rx.
 * Swc_CvcCom_E2eProtect/E2eCheck/Receive/GetRxStatus no longer exist —
 * this harness covers the surviving API: Init, TransmitSchedule,
 * BridgeRxToRte.
 *
 * Mocks: Com_SendSignal, Com_ReceiveSignal, Rte_Read, Rte_Write,
 *        Swc_VehicleState_GetState
 */
#include "unity.h"
#include <string.h>

/* ==================================================================
 * Local type definitions (avoid BSW header mock conflicts)
 * ================================================================== */

typedef unsigned char   uint8;
typedef unsigned short  uint16;
typedef unsigned int   uint32;
typedef signed char     sint8;
typedef signed short    sint16;
typedef signed int      sint32;
typedef unsigned char   boolean;
typedef uint8           Std_ReturnType;

#define E_OK        0u
#define E_NOT_OK    1u
#define TRUE        1u
#define FALSE       0u
#define NULL_PTR    ((void*)0)

/* Prevent BSW headers from redefining types when Swc_CvcCom.c is included */
#define PLATFORM_TYPES_H
#define STD_TYPES_H
#define SWC_CVCCOM_H
#define CVC_CFG_H
#define COM_H
#define RTE_H
#define SWC_VEHICLESTATE_H

/* RTE signal IDs (mirrors Cvc_App.h aliases) needed by bridge/TX code */
#define CVC_SIG_PEDAL_FAULT       19u
#define CVC_SIG_VEHICLE_STATE     20u
#define CVC_SIG_TORQUE_REQUEST    21u
#define CVC_SIG_ESTOP_ACTIVE      22u
#define CVC_SIG_FZC_COMM_STATUS   23u
#define CVC_SIG_RZC_COMM_STATUS   24u
#define CVC_SIG_FAULT_MASK        26u
#define CVC_SIG_MOTOR_CUTOFF      28u
#define CVC_SIG_STEERING_FAULT    29u
#define CVC_SIG_BRAKE_FAULT       30u
#define CVC_SIG_SC_RELAY_KILL     31u
#define CVC_SIG_BATTERY_STATUS    32u
#define CVC_SIG_MOTOR_FAULT_RZC   33u
/* Heartbeat alive counter RTE mirrors — generated values from Cvc_Cfg.h */
#define CVC_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER    73u
#define CVC_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER   130u
#define CVC_COMM_OK                0u
#define CVC_COMM_TIMEOUT           1u

/* Com Signal IDs used by Swc_CvcCom.c (must match Cvc_Cfg.h generated values) */
#define CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE                     3u
#define CVC_COM_SIG_ESTOP_BROADCAST_SOURCE                     4u
#define CVC_COM_SIG_CVC_HEARTBEAT_ECU_ID                       8u
#define CVC_COM_SIG_CVC_HEARTBEAT_OPERATING_MODE               9u
#define CVC_COM_SIG_VEHICLE_STATE_MODE                        14u
#define CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK                  15u
#define CVC_COM_SIG_VEHICLE_STATE_TORQUE_LIMIT                16u
#define CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT                21u
#define CVC_COM_SIG_STEER_COMMAND_STEER_ANGLE_CMD             29u
#define CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD             35u
#define CVC_COM_SIG_BODY_CONTROL_CMD_HEADLIGHT_CMD            38u
#define CVC_COM_SIG_BODY_CONTROL_CMD_TAIL_LIGHT_ON            39u
#define CVC_COM_SIG_BODY_CONTROL_CMD_HAZARD_ACTIVE            40u
#define CVC_COM_SIG_BODY_CONTROL_CMD_TURN_SIGNAL_CMD          41u
#define CVC_COM_SIG_BODY_CONTROL_CMD_DOOR_LOCK_CMD            42u
#define CVC_COM_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER         59u
#define CVC_COM_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER         65u
#define CVC_COM_SIG_SC_STATUS_RELAY_ENERGIZED                 76u
#define CVC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS        97u
#define CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS          106u
#define CVC_COM_SIG_BRAKE_FAULT_FAULT_TYPE                   111u
#define CVC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE            117u
#define CVC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS          133u
#define CVC_COM_SIG_BATTERY_STATUS_LEVEL                     152u

/* ECU constants (mirrors Cvc_App.h) */
#define CVC_ECU_ID_CVC                 0x01u

/* Vehicle state constants (mirrors Cvc_Cfg.h) */
#define CVC_STATE_INIT        0u
#define CVC_STATE_RUN         1u
#define CVC_STATE_SAFE_STOP   4u
#define CVC_STATE_SHUTDOWN    5u

/* ==================================================================
 * Mock: VehicleState
 * ================================================================== */

static uint8 mock_vehicle_state;
uint8 Swc_VehicleState_GetState(void) { return mock_vehicle_state; }

/* ==================================================================
 * Mock: Com_SendSignal — per-signal u8 store (+ s16 for steer angle)
 * ================================================================== */

#define MOCK_COM_TX_MAX_SIGNALS  64u

static uint8  mock_com_sent_u8[MOCK_COM_TX_MAX_SIGNALS];
static sint16 mock_com_sent_steer_s16;
static uint8  mock_com_send_count;

Std_ReturnType Com_SendSignal(uint16 SignalId, const void* SignalDataPtr)
{
    mock_com_send_count++;
    if (SignalDataPtr == NULL_PTR)
    {
        return E_NOT_OK;
    }
    if (SignalId == CVC_COM_SIG_STEER_COMMAND_STEER_ANGLE_CMD)
    {
        mock_com_sent_steer_s16 = *((const sint16*)SignalDataPtr);
    }
    if (SignalId < MOCK_COM_TX_MAX_SIGNALS)
    {
        mock_com_sent_u8[SignalId] = *((const uint8*)SignalDataPtr);
    }
    return E_OK;
}

/* ==================================================================
 * Mock: Com_ReceiveSignal — controllable per-signal shadow store
 * ================================================================== */

#define MOCK_COM_RX_MAX_SIGNALS  256u

static uint8 mock_com_rx_values[MOCK_COM_RX_MAX_SIGNALS];

Std_ReturnType Com_ReceiveSignal(uint16 SignalId, void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR)
    {
        return E_NOT_OK;
    }
    if (SignalId < MOCK_COM_RX_MAX_SIGNALS)
    {
        *((uint8*)SignalDataPtr) = mock_com_rx_values[SignalId];
    }
    return E_OK;
}

/* ==================================================================
 * Mock: Rte_Read, Rte_Write
 * ================================================================== */

#define MOCK_RTE_MAX_SIGNALS  256u   /* Covers generated IDs (alive ctr = 130) */

static uint32 mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint8  mock_rte_write_count;

Std_ReturnType Rte_Write(uint16 SignalId, uint32 Value)
{
    mock_rte_write_count++;
    if (SignalId < MOCK_RTE_MAX_SIGNALS) { mock_rte_signals[SignalId] = Value; }
    return E_OK;
}

Std_ReturnType Rte_Read(uint16 SignalId, uint32* Value)
{
    if ((SignalId < MOCK_RTE_MAX_SIGNALS) && (Value != NULL_PTR))
    {
        *Value = mock_rte_signals[SignalId];
    }
    return E_OK;
}

/* ==================================================================
 * Source inclusion — link SWC under test directly into test binary
 * (mid-file so tests can reach module statics, e.g. CvcCom_Initialized)
 * ================================================================== */

#include "../src/Swc_CvcCom.c"

/* ==================================================================
 * Test Configuration
 * ================================================================== */

void setUp(void)
{
    mock_vehicle_state = CVC_STATE_RUN;
    mock_com_send_count = 0u;
    mock_com_sent_steer_s16 = 0x7FFF;
    (void)memset(mock_com_sent_u8, 0, sizeof(mock_com_sent_u8));
    (void)memset(mock_com_rx_values, 0, sizeof(mock_com_rx_values));
    (void)memset(mock_rte_signals, 0, sizeof(mock_rte_signals));
    mock_rte_write_count = 0u;

    Swc_CvcCom_Init();
}

void tearDown(void) { }

/* ==================================================================
 * SWR-CVC-017: TX Schedule — heartbeat + Vehicle_State signals
 * ================================================================== */

/** @verifies SWR-CVC-017 — Heartbeat signals carry ECU ID and current state */
void test_TX_heartbeat_ecu_id_and_mode(void)
{
    mock_vehicle_state = CVC_STATE_RUN;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(CVC_ECU_ID_CVC,
        mock_com_sent_u8[CVC_COM_SIG_CVC_HEARTBEAT_ECU_ID]);
    TEST_ASSERT_EQUAL_UINT8(CVC_STATE_RUN,
        mock_com_sent_u8[CVC_COM_SIG_CVC_HEARTBEAT_OPERATING_MODE]);
}

/** @verifies SWR-CVC-017 — Vehicle_State mode + torque limit + torque forward */
void test_TX_vehicle_state_mode_and_torque(void)
{
    mock_vehicle_state = CVC_STATE_RUN;
    mock_rte_signals[CVC_SIG_TORQUE_REQUEST] = 50u;   /* 0..100 percent */

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(CVC_STATE_RUN,
        mock_com_sent_u8[CVC_COM_SIG_VEHICLE_STATE_MODE]);
    TEST_ASSERT_EQUAL_UINT8(50u,
        mock_com_sent_u8[CVC_COM_SIG_VEHICLE_STATE_TORQUE_LIMIT]);
    TEST_ASSERT_EQUAL_UINT8(50u,
        mock_com_sent_u8[CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT]);
}

/** @verifies SWR-CVC-017 — Torque above 100% is clamped before TX */
void test_TX_torque_clamped_to_100(void)
{
    mock_rte_signals[CVC_SIG_TORQUE_REQUEST] = 250u;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(100u,
        mock_com_sent_u8[CVC_COM_SIG_VEHICLE_STATE_TORQUE_LIMIT]);
    TEST_ASSERT_EQUAL_UINT8(100u,
        mock_com_sent_u8[CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT]);
}

/** @verifies SWR-CVC-017 — Fault mask composed from RTE fault signals:
 *  bit0=estop, bit5=pedal (SC relay energized -> bit1 clear) */
void test_TX_fault_mask_estop_and_pedal(void)
{
    mock_rte_signals[CVC_SIG_SC_RELAY_KILL] = 1u;   /* energized (OK)  */
    mock_rte_signals[CVC_SIG_ESTOP_ACTIVE]  = 1u;   /* -> 0x01         */
    mock_rte_signals[CVC_SIG_PEDAL_FAULT]   = 1u;   /* -> 0x20         */

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT32(0x21u, mock_rte_signals[CVC_SIG_FAULT_MASK]);
    TEST_ASSERT_EQUAL_UINT8(0x21u,
        mock_com_sent_u8[CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK]);
}

/** @verifies SWR-CVC-017 — Fault mask: relay killed (0) sets bit1,
 *  FZC comm timeout sets bit6 */
void test_TX_fault_mask_relay_kill_and_can_timeout(void)
{
    mock_rte_signals[CVC_SIG_SC_RELAY_KILL]   = 0u;              /* -> 0x02 */
    mock_rte_signals[CVC_SIG_FZC_COMM_STATUS] = CVC_COMM_TIMEOUT; /* -> 0x40 */

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT32(0x42u, mock_rte_signals[CVC_SIG_FAULT_MASK]);
    TEST_ASSERT_EQUAL_UINT8(0x42u,
        mock_com_sent_u8[CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK]);
}

/* ==================================================================
 * SWR-CVC-017: State-Gated TX Tests (Safe-State Actuation)
 * ================================================================== */

/** @verifies SWR-CVC-017 — TX sends brake=0 in RUN state */
void test_TX_brake_zero_in_run_state(void)
{
    mock_com_sent_u8[CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD] = 0xAAu;
    mock_vehicle_state = CVC_STATE_RUN;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(0u,
        mock_com_sent_u8[CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD]);
}

/** @verifies SWR-CVC-017 — TX sends brake=100 in SAFE_STOP state */
void test_TX_brake_max_in_safe_stop(void)
{
    mock_vehicle_state = CVC_STATE_SAFE_STOP;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(CVC_SAFE_BRAKE_CMD,
        mock_com_sent_u8[CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD]);
}

/** @verifies SWR-CVC-017 — TX sends brake=100 in SHUTDOWN state */
void test_TX_brake_max_in_shutdown(void)
{
    mock_vehicle_state = CVC_STATE_SHUTDOWN;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(CVC_SAFE_BRAKE_CMD,
        mock_com_sent_u8[CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD]);
}

/** @verifies SWR-CVC-017 — TX sends steer=center (0 deg) in SAFE_STOP state.
 *  Plain degrees encoding: 0 = center, no DBC scaling */
void test_TX_steer_center_in_safe_stop(void)
{
    mock_vehicle_state = CVC_STATE_SAFE_STOP;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_INT16(0, mock_com_sent_steer_s16);
}

/* ==================================================================
 * SWR-CVC-017: E-Stop broadcast + Body_Control_Cmd bridging
 * ================================================================== */

/** @verifies SWR-CVC-017 — E-stop RTE signal bridged to broadcast signals */
void test_TX_estop_bridge_active(void)
{
    mock_rte_signals[CVC_SIG_ESTOP_ACTIVE] = 1u;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(1u,
        mock_com_sent_u8[CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE]);
    TEST_ASSERT_EQUAL_UINT8(1u,   /* CVC = source 1 */
        mock_com_sent_u8[CVC_COM_SIG_ESTOP_BROADCAST_SOURCE]);
}

/** @verifies SWR-CVC-017 — E-stop inactive: broadcast signal refreshed to 0 */
void test_TX_estop_bridge_inactive(void)
{
    mock_com_sent_u8[CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE] = 0xAAu;
    mock_rte_signals[CVC_SIG_ESTOP_ACTIVE] = 0u;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(0u,
        mock_com_sent_u8[CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE]);
}

/** @verifies SWR-CVC-017 — Body_Control_Cmd signals refreshed every schedule
 *  call (0x350 PERIODIC 100ms — Com handles timing, SWC keeps signals fresh) */
void test_TX_body_control_signals_refreshed(void)
{
    mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_HEADLIGHT_CMD]   = 0xAAu;
    mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_TAIL_LIGHT_ON]   = 0xAAu;
    mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_HAZARD_ACTIVE]   = 0xAAu;
    mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_TURN_SIGNAL_CMD] = 0xAAu;
    mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_DOOR_LOCK_CMD]   = 0xAAu;

    Swc_CvcCom_TransmitSchedule(0u);

    /* Body control state is all-off until RTE wiring lands (TODO:POST-BETA) */
    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_HEADLIGHT_CMD]);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_TAIL_LIGHT_ON]);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_HAZARD_ACTIVE]);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_TURN_SIGNAL_CMD]);
    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_sent_u8[CVC_COM_SIG_BODY_CONTROL_CMD_DOOR_LOCK_CMD]);
}

/** @verifies SWR-CVC-017 — Transmit schedule handles large time values
 *  (timing moved to Com PERIODIC mode — argument must be ignored safely) */
void test_CvcCom_transmit_schedule_large_time(void)
{
    Swc_CvcCom_TransmitSchedule(0xFFFFFF00u);
    Swc_CvcCom_TransmitSchedule(0xFFFFFFFFu);

    TEST_ASSERT_TRUE(1u);
}

/** @verifies SWR-CVC-017 — No TX before Init (fail-silent) */
void test_TX_before_init_no_send(void)
{
    CvcCom_Initialized = FALSE;
    mock_com_send_count = 0u;

    Swc_CvcCom_TransmitSchedule(0u);

    TEST_ASSERT_EQUAL_UINT8(0u, mock_com_send_count);

    /* Restore */
    Swc_CvcCom_Init();
}

/* ==================================================================
 * SWR-CVC-016: RX-to-RTE Bridge Tests
 * ================================================================== */

/** @verifies SWR-CVC-016 — Brake fault: 0x210 event frame takes precedence */
void test_Bridge_brake_fault_prefers_event_frame(void)
{
    mock_com_rx_values[CVC_COM_SIG_BRAKE_FAULT_FAULT_TYPE]          = 3u;
    mock_com_rx_values[CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS] = 1u;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT32(3u, mock_rte_signals[CVC_SIG_BRAKE_FAULT]);
}

/** @verifies SWR-CVC-016 — Brake fault source merge: when the 0x210 event
 *  shadow is 0 (never TXed in SIL), the 0x201 Brake_Status.BrakeFaultStatus
 *  value must survive — not be overwritten to 0 every 10ms */
void test_Bridge_brake_fault_falls_back_to_status(void)
{
    mock_com_rx_values[CVC_COM_SIG_BRAKE_FAULT_FAULT_TYPE]          = 0u;
    mock_com_rx_values[CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS] = 2u;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT32(2u, mock_rte_signals[CVC_SIG_BRAKE_FAULT]);
}

/** @verifies SWR-CVC-016 — Fault shadows bridged to RTE for VehicleState */
void test_Bridge_fault_signals_to_rte(void)
{
    mock_com_rx_values[CVC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE]      = 1u;
    mock_com_rx_values[CVC_COM_SIG_SC_STATUS_RELAY_ENERGIZED]          = 1u;
    mock_com_rx_values[CVC_COM_SIG_BATTERY_STATUS_LEVEL]               = 3u;
    mock_com_rx_values[CVC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS] = 1u;
    mock_com_rx_values[CVC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS]    = 1u;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT32(1u, mock_rte_signals[CVC_SIG_MOTOR_CUTOFF]);
    TEST_ASSERT_EQUAL_UINT32(1u, mock_rte_signals[CVC_SIG_SC_RELAY_KILL]);
    TEST_ASSERT_EQUAL_UINT32(3u, mock_rte_signals[CVC_SIG_BATTERY_STATUS]);
    TEST_ASSERT_EQUAL_UINT32(1u, mock_rte_signals[CVC_SIG_STEERING_FAULT]);
    TEST_ASSERT_EQUAL_UINT32(1u, mock_rte_signals[CVC_SIG_MOTOR_FAULT_RZC]);
}

/** @verifies SWR-CVC-016 — Heartbeat alive counters bridged raw to RTE
 *  (comm STATUS is owned by Swc_Heartbeat — only the counters move here) */
void test_Bridge_heartbeat_alive_counters(void)
{
    mock_com_rx_values[CVC_COM_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER] = 7u;
    mock_com_rx_values[CVC_COM_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER] = 9u;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT32(7u,
        mock_rte_signals[CVC_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER]);
    TEST_ASSERT_EQUAL_UINT32(9u,
        mock_rte_signals[CVC_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER]);
}

/** @verifies SWR-CVC-016 — Bridge must not write comm status signals:
 *  heartbeat comm status is owned exclusively by Swc_Heartbeat */
void test_Bridge_does_not_touch_comm_status(void)
{
    mock_rte_signals[CVC_SIG_FZC_COMM_STATUS] = 0xEEu;
    mock_rte_signals[CVC_SIG_RZC_COMM_STATUS] = 0xEEu;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT32(0xEEu, mock_rte_signals[CVC_SIG_FZC_COMM_STATUS]);
    TEST_ASSERT_EQUAL_UINT32(0xEEu, mock_rte_signals[CVC_SIG_RZC_COMM_STATUS]);
}

/** @verifies SWR-CVC-016 — No bridge writes before Init (fail-silent) */
void test_Bridge_before_init_no_writes(void)
{
    CvcCom_Initialized = FALSE;
    mock_rte_write_count = 0u;

    Swc_CvcCom_BridgeRxToRte();

    TEST_ASSERT_EQUAL_UINT8(0u, mock_rte_write_count);

    /* Restore */
    Swc_CvcCom_Init();
}

/* ==================================================================
 * Test runner
 * ================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* SWR-CVC-017: TX schedule — heartbeat + Vehicle_State */
    RUN_TEST(test_TX_heartbeat_ecu_id_and_mode);
    RUN_TEST(test_TX_vehicle_state_mode_and_torque);
    RUN_TEST(test_TX_torque_clamped_to_100);
    RUN_TEST(test_TX_fault_mask_estop_and_pedal);
    RUN_TEST(test_TX_fault_mask_relay_kill_and_can_timeout);

    /* SWR-CVC-017: State-gated TX (safe-state actuation) */
    RUN_TEST(test_TX_brake_zero_in_run_state);
    RUN_TEST(test_TX_brake_max_in_safe_stop);
    RUN_TEST(test_TX_brake_max_in_shutdown);
    RUN_TEST(test_TX_steer_center_in_safe_stop);

    /* SWR-CVC-017: E-stop / body control bridging + robustness */
    RUN_TEST(test_TX_estop_bridge_active);
    RUN_TEST(test_TX_estop_bridge_inactive);
    RUN_TEST(test_TX_body_control_signals_refreshed);
    RUN_TEST(test_CvcCom_transmit_schedule_large_time);
    RUN_TEST(test_TX_before_init_no_send);

    /* SWR-CVC-016: RX-to-RTE bridge */
    RUN_TEST(test_Bridge_brake_fault_prefers_event_frame);
    RUN_TEST(test_Bridge_brake_fault_falls_back_to_status);
    RUN_TEST(test_Bridge_fault_signals_to_rte);
    RUN_TEST(test_Bridge_heartbeat_alive_counters);
    RUN_TEST(test_Bridge_does_not_touch_comm_status);
    RUN_TEST(test_Bridge_before_init_no_writes);

    return UNITY_END();
}
