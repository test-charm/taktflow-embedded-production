package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC plausibility phase script. Each phase drives one segment of the
 * {@code sc_plausibility} native harness.
 */
@Getter
@Setter
public class ScPlausibilitySetup {
    private List<ScPlausibilityPhase> phases;
}
