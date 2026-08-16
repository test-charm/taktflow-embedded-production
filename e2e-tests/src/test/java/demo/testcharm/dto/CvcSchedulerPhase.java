package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC scheduler harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults (valid init with the production 8-runnable table).
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcSchedulerPhase {
    private Boolean skipInit;       // skip Swc_Scheduler_Init (uninitialized guard)
    private Boolean initNull;       // Swc_Scheduler_Init(NULL_PTR) (NULL-config guard)
    private Boolean nullRunnables;  // Init with config.runnables == NULL (guard)
    private Boolean zeroCount;      // Init with config.runnableCount == 0 (guard)
    private Integer tableIndex;     // valid-init table: 0=production 8-run, 1=min 1-run, 2=max 16-run
}
