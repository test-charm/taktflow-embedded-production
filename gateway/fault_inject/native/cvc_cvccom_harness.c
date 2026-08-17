/**
 * @file    cvc_cvccom_harness.c
 * @brief   Native test harness for Swc_CvcCom (CVC CAN communication SWC)
 * @date    2026-08-15
 *
 * @details Links the REAL production Swc_CvcCom.c and drives
 *          Swc_CvcCom_TransmitSchedule() + Swc_CvcCom_BridgeRxToRte() through
 *          a phase script read from stdin. Each phase is one line of
 *          whitespace-separated key=value tokens. The harness pins the RTE
 *          fault signals (TX path: 0x100 fault-mask composition, heartbeat,
 *          E-stop broadcast, brake/steer commands, torque request bridge) and
 *          the Com RX shadow signals (RX path: brake/motor/SC/battery fault
 *          bridging + alive counters), then runs the SWC functions.
 *
 *          Phase keys:
 *            cycles       uint32   (default 1) TransmitSchedule calls
 *            skipInit     0|1      skip Swc_CvcCom_Init() (uninitialized guard)
 *            bridgeRx     0|1      call Swc_CvcCom_BridgeRxToRte() after TX
 *            vehicleState uint32   Swc_VehicleState_GetState() result
 *                                  (0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN)
 *            ---- TX fault inputs (RTE, read by TransmitSchedule) ----
 *            estop        0|1      CVC_SIG_ESTOP_ACTIVE        (→ faultMask 0x01)
 *            relayKill    0|1      CVC_SIG_SC_RELAY_KILL       (0=killed → 0x02)
 *            motorCutoff  0|1      CVC_SIG_MOTOR_CUTOFF        (→ 0x04)
 *            brakeFault   0|1      CVC_SIG_BRAKE_FAULT         (→ 0x08)
 *            steerFault   0|1      CVC_SIG_STEERING_FAULT      (→ 0x10)
 *            pedalFault   0|1      CVC_SIG_PEDAL_FAULT         (→ 0x20)
 *            fzcComm      0|1      CVC_SIG_FZC_COMM_STATUS     (1=TIMEOUT → 0x40)
 *            rzcComm      0|1      CVC_SIG_RZC_COMM_STATUS     (1=TIMEOUT → 0x80)
 *            torque       uint32   CVC_SIG_TORQUE_REQUEST      (clamped at 100)
 *            ---- RX bridge inputs (Com shadow, read by BridgeRxToRte) ----
 *            rxBrakeEvent uint32   CVC_COM_SIG_BRAKE_FAULT_FAULT_TYPE (0x210 event)
 *            rxBrakeStatus uint32  CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS (0x201)
 *            rxMotorCutoff uint32  CVC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE
 *            rxScRelay    uint32   CVC_COM_SIG_SC_STATUS_RELAY_ENERGIZED (1=OK,0=killed)
 *            rxBattery    uint32   CVC_COM_SIG_BATTERY_STATUS_LEVEL
 *            rxSteerFault uint32   CVC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS
 *            rxMotorFault uint32   CVC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS
 *            rxFzcAlive   uint32   CVC_COM_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER
 *            rxRzcAlive   uint32   CVC_COM_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER
 *
 *          Output is a single JSON object on stdout:
 *            {"heartbeatEcuId":1,"heartbeatMode":1,"vehicleStateMode":1,
 *             "faultMask":0,"torqueLimit":0,"steerAngleCmd":0,"brakeForceCmd":0,
 *             "estopBroadcastActive":0,"estopBroadcastSource":1,
 *             "torqueCommandPct":0,
 *             "bodyHeadlight":0,"bodyTaillight":0,"bodyHazard":0,
 *             "bodyTurnSignal":0,"bodyDoorLock":0,
 *             "rteBrakeFault":0,"rteMotorCutoff":0,"rteScRelayKill":1,
 *             "rteBattery":0,"rteSteerFault":0,"rteMotorFaultRzc":0,
 *             "rteFzcAlive":0,"rteRzcAlive":0,"rteFaultMask":0}
 *
 * @copyright Taktflow Systems 2026
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_CvcCom.h"
#include "Com.h"
#include "Rte.h"
#include "Swc_VehicleState.h"

#include "harness_common.h"

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_COM_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];

static uint32_t mock_vehicle_state = CVC_STATE_RUN;

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
    *DataPtr = mock_rte_signals[SignalId];
    return E_OK;
}

Std_ReturnType Com_SendSignal(Com_SignalIdType SignalId, const void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    switch (SignalId) {
        case CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK:
            mock_com_signals[SignalId] = *(const uint16*)SignalDataPtr;
            break;
        case CVC_COM_SIG_STEER_COMMAND_STEER_ANGLE_CMD:
            mock_com_signals[SignalId] = (uint32)*(const sint16*)SignalDataPtr;
            break;
        default:
            mock_com_signals[SignalId] = *(const uint8*)SignalDataPtr;
            break;
    }
    return E_OK;
}

Std_ReturnType Com_ReceiveSignal(Com_SignalIdType SignalId, void* SignalDataPtr)
{
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    *(uint8_t*)SignalDataPtr = (uint8_t)mock_com_signals[SignalId];
    return E_OK;
}

uint8 Swc_VehicleState_GetState(void)
{
    return (uint8)mock_vehicle_state;
}

/* ==================================================================
 * Phase parsing
 * ================================================================== */

