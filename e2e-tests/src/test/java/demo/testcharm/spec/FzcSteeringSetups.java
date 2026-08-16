package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSteeringSetups {

    public static class FzcSteeringSetup extends Spec<demo.testcharm.dto.FzcSteeringSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcSteeringPhase");
        }
    }
}
