package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSafetyPhases {

    public static class FzcSafetyPhase extends Spec<demo.testcharm.dto.FzcSafetyPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("reinit").defaultValue(null);
            property("steerFault").defaultValue(null);
            property("brakeFault").defaultValue(null);
            property("lidarFault").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("selfTestResult").defaultValue(null);
            property("selfTestDone").defaultValue(null);
            property("steerCmdQuality").defaultValue(null);
            property("brakeCmdQuality").defaultValue(null);
        }
    }
}
