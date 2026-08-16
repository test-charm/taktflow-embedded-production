package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcNvmSetups {

    public static class CvcNvmSetup extends Spec<demo.testcharm.dto.CvcNvmSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcNvmPhase");
        }
    }
}
