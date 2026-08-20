package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * BSW RTE task-bodies phase script. Each phase drives one generated OSEK task
 * body of the {@code bsw_rtetaskbodies_cvc_harness} native harness.
 */
@Getter
@Setter
public class BswRteTaskBodiesSetup {
    private List<BswRteTaskBodiesPhase> phases;
}
