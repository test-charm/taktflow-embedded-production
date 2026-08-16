package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC heartbeat phase script. Each phase drives one segment of the
 * {@code Swc_Heartbeat} cycle in the native heartbeat harness.
 */
@Getter
@Setter
public class CvcHeartbeatSetup {
    private List<CvcHeartbeatPhase> phases;
}
