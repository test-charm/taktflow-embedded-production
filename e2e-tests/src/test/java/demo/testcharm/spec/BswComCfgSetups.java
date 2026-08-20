package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswComCfgSetups {

    public static class BswComCfgSetup extends Spec<demo.testcharm.dto.BswComCfgSetup> {
        @Override
        public void main() {
            property("phases[]").apply("BswComCfgPhase");
        }
    }
}
