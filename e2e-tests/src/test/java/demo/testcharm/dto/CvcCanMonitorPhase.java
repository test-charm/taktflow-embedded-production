package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC CAN monitor harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcCanMonitorPhase {
    private Integer cycles;          // Swc_CanMonitor_Check calls
    private Boolean skipInit;        // skip Swc_CanMonitor_Init (uninitialized guard)
    private Boolean isBusOff;        // Check isBusOff arg (bus-off detection)
    private Integer rxMsgCount;      // Check rxMsgCount arg (total RX count)
    private Boolean rxInc;           // increment rxMsgCount each Check call (messages arriving)
    private Boolean errorWarning;    // Check errorWarning arg
    private Integer timeStartMs;     // currentTimeMs for first Check call
    private Integer timeStepMs;      // currentTimeMs delta between Check calls
    private Boolean recovery;        // call Swc_CanMonitor_Recovery after cycles
    private Integer recoveryTimeMs;  // currentTimeMs for the Recovery call
}
