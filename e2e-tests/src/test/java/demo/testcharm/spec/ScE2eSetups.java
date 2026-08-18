package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScE2eSetups {

    public static class ScE2eSetup extends Spec<demo.testcharm.dto.ScE2eSetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScE2ePhase");
        }
    }
}
