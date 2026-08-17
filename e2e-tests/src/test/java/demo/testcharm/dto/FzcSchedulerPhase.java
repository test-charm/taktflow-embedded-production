package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC scheduler harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults (valid init with the SWR-FZC-029 static table).
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcSchedulerPhase {
    private Boolean skipInit;       // skip Swc_FzcScheduler_Init (uninitialized guard)
    private Boolean reinit;         // call Swc_FzcScheduler_Init again at phase start
}
