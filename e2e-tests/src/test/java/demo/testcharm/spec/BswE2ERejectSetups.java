package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswE2ERejectSetups {

    public static class BswE2ERejectSetup extends Spec<demo.testcharm.dto.BswE2ERejectSetup> {
        @Override
        public void main() {
            property("phases[]").apply("BswE2ERejectPhase");
        }
    }
}