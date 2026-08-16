package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC heartbeat harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcHeartbeatPhase {
    private Integer cycles;          // Swc_Heartbeat_MainFunction calls
    private Boolean skipInit;        // skip Swc_Heartbeat_Init (uninitialized guard)
    private Integer vehicleState;    // RTE CVC_SIG_VEHICLE_STATE read at TX boundary
    private Integer rxEcu;           // RxIndication arg (0=none, 2=FZC, 3=RZC, else unknown)
    private Boolean resetComm;       // call Swc_Heartbeat_ResetCommStatus after cycles
}
