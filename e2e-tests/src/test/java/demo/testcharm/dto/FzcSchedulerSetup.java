package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * FZC scheduler phase script. Each phase drives one segment of the
 * {@code Swc_FzcScheduler} runnable-table initialization in the native
 * scheduler harness.
 */
@Getter
@Setter
public class FzcSchedulerSetup {
    private List<FzcSchedulerPhase> phases;
}
