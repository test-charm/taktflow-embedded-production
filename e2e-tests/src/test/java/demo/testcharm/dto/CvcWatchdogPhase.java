package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC watchdog harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcWatchdogPhase {
    private Boolean skipInit;        // skip Swc_Watchdog_Init (uninitialized guard)
    private Boolean initNull;        // call Swc_Watchdog_Init(NULL) (NULL-config guard)
    private Boolean loopComplete;    // Swc_Watchdog_Feed arg (main loop finished)
    private Boolean canaryOk;        // Swc_Watchdog_Feed arg (stack canary intact)
    private Boolean ramOk;           // Swc_Watchdog_Feed arg (RAM pattern passed)
    private Boolean canOk;           // Swc_Watchdog_Feed arg (CAN not bus-off)
    private Integer feedCount;       // Swc_Watchdog_Feed calls
}
