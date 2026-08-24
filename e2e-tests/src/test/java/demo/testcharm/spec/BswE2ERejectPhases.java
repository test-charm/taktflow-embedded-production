package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class BswE2ERejectPhases {

    public static class BswE2ERejectPhase extends Spec<demo.testcharm.dto.BswE2ERejectPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("target").defaultValue(null);
            property("mode").defaultValue(null);
            property("count").defaultValue(null);
            property("intervalMs").defaultValue(null);
            property("settleMs").defaultValue(null);
            property("observeMs").defaultValue(null);
            property("restartCvc").defaultValue(null);
        }
    }
}