package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC NVM phase script. Each phase drives one segment of the
 * {@code Swc_FzcNvm} native harness.
 */
@Getter
@Setter
public class FzcNvmSetup {
    private List<FzcNvmPhase> phases;
}
