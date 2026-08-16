package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcLidarPhases {

    public static class FzcLidarPhase extends Spec<demo.testcharm.dto.FzcLidarPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("distCm").defaultValue(null);
            property("signal").defaultValue(null);
            property("noFrame").defaultValue(null);
            property("badChecksum").defaultValue(null);
            property("garbageHeader").defaultValue(null);
            property("partialFrame").defaultValue(null);
            property("uartFailAt").defaultValue(null);
            property("getDist").defaultValue(null);
            property("getDistNull").defaultValue(null);
        }
    }
}
