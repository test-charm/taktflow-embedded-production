package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC CAN monitor phase script. Each phase drives one segment of the
 * {@code Swc_CanMonitor} cycle in the native CAN monitor harness.
 */
@Getter
@Setter
public class CvcCanMonitorSetup {
    private List<CvcCanMonitorPhase> phases;
}
