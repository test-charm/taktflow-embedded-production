package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC COM harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcFzcComPhase {
    private String op;              // init | e2eProtect | e2eCheck | receive | tx
    private String data;            // E2E 8-byte payload as hex ("null" = NULL_PTR)
    private Integer dataId;         // FZC E2E Data ID
    private Integer length;         // E2E payload length (default 8)
    private Integer repeats;        // repeat count for e2eProtect / e2eCheck
    private Integer cycles;         // cyclic call count (receive / tx)
    private Boolean skipInit;       // skip Swc_FzcCom_Init (uninitialized guard)
    private Integer vehicleState;   // RTE FZC_SIG_VEHICLE_STATE (tx input)
    private Integer faultMask;      // RTE FZC_SIG_FAULT_MASK (tx input)
    private Integer steerAngle;     // RTE FZC_SIG_STEER_ANGLE (tx input)
    private Integer steerFault;     // RTE FZC_SIG_STEER_FAULT (tx input)
    private Integer brakePos;       // RTE FZC_SIG_BRAKE_POS (tx input)
    private Integer brakeFault;     // RTE FZC_SIG_BRAKE_FAULT (tx input)
    private Integer motorCutoff;    // RTE FZC_SIG_MOTOR_CUTOFF (tx input)
    private Integer lidarZone;      // RTE FZC_SIG_LIDAR_ZONE (tx input)
    private Integer lidarDist;      // RTE FZC_SIG_LIDAR_DIST (tx input)
    private Integer lidarSignal;    // RTE FZC_SIG_LIDAR_SIGNAL (tx input)
}
