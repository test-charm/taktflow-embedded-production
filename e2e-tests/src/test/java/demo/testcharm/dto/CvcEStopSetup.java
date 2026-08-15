package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC E-stop phase script. Each phase drives one segment of the
 * {@code Swc_EStop_MainFunction} cycle in the native harness.
 */
@Getter
@Setter
public class CvcEStopSetup {
    private List<CvcEStopPhase> phases;
}
