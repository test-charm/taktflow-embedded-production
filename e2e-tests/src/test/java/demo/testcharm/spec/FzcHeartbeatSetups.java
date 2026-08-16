package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcHeartbeatSetups {

    public static class FzcHeartbeatSetup extends Spec<demo.testcharm.dto.FzcHeartbeatSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcHeartbeatPhase");
        }
    }
}
