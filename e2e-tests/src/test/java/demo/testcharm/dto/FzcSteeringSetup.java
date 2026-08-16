package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC steering phase script. Each phase drives one segment of the
 * {@code Swc_Steering_MainFunction} cycle in the native steering harness.
 */
@Getter
@Setter
public class FzcSteeringSetup {
    private List<FzcSteeringPhase> phases;
}
