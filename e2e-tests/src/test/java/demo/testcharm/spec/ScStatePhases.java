package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScStatePhases {

    public static class ScStatePhase extends Spec<demo.testcharm.dto.ScStatePhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("newState").defaultValue(null);
            property("state").defaultValue(null);
        }
    }
}
