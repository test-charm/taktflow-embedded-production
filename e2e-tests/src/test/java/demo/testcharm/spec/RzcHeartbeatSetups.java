package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcHeartbeatSetups {

    public static class RzcHeartbeatSetup extends Spec<demo.testcharm.dto.RzcHeartbeatSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcHeartbeatPhase");
        }
    }
}
