package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcWatchdogPhases {

    public static class CvcWatchdogPhase extends Spec<demo.testcharm.dto.CvcWatchdogPhase> {
        @Override
        public void main() {
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("loopComplete").defaultValue(null);
            property("canaryOk").defaultValue(null);
            property("ramOk").defaultValue(null);
            property("canOk").defaultValue(null);
            property("feedCount").defaultValue(null);
        }
    }
}
