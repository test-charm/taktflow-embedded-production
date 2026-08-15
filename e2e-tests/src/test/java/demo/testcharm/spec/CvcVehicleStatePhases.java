package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcVehicleStatePhases {

    public static class CvcVehicleStatePhase extends Spec<demo.testcharm.dto.CvcVehicleStatePhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("selfTestPass").defaultValue(null);
            property("estop").defaultValue(null);
            property("scRelayEnergized").defaultValue(null);
            property("fzcComm").defaultValue(null);
            property("rzcComm").defaultValue(null);
            property("pedalFault").defaultValue(null);
            property("motorCutoff").defaultValue(null);
            property("brakeFault").defaultValue(null);
            property("steeringFault").defaultValue(null);
            property("batteryStatus").defaultValue(null);
            property("motorFaultRzc").defaultValue(null);
            property("motorSpeed").defaultValue(null);
            property("torqueRequest").defaultValue(null);
            property("pedalPosition").defaultValue(null);
            property("pedalFaultDual").defaultValue(null);
            property("comBrakeFault").defaultValue(null);
            property("comMotorCutoff").defaultValue(null);
            property("motorPduTimedOut").defaultValue(null);
        }
    }
}