typedef struct {
    uint32_t cycles;
    uint8_t  skip_init;
    uint8_t  bridge_rx;
    uint32_t vehicle_state;
    uint32_t estop;
    uint32_t relay_kill;
    uint32_t motor_cutoff;
    uint32_t brake_fault;
    uint32_t steer_fault;
    uint32_t pedal_fault;
    uint32_t fzc_comm;
    uint32_t rzc_comm;
    uint32_t torque;
    uint32_t rx_brake_event;
    uint32_t rx_brake_status;
    uint32_t rx_motor_cutoff;
    uint32_t rx_sc_relay;
    uint32_t rx_battery;
    uint32_t rx_steer_fault;
    uint32_t rx_motor_fault;
    uint32_t rx_fzc_alive;
    uint32_t rx_rzc_alive;
} Phase;

static void set_tx_inputs(const Phase* p)
{
    mock_rte_signals[CVC_SIG_ESTOP_ACTIVE]    = p->estop;
    mock_rte_signals[CVC_SIG_SC_RELAY_KILL]   = p->relay_kill;
    mock_rte_signals[CVC_SIG_MOTOR_CUTOFF]    = p->motor_cutoff;
    mock_rte_signals[CVC_SIG_BRAKE_FAULT]     = p->brake_fault;
    mock_rte_signals[CVC_SIG_STEERING_FAULT]  = p->steer_fault;
    mock_rte_signals[CVC_SIG_PEDAL_FAULT]     = p->pedal_fault;
    mock_rte_signals[CVC_SIG_FZC_COMM_STATUS] = p->fzc_comm;
    mock_rte_signals[CVC_SIG_RZC_COMM_STATUS] = p->rzc_comm;
    mock_rte_signals[CVC_SIG_TORQUE_REQUEST]  = p->torque;
}

static void set_rx_inputs(const Phase* p)
{
    mock_com_signals[CVC_COM_SIG_BRAKE_FAULT_FAULT_TYPE]         = p->rx_brake_event;
    mock_com_signals[CVC_COM_SIG_BRAKE_STATUS_BRAKE_FAULT_STATUS] = p->rx_brake_status;
    mock_com_signals[CVC_COM_SIG_MOTOR_CUTOFF_REQ_REQUEST_TYPE]  = p->rx_motor_cutoff;
    mock_com_signals[CVC_COM_SIG_SC_STATUS_RELAY_ENERGIZED]      = p->rx_sc_relay;
    mock_com_signals[CVC_COM_SIG_BATTERY_STATUS_LEVEL]           = p->rx_battery;
    mock_com_signals[CVC_COM_SIG_STEERING_STATUS_STEER_FAULT_STATUS] = p->rx_steer_fault;
    mock_com_signals[CVC_COM_SIG_MOTOR_STATUS_MOTOR_FAULT_STATUS] = p->rx_motor_fault;
    mock_com_signals[CVC_COM_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER] = p->rx_fzc_alive;
    mock_com_signals[CVC_COM_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER] = p->rx_rzc_alive;
}

