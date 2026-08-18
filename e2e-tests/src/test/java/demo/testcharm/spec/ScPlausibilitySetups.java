package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScPlausibilitySetups {

    public static class ScPlausibilitySetup extends Spec<demo.testcharm.dto.ScPlausibilitySetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScPlausibilityPhase");
        }
    }
}
