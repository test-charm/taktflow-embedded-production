package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcEStopSetups {

    public static class CvcEStopSetup extends Spec<demo.testcharm.dto.CvcEStopSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcEStopPhase");
        }
    }
}
