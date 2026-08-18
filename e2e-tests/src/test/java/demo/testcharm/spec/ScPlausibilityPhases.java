package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScPlausibilityPhases {

    public static class ScPlausibilityPhase extends Spec<demo.testcharm.dto.ScPlausibilityPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("torque").defaultValue(null);
            property("current").defaultValue(null);
            property("vehValid").defaultValue(null);
            property("curValid").defaultValue(null);
            property("brakeFault").defaultValue(null);
            property("repeats").defaultValue(null);
            property("ticks").defaultValue(null);
            property("expected").defaultValue(null);
            property("actual").defaultValue(null);
        }
    }
}
