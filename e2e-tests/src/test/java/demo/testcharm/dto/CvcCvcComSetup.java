package demo.testcharm.dto;

import lombok.Getter;
import lombok.Setter;

import java.util.List;

/**
 * CVC CAN-communication phase script. Each phase drives one segment of the
 * {@code Swc_CvcCom_TransmitSchedule} / {@code BridgeRxToRte} cycle in the
 * native CvcCom harness.
 */
@Getter
@Setter
public class CvcCvcComSetup {
    private List<CvcCvcComPhase> phases;
}
