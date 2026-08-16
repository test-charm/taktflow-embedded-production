package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the FZC CAN monitor harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class FzcCanMonitorPhase {
    private Integer cycles;          // Swc_FzcCanMonitor_Check calls
    private Boolean skipInit;        // skip Swc_FzcCanMonitor_Init (uninitialized guard)
    private Integer canMode;         // Can_GetControllerMode(0) return (2=STARTED, 1=STOPPED)
    private Integer tec;             // Can_GetErrorCounters transmit error counter
    private Integer rec;             // Can_GetErrorCounters receive error counter
    private Boolean notifyRx;        // call NotifyRx before each Check (messages arriving)
}
