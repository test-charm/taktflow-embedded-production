package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC CAN-communication harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcCvcComPhase {
    private Integer cycles;          // TransmitSchedule calls
    private Boolean skipInit;        // skip Swc_CvcCom_Init (uninitialized guard)
    private Boolean bridgeRx;        // call Swc_CvcCom_BridgeRxToRte after TX
    private Integer vehicleState;    // 0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN
    private Integer estop;           // TX fault input → faultMask 0x01
    private Integer relayKill;       // 0=killed → 0x02
    private Integer motorCutoff;     // → 0x04
    private Integer brakeFault;      // → 0x08
    private Integer steerFault;      // → 0x10
    private Integer pedalFault;      // → 0x20
    private Integer fzcComm;         // 1=TIMEOUT → 0x40
    private Integer rzcComm;         // 1=TIMEOUT → 0x80
    private Integer torque;          // CVC_SIG_TORQUE_REQUEST (clamped at 100)
    private Integer rxBrakeEvent;    // RX bridge: 0x210 event frame
    private Integer rxBrakeStatus;   // RX bridge: 0x201 periodic status
    private Integer rxMotorCutoff;   // RX bridge: Motor_Cutoff_Req
    private Integer rxScRelay;       // RX bridge: 1=energized, 0=killed
    private Integer rxBattery;       // RX bridge: Battery_Status level
    private Integer rxSteerFault;    // RX bridge: Steering_Status fault
    private Integer rxMotorFault;    // RX bridge: Motor_Status fault
    private Integer rxFzcAlive;      // RX bridge: FZC heartbeat E2E alive
    private Integer rxRzcAlive;      // RX bridge: RZC heartbeat E2E alive
}
