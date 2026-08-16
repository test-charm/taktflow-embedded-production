package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcCvcComPhases {

    public static class CvcCvcComPhase extends Spec<demo.testcharm.dto.CvcCvcComPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("bridgeRx").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("estop").defaultValue(null);
            property("relayKill").defaultValue(null);
            property("motorCutoff").defaultValue(null);
            property("brakeFault").defaultValue(null);
            property("steerFault").defaultValue(null);
            property("pedalFault").defaultValue(null);
            property("fzcComm").defaultValue(null);
            property("rzcComm").defaultValue(null);
            property("torque").defaultValue(null);
            property("rxBrakeEvent").defaultValue(null);
            property("rxBrakeStatus").defaultValue(null);
            property("rxMotorCutoff").defaultValue(null);
            property("rxScRelay").defaultValue(null);
            property("rxBattery").defaultValue(null);
            property("rxSteerFault").defaultValue(null);
            property("rxMotorFault").defaultValue(null);
            property("rxFzcAlive").defaultValue(null);
            property("rxRzcAlive").defaultValue(null);
        }
    }
}
