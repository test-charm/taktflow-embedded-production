package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswComCfgPhases {

    public static class BswComCfgPhase extends Spec<demo.testcharm.dto.BswComCfgPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("id").defaultValue(null);
            property("table").defaultValue(null);
        }
    }
}
