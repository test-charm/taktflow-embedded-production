package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSafetyPhases {

    public static class RzcSafetyPhase extends Spec<demo.testcharm.dto.RzcSafetyPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("reinit").defaultValue(null);
            property("overcurrent").defaultValue(null);
            property("overtemp").defaultValue(null);
            property("directionFault").defaultValue(null);
            property("stallFault").defaultValue(null);
            property("batteryFault").defaultValue(null);
            property("selfTestResult").defaultValue(null);
            property("estopActive").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("canErrorState").defaultValue(null);
            property("notifyCanRx").defaultValue(null);
        }
    }
}
