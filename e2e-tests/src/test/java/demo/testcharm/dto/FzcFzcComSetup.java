package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC COM phase script. Each phase drives one Swc_FzcCom action
 * (init / e2eProtect / e2eCheck / receive / tx) in the native COM harness.
 */
@Getter
@Setter
public class FzcFzcComSetup {
    private List<FzcFzcComPhase> phases;
}
