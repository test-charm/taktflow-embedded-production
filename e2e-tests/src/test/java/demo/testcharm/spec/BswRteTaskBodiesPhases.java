package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswRteTaskBodiesPhases {

    public static class BswRteTaskBodiesPhase extends Spec<demo.testcharm.dto.BswRteTaskBodiesPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("targets").defaultValue(null);
            property("windowMs").defaultValue(null);
            property("minFrames").defaultValue(null);
            property("periodTolerancePct").defaultValue(null);
            property("budgetMs").defaultValue(null);
            property("restartCvc").defaultValue(null);
        }
    }
}
