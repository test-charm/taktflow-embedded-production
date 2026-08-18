package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScWatchdogSetups {

    public static class ScWatchdogSetup extends Spec<demo.testcharm.dto.ScWatchdogSetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScWatchdogPhase");
        }
    }
}
