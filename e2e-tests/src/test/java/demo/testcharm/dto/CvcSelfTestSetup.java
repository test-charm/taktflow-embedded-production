package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC self-test phase script. Each phase drives one segment of the
 * {@code Swc_SelfTest} startup sequence in the native self-test harness.
 */
@Getter
@Setter
public class CvcSelfTestSetup {
    private List<CvcSelfTestPhase> phases;
}
