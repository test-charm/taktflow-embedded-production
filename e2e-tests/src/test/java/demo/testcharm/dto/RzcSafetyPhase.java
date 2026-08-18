package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC safety harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcSafetyPhase {
    private Integer cycles;             // MainFunction calls (default 1)
    private Boolean skipInit;           // skip Swc_RzcSafety_Init (uninitialized guard)
    private Boolean reinit;             // call Swc_RzcSafety_Init again at phase start
    private Integer overcurrent;        // RTE RZC_SIG_OVERCURRENT input
    private Integer overtemp;           // RTE RZC_SIG_TEMP_FAULT input
    private Integer directionFault;     // RTE RZC_SIG_ENCODER_DIR input
    private Integer stallFault;         // RTE RZC_SIG_ENCODER_STALL input
    private Integer batteryFault;       // RTE RZC_SIG_BATTERY_STATUS input
    private Integer selfTestResult;     // RTE RZC_SIG_SELF_TEST_RESULT input (1=PASS 0=FAIL)
    private Integer estopActive;        // RTE RZC_SIG_ESTOP_ACTIVE input
    private Integer vehicleState;       // 0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN
    private Integer canErrorState;      // Can_GetControllerErrorState(0) (0=ACTIVE 1=WARNING 2=BUSOFF)
    private Boolean notifyCanRx;        // call Swc_RzcSafety_NotifyCanRx before each MainFunction
}
