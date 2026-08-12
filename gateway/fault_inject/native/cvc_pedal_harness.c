#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Std_Types.h"
#include "Cvc_Cfg.h"
#include "Swc_Pedal.h"
#include "Swc_CvcCom.h"
#include "Com.h"
#include "Rte.h"
#include "IoHwAb.h"
#include "Dem.h"

/* ---- BSW stubs for linking all CVC SWC files ---- */
#include "E2E_Sm.h"
#include "PduR.h"
#include "BswM.h"
#include "Dio.h"
#include "NvM.h"
#include "WdgM.h"

void E2E_Sm_Init(E2E_SmStateType* sm)                 { (void)sm; }
E2E_SmStatusType E2E_Sm_Check(const E2E_SmConfigType* cfg,
                               E2E_SmStateType* sm,
                               E2E_CheckStatusType status)
                                                       { (void)cfg; (void)sm; (void)status; return 0u; }
Std_ReturnType BswM_RequestMode(BswM_RequesterIdType id, BswM_ModeType mode)
                                                       { (void)id; (void)mode; return E_OK; }
uint8 Dio_FlipChannel(uint8 ch)                        { (void)ch; return 0u; }
Std_ReturnType WdgM_CheckpointReached(WdgM_SupervisedEntityIdType seid)
                                                       { (void)seid; return E_OK; }
uint16 Nvm_ComputeCalCrc(void)                         { return 0u; }
uint16 Nvm_ComputeDtcCrc(void)                         { return 0u; }
void NVIC_SystemReset(void)                            { /* no-op in harness */ }

/* Ssd1306 stubs for Swc_Dashboard */
#include "Ssd1306.h"
Std_ReturnType Ssd1306_Init(void)                       { return E_OK; }
void           Ssd1306_Clear(void)                      { }
void           Ssd1306_SetCursor(uint8 page, uint8 col) { (void)page; (void)col; }
Std_ReturnType Ssd1306_WriteString(const char* s)       { (void)s; return E_OK; }

/* SelfTest_Hw stubs */
#include "Swc_SelfTest.h"
boolean SelfTest_Hw_RamPattern(void)     { return TRUE; }
boolean SelfTest_Hw_NvmCheck(void)       { return TRUE; }
boolean SelfTest_Hw_CanLoopback(void)    { return TRUE; }
boolean SelfTest_Hw_SpiLoopback(void)    { return TRUE; }
boolean SelfTest_Hw_CanaryCheck(void)    { return TRUE; }
boolean SelfTest_Hw_MpuVerify(void)      { return TRUE; }
boolean SelfTest_Hw_OledAck(void)        { return TRUE; }

/* CvcDcm stubs */
#include "Swc_CvcDcm.h"
void*   CvcDcm_FindDid(uint16 did)            { (void)did; return NULL_PTR; }
boolean CvcDcm_IsServiceSupported(uint8 sid)  { (void)sid; return FALSE; }

/* ---- Additional BSW stubs for FZC / RZC / BCM / SC ECUs ---- */
#include "Pwm.h"
#include "Can.h"
#include "Uart.h"

Std_ReturnType Ssd1306_Hw_I2cWrite(uint8 addr, const uint8* data, uint8 len)
                                                             { (void)addr; (void)data; (void)len; return E_OK; }
