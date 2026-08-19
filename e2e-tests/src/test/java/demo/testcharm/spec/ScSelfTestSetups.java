package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScSelfTestSetups {

    public static class ScSelfTestSetup extends Spec<demo.testcharm.dto.ScSelfTestSetup> {
        @Override
        public void main() {
            property("phases[]").apply("ScSelfTestPhase");
        }
    }
}
