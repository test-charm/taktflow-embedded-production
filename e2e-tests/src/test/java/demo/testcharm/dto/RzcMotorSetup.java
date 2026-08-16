package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC motor phase script. Each phase drives one segment of the
 * {@code Swc_Motor_MainFunction} cycle in the native motor harness.
 */
@Getter
@Setter
public class RzcMotorSetup {
    private List<RzcMotorPhase> phases;
}