static int run_phase(const Phase* p)
{
    uint32_t i;

    mock_vehicle_state = p->vehicle_state;
    set_tx_inputs(p);
    set_rx_inputs(p);

    for (i = 0u; i < p->cycles; i++) {
        Swc_CvcCom_TransmitSchedule(i * 10u);
    }

    if (p->bridge_rx != 0u) {
        Swc_CvcCom_BridgeRxToRte();
    }
    return 0;
}

/* ==================================================================
 * main
 * ================================================================== */


static void reset_phase(void* phase)
{
    Phase* p = (Phase*)phase;
    p->cycles = 1u;
    p->vehicle_state = CVC_STATE_RUN;
}

static int set_phase_field(void* phase, const char* key, const char* value)
{
    Phase* p = (Phase*)phase;
    uint32_t val = harness_parse_uint(value);
    if (strcmp(key, "cycles") == 0)         p->cycles = val;
    else if (strcmp(key, "skipInit") == 0)  p->skip_init = (uint8_t)val;
    else if (strcmp(key, "bridgeRx") == 0)  p->bridge_rx = (uint8_t)val;
    else if (strcmp(key, "vehicleState") == 0) p->vehicle_state = val;
    else if (strcmp(key, "estop") == 0)     p->estop = val;
    else if (strcmp(key, "relayKill") == 0) p->relay_kill = val;
    else if (strcmp(key, "motorCutoff") == 0) p->motor_cutoff = val;
    else if (strcmp(key, "brakeFault") == 0) p->brake_fault = val;
    else if (strcmp(key, "steerFault") == 0) p->steer_fault = val;
    else if (strcmp(key, "pedalFault") == 0) p->pedal_fault = val;
    else if (strcmp(key, "fzcComm") == 0)   p->fzc_comm = val;
    else if (strcmp(key, "rzcComm") == 0)   p->rzc_comm = val;
    else if (strcmp(key, "torque") == 0)    p->torque = val;
    else if (strcmp(key, "rxBrakeEvent") == 0) p->rx_brake_event = val;
    else if (strcmp(key, "rxBrakeStatus") == 0) p->rx_brake_status = val;
    else if (strcmp(key, "rxMotorCutoff") == 0) p->rx_motor_cutoff = val;
    else if (strcmp(key, "rxScRelay") == 0) p->rx_sc_relay = val;
    else if (strcmp(key, "rxBattery") == 0) p->rx_battery = val;
    else if (strcmp(key, "rxSteerFault") == 0) p->rx_steer_fault = val;
    else if (strcmp(key, "rxMotorFault") == 0) p->rx_motor_fault = val;
    else if (strcmp(key, "rxFzcAlive") == 0) p->rx_fzc_alive = val;
    else if (strcmp(key, "rxRzcAlive") == 0) p->rx_rzc_alive = val;
    return 0;
}

