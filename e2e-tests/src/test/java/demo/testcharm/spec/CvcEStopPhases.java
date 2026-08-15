package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcEStopPhases {

    public static class CvcEStopPhase extends Spec<demo.testcharm.dto.CvcEStopPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("pin").defaultValue(null);
            property("readFail").defaultValue(null);
            property("skipInit").defaultValue(null);
        }
    }
}
