package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC watchdog phase script. Each phase drives one segment of the
 * {@code sc_watchdog} native harness.
 */
@Getter
@Setter
public class ScWatchdogSetup {
    private List<ScWatchdogPhase> phases;
}
