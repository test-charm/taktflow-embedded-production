package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcHeartbeatPhases {

    public static class FzcHeartbeatPhase extends Spec<demo.testcharm.dto.FzcHeartbeatPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("faultMask").defaultValue(null);
        }
    }
}
