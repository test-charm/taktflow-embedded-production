package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScWatchdogPhases {

    public static class ScWatchdogPhase extends Spec<demo.testcharm.dto.ScWatchdogPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("ok").defaultValue(null);
            property("repeats").defaultValue(null);
        }
    }
}
