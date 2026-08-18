package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSelfTestSetups {

    public static class RzcSelfTestSetup extends Spec<demo.testcharm.dto.RzcSelfTestSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcSelfTestPhase");
        }
    }
}