Std_ReturnType IoHwAb_ReadBrakePosition(uint16* pos)         { if (pos) *pos = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadSteeringAngle(uint16* angle)       { if (angle) *angle = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadEncoderCount(uint32* count)        { if (count) *count = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadEncoderDirection(uint8* dir)       { if (dir) *dir = 0u; return E_OK; }
Std_ReturnType IoHwAb_SetMotorPWM(uint8 dir, uint16 duty)    { (void)dir; (void)duty; return E_OK; }
Std_ReturnType IoHwAb_ReadBatteryVoltage(uint16* mv)         { if (mv) *mv = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadMotorCurrent(uint16* ma)           { if (ma) *ma = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadMotorTemp(uint16* c)               { if (c) *c = 0u; return E_OK; }
Std_ReturnType IoHwAb_ReadMotorTemp2(uint16* c)              { if (c) *c = 0u; return E_OK; }

void           Pwm_SetDutyCycle(uint8 ch, uint16 duty)       { (void)ch; (void)duty; }
void           Dio_WriteChannel(uint8 ch, uint8 level)       { (void)ch; (void)level; }
Std_ReturnType Uart_ReadRxData(uint8* buf, uint8 len, uint8* read)
                                                             { if (read) *read = 0u; (void)buf; (void)len; return E_OK; }
Can_StateType  Can_GetControllerMode(uint8 ctrl)             { (void)ctrl; return 0u; }
Std_ReturnType Can_GetErrorCounters(uint8 ctrl, uint8* tec, uint8* rec)
                                                             { if (tec) *tec = 0u; if (rec) *rec = 0u; (void)ctrl; return E_OK; }
Std_ReturnType Can_GetControllerErrorState(uint8 ctrl, uint8* state)
                                                             { if (state) *state = 0u; (void)ctrl; return E_OK; }
Std_ReturnType NvM_ReadBlock(NvM_BlockIdType id, void* buf)  { (void)id; (void)buf; return E_NOT_OK; }
Std_ReturnType NvM_WriteBlock(NvM_BlockIdType id, const void* buf)
                                                             { (void)id; (void)buf; return E_NOT_OK; }
void           WdgM_MainFunction(void)                        { }

/* SC-internal cross-dependencies (stubs for excluded SC files) */
uint8  SC_Relay_IsKilled(void)              { return 0u; }
uint8  SC_Relay_GetKillReason(void)         { return 0u; }
uint8  SC_Plausibility_IsFaulted(void)      { return 0u; }
uint8  SC_Heartbeat_IsTimedOut(uint8 id)    { (void)id; return 0u; }
uint8  SC_Heartbeat_IsContentFault(uint8 id) { (void)id; return 0u; }
Std_ReturnType SC_CAN_TransmitStatus(uint8 ecu, uint8 status) { (void)ecu; (void)status; return E_OK; }

#define MOCK_RTE_MAX_SIGNALS 256u
#define MOCK_COM_MAX_SIGNALS 256u

static uint32_t mock_rte_signals[MOCK_RTE_MAX_SIGNALS];
static uint32_t mock_com_signals[MOCK_COM_MAX_SIGNALS];
static uint16_t mock_pedal_raw_0;
static uint16_t mock_pedal_raw_1;
static uint16_t mock_pedal_raw_0_base;
static uint16_t mock_pedal_raw_1_base;

static uint16_t percent_to_raw(uint32_t pct)
{
    if (pct > 100u) {
        pct = 100u;
    }
    return (uint16_t)((pct * 16383u) / 100u);
}

static const char* fault_name(uint32_t fault)
{
    switch (fault) {
        case CVC_PEDAL_NO_FAULT:
            return "NONE";
        case CVC_PEDAL_PLAUSIBILITY:
            return "PLAUSIBILITY";
        case CVC_PEDAL_STUCK:
            return "STUCK";
        case CVC_PEDAL_SENSOR1_FAIL:
            return "SENSOR1_FAIL";
        case CVC_PEDAL_SENSOR2_FAIL:
            return "SENSOR2_FAIL";
        default:
            return "UNKNOWN";
    }
}

Std_ReturnType IoHwAb_ReadPedalAngle(uint8 SensorId, uint16* Angle)
{
    if (Angle == NULL_PTR) {
        return E_NOT_OK;
    }
    if (SensorId == 0u) {
        *Angle = mock_pedal_raw_0;
        return E_OK;
    }
    if (SensorId == 1u) {
        *Angle = mock_pedal_raw_1;
        return E_OK;
    }
    return E_NOT_OK;
}

Std_ReturnType Rte_Write(Rte_SignalIdType SignalId, uint32 Data)
{
    if (SignalId < MOCK_RTE_MAX_SIGNALS) {
        mock_rte_signals[SignalId] = Data;
        return E_OK;
    }
    return E_NOT_OK;
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
        case CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT:
        case CVC_COM_SIG_CVC_HEARTBEAT_ECU_ID:
        case CVC_COM_SIG_CVC_HEARTBEAT_OPERATING_MODE:
        case CVC_COM_SIG_VEHICLE_STATE_MODE:
        case CVC_COM_SIG_ESTOP_BROADCAST_ACTIVE:
        case CVC_COM_SIG_ESTOP_BROADCAST_SOURCE:
        case CVC_COM_SIG_BODY_CONTROL_CMD_HEADLIGHT_CMD:
        case CVC_COM_SIG_BODY_CONTROL_CMD_TAIL_LIGHT_ON:
        case CVC_COM_SIG_BODY_CONTROL_CMD_HAZARD_ACTIVE:
        case CVC_COM_SIG_BODY_CONTROL_CMD_TURN_SIGNAL_CMD:
        case CVC_COM_SIG_BODY_CONTROL_CMD_DOOR_LOCK_CMD:
            mock_com_signals[SignalId] = *(const uint8*)SignalDataPtr;
            break;
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
    (void)SignalId;
    if (SignalDataPtr == NULL_PTR) {
        return E_NOT_OK;
    }
    *(uint8*)SignalDataPtr = 0u;
    return E_OK;
}

void Dem_ReportErrorStatus(Dem_EventIdType EventId, Dem_EventStatusType EventStatus)
{
    (void)EventId;
    (void)EventStatus;
}

Std_ReturnType Dem_GetEventStatus(Dem_EventIdType EventId, uint8* StatusPtr)
{
    if (StatusPtr != NULL_PTR) { *StatusPtr = 0u; }
    (void)EventId;
    return E_OK;
}

Std_ReturnType Dem_ClearAllDTCs(void) { return E_OK; }

Std_ReturnType IoHwAb_ReadEStop(uint8* State)
{
    if (State != NULL_PTR) { *State = 0u; }
    return E_OK;
}

Com_SignalQualityType Com_GetRxPduQuality(PduIdType RxPduId)
{
    (void)RxPduId;
    return 0u;
}

uint8 Swc_VehicleState_GetState(void)
{
    return (uint8)mock_rte_signals[CVC_SIG_VEHICLE_STATE];
}

static void reset_state(uint32_t vehicle_state)
{
    uint16_t i;
    for (i = 0u; i < MOCK_RTE_MAX_SIGNALS; i++) {
        mock_rte_signals[i] = 0u;
    }
    for (i = 0u; i < MOCK_COM_MAX_SIGNALS; i++) {
        mock_com_signals[i] = 0u;
    }
    mock_rte_signals[CVC_SIG_VEHICLE_STATE] = vehicle_state;
}

int main(int argc, char** argv)
{
    uint32_t sensor1_pct;
    uint32_t sensor2_pct;
    uint32_t vehicle_state;
    uint32_t cycles;
    uint32_t i;
    Swc_Pedal_ConfigType pedal_config;
    uint32_t pedal_fault;
    uint32_t pedal_position;
    uint32_t torque_request_pct;
    uint32_t torque_direction;

    if (argc != 5) {
        fprintf(stderr, "usage: %s <sensor1_pct> <sensor2_pct> <vehicle_state> <cycles>\n", argv[0]);
        return 2;
    }

    sensor1_pct = (uint32_t)strtoul(argv[1], NULL, 10);
    sensor2_pct = (uint32_t)strtoul(argv[2], NULL, 10);
    vehicle_state = (uint32_t)strtoul(argv[3], NULL, 10);
    cycles = (uint32_t)strtoul(argv[4], NULL, 10);

    mock_pedal_raw_0_base = percent_to_raw(sensor1_pct);
    mock_pedal_raw_1_base = percent_to_raw(sensor2_pct);
    mock_pedal_raw_0 = mock_pedal_raw_0_base;
    mock_pedal_raw_1 = mock_pedal_raw_1_base;
    reset_state(vehicle_state);

    pedal_config.plausThreshold = CVC_PEDAL_PLAUS_THRESHOLD;
    pedal_config.plausDebounce = CVC_PEDAL_PLAUS_DEBOUNCE;
    pedal_config.stuckThreshold = CVC_PEDAL_STUCK_THRESHOLD;
    pedal_config.stuckCycles = CVC_PEDAL_STUCK_CYCLES;
    pedal_config.latchClearCycles = CVC_PEDAL_LATCH_CLEAR_CYCLES;
    pedal_config.rampLimit = CVC_PEDAL_RAMP_LIMIT;

    Swc_Pedal_Init(&pedal_config);
    Swc_CvcCom_Init();

    for (i = 0u; i < cycles; i++) {
        /* Add a tiny deterministic dither to avoid the production stuck-sensor
         * detector firing on perfectly constant synthetic values. Real sensors
         * exhibit small jitter; this keeps the harness representative while
         * still using the unmodified ASW implementation. */
        {
            uint16_t dither = (uint16_t)((i & 0x01u) ? 16u : 0u);
            mock_pedal_raw_0 = (uint16_t)(mock_pedal_raw_0_base + dither);
            mock_pedal_raw_1 = (uint16_t)(mock_pedal_raw_1_base + dither);
        }
        Swc_Pedal_MainFunction();
        Swc_CvcCom_TransmitSchedule(i * 10u);
    }

    pedal_fault = mock_rte_signals[CVC_SIG_PEDAL_FAULT];
    pedal_position = mock_rte_signals[CVC_SIG_PEDAL_POSITION];
    torque_request_pct = mock_rte_signals[CVC_SIG_TORQUE_REQUEST];
    torque_direction = mock_rte_signals[CVC_SIG_TORQUE_REQUEST_DIRECTION];

    printf("{\"inputs\":{\"sensor1Pct\":%u,\"sensor2Pct\":%u,\"vehicleState\":%u,\"cycles\":%u},"
           "\"outputs\":{\"pedalPosition\":%u,\"pedalFaultCode\":%u,\"pedalFaultName\":\"%s\","
           "\"torqueRequestPct\":%u,\"torqueDirection\":%u,"
           "\"comSignals\":{\"torqueRequestCommandPct\":%u}}}\n",
           (unsigned)sensor1_pct,
           (unsigned)sensor2_pct,
           (unsigned)vehicle_state,
           (unsigned)cycles,
           (unsigned)pedal_position,
           (unsigned)pedal_fault,
           fault_name(pedal_fault),
           (unsigned)torque_request_pct,
           (unsigned)torque_direction,
           (unsigned)mock_com_signals[CVC_COM_SIG_TORQUE_REQUEST_COMMAND_PCT]);
    return 0;
}
