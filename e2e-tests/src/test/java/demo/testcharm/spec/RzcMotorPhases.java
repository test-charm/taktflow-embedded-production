package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcMotorPhases {

    public static class RzcMotorPhase extends Spec<demo.testcharm.dto.RzcMotorPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("estop").defaultValue(null);
            property("torqueCmd").defaultValue(null);
            property("derating").defaultValue(null);
            property("overcurrent").defaultValue(null);
            property("tempFault").defaultValue(null);
        }
    }
}
