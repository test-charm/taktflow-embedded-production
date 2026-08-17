package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC safety harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcSafetyPhase {
    private Integer cycles;            // Swc_FzcSafety_MainFunction calls
    private Boolean skipInit;          // skip Swc_FzcSafety_Init (uninitialized guard)
    private Boolean reinit;            // call Swc_FzcSafety_Init again at phase start
    private Integer steerFault;        // RTE FZC_SIG_STEER_FAULT (0 = no fault)
    private Integer brakeFault;        // RTE FZC_SIG_BRAKE_FAULT (0 = no fault)
    private Integer lidarFault;        // RTE FZC_SIG_LIDAR_FAULT (0 = no fault)
    private Integer vehicleState;      // RTE FZC_SIG_VEHICLE_STATE (1=RUN 5=SHUTDOWN)
    private Integer selfTestResult;    // RTE FZC_SIG_SELF_TEST_RESULT (1=PASS 0=FAIL)
    private Boolean selfTestDone;      // Safety_SelfTestDone injection
    private Integer steerCmdQuality;   // Com_GetRxPduQuality(STEER_CMD) (2=TIMED_OUT)
    private Integer brakeCmdQuality;   // Com_GetRxPduQuality(BRAKE_CMD) (2=TIMED_OUT)
}
