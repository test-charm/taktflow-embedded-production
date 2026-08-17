package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the RZC heartbeat harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request
 * JSON (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RzcHeartbeatPhase {
    private Integer cycles;          // Swc_Heartbeat_MainFunction calls
    private Boolean skipInit;        // skip Swc_Heartbeat_Init (uninitialized guard)
    private Integer vehicleState;    // RTE RZC_SIG_VEHICLE_STATE read at TX boundary
    private Integer faultMask;       // RTE RZC_SIG_FAULT_MASK read at TX boundary (bit3=CAN fault)
}
