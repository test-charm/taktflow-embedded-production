package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC self-test phase script. Each phase drives one segment of the
 * {@code sc_selftest} native harness.
 */
@Getter
@Setter
public class ScSelfTestSetup {
    private List<ScSelfTestPhase> phases;
}
