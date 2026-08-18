package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC plausibility harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScPlausibilityPhase {
    private String op;              // init|check|creep|drainGrace|lookup|implausible
    private Boolean skipInit;       // skip SC_Plausibility_Init on harness start
    private Integer torque;         // check/creep/lookup: torque percentage (0-255)
    private Integer current;        // check/creep: motor current in mA
    private Integer vehValid;       // check/creep: vehicle-state mailbox valid
    private Integer curValid;       // check/creep: motor-current mailbox valid
    private Integer brakeFault;     // check: FZC brake fault (backup cutoff)
    private Integer repeats;        // check/creep: Check/CreepGuard call count
    private Integer ticks;          // drainGrace: Check call count
    private Integer expected;       // implausible: expected current in mA
    private Integer actual;         // implausible: measured current in mA
}
