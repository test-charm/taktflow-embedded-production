package demo.testcharm.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import lombok.Getter;
import lombok.Setter;

/**
 * One phase of the CVC vehicle-state harness script. All fields are boxed so
 * unspecified fields become {@code null} and are omitted from the request JSON
 * (NON_NULL); the server-side harness then applies its production defaults.
 */
@Getter
@Setter
@JsonInclude(JsonInclude.Include.NON_NULL)
public class CvcVehicleStatePhase {
    private Integer cycles;
    private Boolean selfTestPass;
    private Boolean estop;
    private Boolean scRelayEnergized;
    private Integer fzcComm;
    private Integer rzcComm;
    private Boolean pedalFault;
    private Boolean motorCutoff;
    private Boolean brakeFault;
    private Boolean steeringFault;
    private Integer batteryStatus;
    private Boolean motorFaultRzc;
    private Integer motorSpeed;
    private Integer torqueRequest;
    private Integer pedalPosition;
}
