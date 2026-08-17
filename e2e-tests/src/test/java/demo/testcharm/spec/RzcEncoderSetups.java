package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcEncoderSetups {

    public static class RzcEncoderSetup extends Spec<demo.testcharm.dto.RzcEncoderSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcEncoderPhase");
        }
    }
}
