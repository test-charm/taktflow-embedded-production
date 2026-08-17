package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSafetySetups {

    public static class FzcSafetySetup extends Spec<demo.testcharm.dto.FzcSafetySetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcSafetyPhase");
        }
    }
}
