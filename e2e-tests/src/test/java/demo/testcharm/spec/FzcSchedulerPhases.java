package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSchedulerPhases {

    public static class FzcSchedulerPhase extends Spec<demo.testcharm.dto.FzcSchedulerPhase> {
        @Override
        public void main() {
            property("skipInit").defaultValue(null);
            property("reinit").defaultValue(null);
        }
    }
}
