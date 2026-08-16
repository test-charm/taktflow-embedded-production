package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSteeringPhases {

    public static class FzcSteeringPhase extends Spec<demo.testcharm.dto.FzcSteeringPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("cmdAngle").defaultValue(null);
            property("rteReadFail").defaultValue(null);
            property("actualAngle").defaultValue(null);
            property("actualTrack").defaultValue(null);
            property("spiFail").defaultValue(null);
            property("getAngle").defaultValue(null);
            property("getAngleNull").defaultValue(null);
        }
    }
}
