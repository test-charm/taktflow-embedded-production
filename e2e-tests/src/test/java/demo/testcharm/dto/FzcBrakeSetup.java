package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC brake phase script. Each phase drives one segment of the
 * {@code Swc_Brake_MainFunction} cycle in the native brake harness.
 */
@Getter
@Setter
public class FzcBrakeSetup {
    private List<FzcBrakePhase> phases;
}
