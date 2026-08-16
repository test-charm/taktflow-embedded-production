package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcCanMonitorSetups {

    public static class FzcCanMonitorSetup extends Spec<demo.testcharm.dto.FzcCanMonitorSetup> {
        @Override
        public void main() {
            property("phases[]").apply("FzcCanMonitorPhase");
        }
    }
}
