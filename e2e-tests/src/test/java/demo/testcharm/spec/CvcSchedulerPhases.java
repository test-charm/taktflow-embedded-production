package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcSchedulerPhases {

    public static class CvcSchedulerPhase extends Spec<demo.testcharm.dto.CvcSchedulerPhase> {
        @Override
        public void main() {
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("nullRunnables").defaultValue(null);
            property("zeroCount").defaultValue(null);
            property("tableIndex").defaultValue(null);
        }
    }
}
