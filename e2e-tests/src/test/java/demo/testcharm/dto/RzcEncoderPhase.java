package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC encoder harness script. Boxed fields are omitted when
 * null so the server-side harness can preserve rolling state between phases.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcEncoderPhase {
    private Integer cycles;          // MainFunction calls (default 1)
    private Boolean skipInit;        // skip Swc_Encoder_Init (uninitialized guard)
    private Long count;              // absolute encoder count before this phase
    private Integer deltaPerCycle;   // count increment before each cycle
    private Integer encoderDir;      // IoHwAb direction: 0=FWD 1=REV 2=STOP
    private Integer commandedDir;    // RTE motor direction: 0=FWD 1=REV 2=STOP
    private Integer torqueEcho;      // torque echo % used by stall detection
}
