package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC temp-monitor phase script. Each phase drives one segment of the
 * {@code Swc_TempMonitor_MainFunction} cycle in the native temp-monitor harness.
 */
@Getter
@Setter
public class RzcTempMonitorSetup {
    private List<RzcTempMonitorPhase> phases;
}
