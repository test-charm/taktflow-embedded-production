package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSchedulerPhases {

    public static class RzcSchedulerPhase extends Spec<demo.testcharm.dto.RzcSchedulerPhase> {
        @Override
        public void main() {
            property("skipInit").defaultValue(null);
            property("reinit").defaultValue(null);
            property("ticks").defaultValue(null);
        }
    }
}
