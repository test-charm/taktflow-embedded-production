package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcSelfTestSetups {

    public static class CvcSelfTestSetup extends Spec<demo.testcharm.dto.CvcSelfTestSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcSelfTestPhase");
        }
    }
}
