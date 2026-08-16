package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcHeartbeatSetups {

    public static class CvcHeartbeatSetup extends Spec<demo.testcharm.dto.CvcHeartbeatSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcHeartbeatPhase");
        }
    }
}
