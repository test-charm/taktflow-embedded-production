package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScRelayPhases {

    public static class ScRelayPhase extends Spec<demo.testcharm.dto.ScRelayPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("repeats").defaultValue(null);
            property("estop").defaultValue(null);
            property("hb").defaultValue(null);
            property("plaus").defaultValue(null);
            property("creep").defaultValue(null);
            property("e2e").defaultValue(null);
            property("selftest").defaultValue(null);
            property("esm").defaultValue(null);
            property("busoff").defaultValue(null);
            property("busSilent").defaultValue(null);
            property("value").defaultValue(null);
        }
    }
}
