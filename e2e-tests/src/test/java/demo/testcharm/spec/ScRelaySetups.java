package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScRelaySetups {

    public static class ScRelaySetup extends Spec<demo.testcharm.dto.ScRelaySetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScRelayPhase");
        }
    }
}
