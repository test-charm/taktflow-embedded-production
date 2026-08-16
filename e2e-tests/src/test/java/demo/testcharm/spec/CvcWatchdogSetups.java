package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcWatchdogSetups {

    public static class CvcWatchdogSetup extends Spec<demo.testcharm.dto.CvcWatchdogSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcWatchdogPhase");
        }
    }
}
