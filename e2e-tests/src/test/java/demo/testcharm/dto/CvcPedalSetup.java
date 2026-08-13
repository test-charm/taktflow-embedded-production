package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class CvcPedalSetup {
    private String vehicleState;
    private boolean resetSpiFault, resetDither, resetRecover;
    @JsonInclude(JsonInclude.Include.NON_NULL)
    private Integer cycles;
    @JsonInclude(JsonInclude.Include.NON_NULL)
    private Integer spiFaultSensor;
    @JsonInclude(JsonInclude.Include.NON_NULL)
    private Integer recoverCycles;
    @JsonInclude(JsonInclude.Include.NON_NULL)
    private Integer ditherAmplitude;
}
