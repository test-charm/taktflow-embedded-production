package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcBrakeSetups {

    public static class FzcBrakeSetup extends Spec<demo.testcharm.dto.FzcBrakeSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcBrakePhase");
        }
    }
}
