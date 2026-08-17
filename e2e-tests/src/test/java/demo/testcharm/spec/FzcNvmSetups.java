package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcNvmSetups {

    public static class FzcNvmSetup extends Spec<demo.testcharm.dto.FzcNvmSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcNvmPhase");
        }
    }
}
