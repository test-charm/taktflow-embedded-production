package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcSchedulerSetups {

    public static class CvcSchedulerSetup extends Spec<demo.testcharm.dto.CvcSchedulerSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcSchedulerPhase");
        }
    }
}
