package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC safety phase script. Each phase drives one segment of the
 * {@code Swc_FzcSafety} cycle in the native safety harness.
 */
@Getter
@Setter
public class FzcSafetySetup {
    private List<FzcSafetyPhase> phases;
}
