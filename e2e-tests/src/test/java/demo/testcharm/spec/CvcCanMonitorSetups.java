package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcCanMonitorSetups {

    public static class CvcCanMonitorSetup extends Spec<demo.testcharm.dto.CvcCanMonitorSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcCanMonitorPhase");
        }
    }
}
