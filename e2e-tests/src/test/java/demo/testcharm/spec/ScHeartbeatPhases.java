package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScHeartbeatPhases {

    public static class ScHeartbeatPhase extends Spec<demo.testcharm.dto.ScHeartbeatPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("ticks").defaultValue(null);
            property("ecu").defaultValue(null);
            property("repeats").defaultValue(null);
            property("payload3").defaultValue(null);
            property("notifyA").defaultValue(null);
            property("notifyB").defaultValue(null);
        }
    }
}
