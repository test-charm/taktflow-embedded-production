package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC brake harness script. All fields are boxed
 * so unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcBrakePhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Brake_Init (uninitialized guard)
    private Boolean initNull;        // Swc_Brake_Init(NULL) (NULL-config guard)
    private Integer cmdBrake;        // commanded brake force 0-100+ (RTE FZC_SIG_BRAKE_CMD)
    private Boolean rteReadFail;     // Rte_Read E_NOT_OK (command timeout path)
    private Integer estop;           // RTE FZC_SIG_ESTOP_ACTIVE (immediate 100% brake)
    private Integer actualPos;       // IoHwAb ADC feedback 0-1000 (0-100% in 10ths)
    private Boolean actualTrack;     // feedback tracks the commanded brake (healthy)
    private Boolean posReadFail;     // IoHwAb_ReadBrakePosition E_NOT_OK
    private Boolean getPos;          // call Swc_Brake_GetPosition at end of phase
    private Boolean getPosNull;      // call Swc_Brake_GetPosition(NULL)
}
