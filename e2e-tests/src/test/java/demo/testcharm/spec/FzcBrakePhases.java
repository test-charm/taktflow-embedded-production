package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcBrakePhases {

    public static class FzcBrakePhase extends Spec<demo.testcharm.dto.FzcBrakePhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("cmdBrake").defaultValue(null);
            property("rteReadFail").defaultValue(null);
            property("estop").defaultValue(null);
            property("actualPos").defaultValue(null);
            property("actualTrack").defaultValue(null);
            property("posReadFail").defaultValue(null);
            property("getPos").defaultValue(null);
            property("getPosNull").defaultValue(null);
        }
    }
}
