package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the SC heartbeat harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production
 * defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class ScHeartbeatPhase {
    private String op;              // init|monitor|notifyRx|validate
    private Boolean skipInit;       // skip SC_Heartbeat_Init on harness start
    private Integer ticks;          // monitor: SC_Heartbeat_Monitor call count
    private Integer ecu;            // notifyRx/validate: SC_ECU_* index
    private Integer repeats;        // notifyRx/validate: repeat count
    private Integer payload3;       // validate: heartbeat byte 3 (mode|faults)
    private Integer notifyA;        // monitor: ECU to NotifyRx once per tick (none=255)
    private Integer notifyB;        // monitor: second ECU to NotifyRx once per tick (none=255)
}
