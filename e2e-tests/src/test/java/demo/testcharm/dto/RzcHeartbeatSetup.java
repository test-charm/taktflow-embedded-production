package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC heartbeat phase script. Each phase drives one segment of the
 * {@code Swc_Heartbeat} cycle in the native heartbeat harness.
 */
@Getter
@Setter
public class RzcHeartbeatSetup {
    private List<RzcHeartbeatPhase> phases;
}
