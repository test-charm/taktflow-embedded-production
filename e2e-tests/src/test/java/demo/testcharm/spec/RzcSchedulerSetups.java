package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSchedulerSetups {

    public static class RzcSchedulerSetup extends Spec<demo.testcharm.dto.RzcSchedulerSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcSchedulerPhase");
        }
    }
}
