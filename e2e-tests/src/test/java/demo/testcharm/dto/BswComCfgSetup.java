package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * BSW Com config readback phase script. Each phase drives one segment of the
 * {@code bsw_comcfg_<ecu>_harness} native harness (reads the generated
 * Com_Cfg_<Ecu>.c data tables back).
 */
@Getter
@Setter
public class BswComCfgSetup {
    private List<BswComCfgPhase> phases;
}
