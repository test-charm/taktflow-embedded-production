package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC scheduler phase script. Each phase drives one segment of the
 * {@code Swc_Scheduler} runnable-table initialization in the native
 * scheduler harness.
 */
@Getter
@Setter
public class CvcSchedulerSetup {
    private List<CvcSchedulerPhase> phases;
}
