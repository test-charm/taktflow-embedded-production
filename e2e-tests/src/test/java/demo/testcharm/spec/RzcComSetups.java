package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcComSetups {

    public static class RzcComSetup extends Spec<demo.testcharm.dto.RzcComSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcComPhase");
        }
    }
}
