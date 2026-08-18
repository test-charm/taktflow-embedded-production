package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC NVM phase script. Each phase drives one segment of the
 * {@code Swc_RzcNvm} native harness.
 */
@Getter
@Setter
public class RzcNvmSetup {
    private List<RzcNvmPhase> phases;
}
