package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC E2E phase script. Each phase drives one segment of the
 * {@code sc_e2e} native harness.
 */
@Getter
@Setter
public class ScE2eSetup {
    private List<ScE2ePhase> phases;
}
