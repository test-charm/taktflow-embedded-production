package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC current-monitor phase script. Each phase drives one segment of the
 * {@code Swc_CurrentMonitor_MainFunction} cycle in the native current-monitor harness.
 */
@Getter
@Setter
public class RzcCurrentMonitorSetup {
    private List<RzcCurrentMonitorPhase> phases;
}