int main(void)
{
    Phase phases[64];
    size_t phase_count = 0u;
    size_t pi;
    uint8_t global_skip_init = 0u;

    /* Defaults */
    mock_vehicle_state = CVC_STATE_RUN;

    /* ---- parse all phases first (skipInit is decided on phase[0]) ---- */
        {
        int n = harness_read_phases(phases, sizeof(phases[0]), reset_phase,
                                    set_phase_field, NULL);
        if (n < 0) {
            return 2;
        }
        phase_count = (size_t)n;
    }


    /* Init once per harness run unless the first phase skips it
     * (exercises the uninitialized no-op guards in TransmitSchedule
     * and BridgeRxToRte). */
    global_skip_init = (phase_count == 0u) ? 0u : phases[0].skip_init;
    if (global_skip_init == 0u) {
        Swc_CvcCom_Init();
    }

    for (pi = 0u; pi < phase_count; pi++) {
        if (run_phase(&phases[pi]) != 0) {
            return 2;
        }
    }

    printf("{\"heartbeatEcuId\":%u,\"heartbeatMode\":%u,"
           "\"vehicleStateMode\":%u,\"faultMask\":%u,\"torqueLimit\":%u,"
           "\"steerAngleCmd\":%u,\"brakeForceCmd\":%u,"
           "\"estopBroadcastActive\":%u,\"estopBroadcastSource\":%u,"
           "\"torqueCommandPct\":%u,"
           "\"bodyHeadlight\":%u,\"bodyTaillight\":%u,\"bodyHazard\":%u,"
           "\"bodyTurnSignal\":%u,\"bodyDoorLock\":%u,"
           "\"rteBrakeFault\":%u,\"rteMotorCutoff\":%u,\"rteScRelayKill\":%u,"
           "\"rteBattery\":%u,\"rteSteerFault\":%u,\"rteMotorFaultRzc\":%u,"
           "\"rteFzcAlive\":%u,\"rteRzcAlive\":%u,\"rteFaultMask\":%u}\n",
           (unsigned)mock_com_signals[CVC_COM_SIG_CVC_HEARTBEAT_ECU_ID],
           (unsigned)mock_com_signals[CVC_COM_SIG_CVC_HEARTBEAT_OPERATING_MODE],
           (unsigned)mock_com_signals[CVC_COM_SIG_VEHICLE_STATE_MODE],
           (unsigned)mock_com_signals[CVC_COM_SIG_VEHICLE_STATE_FAULT_MASK],
           (unsigned)mock_com_signals[CVC_COM_SIG_VEHICLE_STATE_TORQUE_LIMIT],
           (unsigned)mock_com_signals[CVC_COM_SIG_STEER_COMMAND_STEER_ANGLE_CMD],
           (unsigned)mock_com_signals[CVC_COM_SIG_BRAKE_COMMAND_BRAKE_FORCE_CMD],
           (unsigned)mock_com_signals[CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE],
           (unsigned)mock_com_signals[CVC_COM_SIG_ESTOP_BROADCAST_SOURCE],
           (unsigned)mock_com_signals[CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT],
           (unsigned)mock_com_signals[CVC_COM_SIG_BODY_CONTROL_CMD_HEADLIGHT_CMD],
           (unsigned)mock_com_signals[CVC_COM_SIG_BODY_CONTROL_CMD_TAIL_LIGHT_ON],
           (unsigned)mock_com_signals[CVC_COM_SIG_BODY_CONTROL_CMD_HAZARD_ACTIVE],
           (unsigned)mock_com_signals[CVC_COM_SIG_BODY_CONTROL_CMD_TURN_SIGNAL_CMD],
           (unsigned)mock_com_signals[CVC_COM_SIG_BODY_CONTROL_CMD_DOOR_LOCK_CMD],
           (unsigned)mock_rte_signals[CVC_SIG_BRAKE_FAULT],
           (unsigned)mock_rte_signals[CVC_SIG_MOTOR_CUTOFF],
           (unsigned)mock_rte_signals[CVC_SIG_SC_RELAY_KILL],
           (unsigned)mock_rte_signals[CVC_SIG_BATTERY_STATUS],
           (unsigned)mock_rte_signals[CVC_SIG_STEERING_FAULT],
           (unsigned)mock_rte_signals[CVC_SIG_MOTOR_FAULT_RZC],
           (unsigned)mock_rte_signals[CVC_SIG_FZC_HEARTBEAT_E_2_E_ALIVE_COUNTER],
           (unsigned)mock_rte_signals[CVC_SIG_RZC_HEARTBEAT_E_2_E_ALIVE_COUNTER],
           (unsigned)mock_rte_signals[CVC_SIG_FAULT_MASK]);

    return 0;
}
