package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC safety phase script. Each phase drives one segment of the
 * {@code Swc_RzcSafety_MainFunction} cycle in the native safety harness.
 */
@Getter
@Setter
public class RzcSafetySetup {
    private List<RzcSafetyPhase> phases;
}
