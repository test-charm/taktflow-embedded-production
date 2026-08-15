package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC vehicle state machine phase script. Each phase drives one segment of
 * the {@code Swc_VehicleState_MainFunction} cycle in the native harness.
 */
@Getter
@Setter
public class CvcVehicleStateSetup {
    private List<CvcVehicleStatePhase> phases;
}
