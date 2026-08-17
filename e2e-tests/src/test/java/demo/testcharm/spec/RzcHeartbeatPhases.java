package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcHeartbeatPhases {

    public static class RzcHeartbeatPhase extends Spec<demo.testcharm.dto.RzcHeartbeatPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("faultMask").defaultValue(null);
        }
    }
}
