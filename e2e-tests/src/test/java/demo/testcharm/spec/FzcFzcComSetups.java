package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcFzcComSetups {

    public static class FzcFzcComSetup extends Spec<demo.testcharm.dto.FzcFzcComSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcFzcComPhase");
        }
    }
}
