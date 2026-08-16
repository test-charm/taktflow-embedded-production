package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC motor harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcMotorPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Motor_Init (uninitialized guard)
    private Integer vehicleState;    // 0=INIT 1=RUN 2=DEGRADED 3=LIMP 4=SAFE_STOP 5=SHUTDOWN
    private Integer estop;           // RTE RZC_SIG_ESTOP_ACTIVE (immediate disable)
    private Integer torqueCmd;       // commanded torque % (sint16, negative = reverse)
    private Integer derating;        // RTE RZC_SIG_DERATING_PCT (clamped to 100)
    private Integer overcurrent;     // RTE RZC_SIG_OVERCURRENT external fault
    private Integer tempFault;       // RTE RZC_SIG_TEMP_FAULT external fault
}
