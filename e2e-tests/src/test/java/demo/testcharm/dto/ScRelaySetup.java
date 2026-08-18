package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC relay phase script. Each phase drives one segment of the
 * {@code sc_relay} native harness.
 */
@Getter
@Setter
public class ScRelaySetup {
    private List<ScRelayPhase> phases;
}
