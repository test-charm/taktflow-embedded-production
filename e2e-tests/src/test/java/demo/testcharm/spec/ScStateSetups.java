package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScStateSetups {

    public static class ScStateSetup extends Spec<demo.testcharm.dto.ScStateSetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScStatePhase");
        }
    }
}
