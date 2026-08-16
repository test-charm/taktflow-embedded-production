package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC steering harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcSteeringPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Steering_Init (uninitialized guard)
    private Boolean initNull;        // Swc_Steering_Init(NULL) (NULL-config guard)
    private Integer cmdAngle;        // commanded angle in degrees (RTE FZC_SIG_STEER_CMD)
    private Boolean rteReadFail;     // Rte_Read E_NOT_OK (command timeout path)
    private Integer actualAngle;     // IoHwAb feedback in degrees (14-bit SPI raw)
    private Boolean actualTrack;     // feedback tracks previous RTE output (healthy)
    private Boolean spiFail;         // IoHwAb_ReadSteeringAngle E_NOT_OK
    private Boolean getAngle;        // call Swc_Steering_GetAngle at end of phase
    private Boolean getAngleNull;    // call Swc_Steering_GetAngle(NULL)
}
