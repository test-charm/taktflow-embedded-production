package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC COM harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcComPhase {
    private String op;              // init | e2eProtect | e2eCheck | receive | tx
    private Integer pduId;          // E2E PDU index
    private String data;            // E2E 8-byte payload as hex ("null" = NULL_PTR)
    private Integer length;         // E2E payload length (default 8)
    private Integer repeats;        // repeat count for e2eProtect / e2eCheck
    private Integer cycles;         // cyclic call count (receive / tx)
    private Boolean skipInit;       // skip Swc_RzcCom_Init (uninitialized guard)
    private Integer estop;          // RTE RZC_SIG_ESTOP_ACTIVE (receive input)
    private Integer vehicleState;   // RTE RZC_SIG_VEHICLE_STATE
    private Integer torqueCmd;      // RTE RZC_SIG_TORQUE_CMD
    private Integer faultMask;      // RTE RZC_SIG_FAULT_MASK (tx input)
    private Integer torqueEcho;     // RTE RZC_SIG_TORQUE_ECHO
    private Integer speedRpm;       // RTE RZC_SIG_ENCODER_SPEED
    private Integer motorDir;       // RTE RZC_SIG_MOTOR_DIR
    private Integer motorEnable;    // RTE RZC_SIG_MOTOR_ENABLE
    private Integer motorFault;     // RTE RZC_SIG_MOTOR_FAULT
    private Integer currentMa;      // RTE RZC_SIG_CURRENT_MA
    private Integer overcurrent;    // RTE RZC_SIG_OVERCURRENT
    private Integer temp1Dc;        // RTE RZC_SIG_TEMP1_DC
    private Integer temp2Dc;        // RTE RZC_SIG_TEMP2_DC
    private Integer deratingPct;    // RTE RZC_SIG_DERATING_PCT
    private Integer batteryMv;      // RTE RZC_SIG_BATTERY_MV
    private Integer batteryStatus;  // RTE RZC_SIG_BATTERY_STATUS
}
