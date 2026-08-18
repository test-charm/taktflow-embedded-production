package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * SC heartbeat phase script. Each phase drives one segment of the
 * {@code sc_heartbeat} native harness.
 */
@Getter
@Setter
public class ScHeartbeatSetup {
    private List<ScHeartbeatPhase> phases;
}
