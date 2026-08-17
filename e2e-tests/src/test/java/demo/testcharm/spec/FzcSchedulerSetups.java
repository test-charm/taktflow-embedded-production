package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcSchedulerSetups {

    public static class FzcSchedulerSetup extends Spec<demo.testcharm.dto.FzcSchedulerSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcSchedulerPhase");
        }
    }
}
