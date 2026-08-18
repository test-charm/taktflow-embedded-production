package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC watchdog harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScWatchdogPhase {
    private String op;              // init|feed
    private Integer ok;             // feed: allChecksOk (1=TRUE toggle, 0=FALSE starve)
    private Integer repeats;        // feed: SC_Watchdog_Feed call count
}
