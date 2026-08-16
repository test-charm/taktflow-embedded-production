package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC battery phase script. Each phase drives one segment of the
 * {@code Swc_Battery_MainFunction} cycle in the native battery harness.
 */
@Getter
@Setter
public class RzcBatterySetup {
    private List<RzcBatteryPhase> phases;
}
