package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcLidarSetups {

    public static class FzcLidarSetup extends Spec<demo.testcharm.dto.FzcLidarSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcLidarPhase");
        }
    }
}
