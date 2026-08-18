package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC state phase script. Each phase drives one segment of the
 * {@code sc_state} native harness.
 */
@Getter
@Setter
public class ScStateSetup {
    private List<ScStatePhase> phases;
}
