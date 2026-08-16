package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC COM phase script. Each phase drives one Swc_RzcCom action
 * (init / e2eProtect / e2eCheck / receive / tx) in the native COM harness.
 */
@Getter
@Setter
public class RzcComSetup {
    private List<RzcComPhase> phases;
}
