package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcNvmSetups {

    public static class RzcNvmSetup extends Spec<demo.testcharm.dto.RzcNvmSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcNvmPhase");
        }
    }
}
