package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC scheduler harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults (valid init with the SWR-RZC-028 static table, 0 ticks).
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcSchedulerPhase {
    private Boolean skipInit;       // skip Swc_RzcScheduler_Init (uninitialized guard)
    private Boolean reinit;         // call Swc_RzcScheduler_Init again at phase start
    private Integer ticks;          // Swc_RzcScheduler_Tick calls (dispatch simulation)
}
