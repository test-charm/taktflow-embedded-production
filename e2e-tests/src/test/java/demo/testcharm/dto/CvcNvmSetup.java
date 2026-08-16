package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC NVM phase script. Each phase drives one segment of the
 * {@code Swc_Nvm} cycle in the native NVM harness.
 */
@Getter
@Setter
public class CvcNvmSetup {
    private List<CvcNvmPhase> phases;
}
