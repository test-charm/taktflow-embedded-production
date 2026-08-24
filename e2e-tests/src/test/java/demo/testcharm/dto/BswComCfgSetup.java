package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * BSW Com config phase script. Each phase is a bus-probe op consumed by
 * /api/test/bsw/comcfg/cvc (true end-to-end over vcan0).
 */
@Getter
@Setter
public class BswComCfgSetup {
    private List<BswComCfgPhase> phases;
}
