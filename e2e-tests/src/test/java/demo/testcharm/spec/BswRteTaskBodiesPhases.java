package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswRteTaskBodiesPhases {

    public static class BswRteTaskBodiesPhase extends Spec<demo.testcharm.dto.BswRteTaskBodiesPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("task").defaultValue(null);
            property("idleIters").defaultValue(null);
        }
    }
}
