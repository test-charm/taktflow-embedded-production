package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC self-test phase script. Each phase drives one segment of the
 * {@code Swc_RzcSelfTest} startup sequence in the native self-test harness.
 */
@Getter
@Setter
public class RzcSelfTestSetup {
    private List<RzcSelfTestPhase> phases;
}
