package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * RZC scheduler phase script. Each phase drives one segment of the
 * {@code Swc_RzcScheduler} runnable-table init/tick in the native
 * scheduler harness.
 */
@Getter
@Setter
public class RzcSchedulerSetup {
    private List<RzcSchedulerPhase> phases;
}
