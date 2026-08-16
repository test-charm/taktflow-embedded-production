package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcNvmPhases {

    public static class CvcNvmPhase extends Spec<demo.testcharm.dto.CvcNvmPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("repeats").defaultValue(null);
            property("dtcId").defaultValue(null);
            property("status").defaultValue(null);
            property("ffMode").defaultValue(null);
            property("slot").defaultValue(null);
            property("nullEntry").defaultValue(null);
            property("nullCal").defaultValue(null);
            property("pThreshold").defaultValue(null);
            property("pDebounce").defaultValue(null);
            property("stuckThreshold").defaultValue(null);
            property("stuckCycles").defaultValue(null);
            property("lut0").defaultValue(null);
            property("dataLen").defaultValue(null);
            property("nullCrc").defaultValue(null);
        }
    }
}
