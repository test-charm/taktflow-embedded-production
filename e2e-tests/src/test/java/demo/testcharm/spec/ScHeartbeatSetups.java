package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScHeartbeatSetups {

    public static class ScHeartbeatSetup extends Spec<demo.testcharm.dto.ScHeartbeatSetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScHeartbeatPhase");
        }
    }
}
