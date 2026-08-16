package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC watchdog phase script. Each phase drives one segment of the
 * {@code Swc_Watchdog} cycle in the native watchdog harness.
 */
@Getter
@Setter
public class CvcWatchdogSetup {
    private List<CvcWatchdogPhase> phases;
}
