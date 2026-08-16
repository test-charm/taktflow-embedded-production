package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcHeartbeatPhases {

    public static class CvcHeartbeatPhase extends Spec<demo.testcharm.dto.CvcHeartbeatPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("rxEcu").defaultValue(null);
            property("resetComm").defaultValue(null);
        }
    }
}
