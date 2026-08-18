package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSafetySetups {

    public static class RzcSafetySetup extends Spec<demo.testcharm.dto.RzcSafetySetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcSafetyPhase");
        }
    }
}
