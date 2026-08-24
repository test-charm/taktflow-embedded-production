package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * BSW E2E-rejection phase script. Each phase is one corruption op consumed by
 * /api/test/bsw/e2ereject/cvc (true end-to-end over vcan0).
 */
@Getter
@Setter
public class BswE2ERejectSetup {
    private List<BswE2ERejectPhase> phases;
}