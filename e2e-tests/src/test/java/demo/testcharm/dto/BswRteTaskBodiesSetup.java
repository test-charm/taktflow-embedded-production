package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * BSW RTE task-bodies phase script. Each phase is one bus-observation op
 * (cadence / ftti) consumed by /api/test/bsw/rtetaskbodies/cvc.
 */
@Getter
@Setter
public class BswRteTaskBodiesSetup {
    private List<BswRteTaskBodiesPhase> phases;
}
