package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswRteTaskBodiesSetups {

    public static class BswRteTaskBodiesSetup extends Spec<demo.testcharm.dto.BswRteTaskBodiesSetup> {
        @Override
        public void main() {
            property("phases[]").apply("BswRteTaskBodiesPhase");
        }
    }
}
