package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC CAN monitor phase script. Each phase drives one segment of the
 * {@code Swc_FzcCanMonitor} cycle in the native CAN monitor harness.
 */
@Getter
@Setter
public class FzcCanMonitorSetup {
    private List<FzcCanMonitorPhase> phases;
}
