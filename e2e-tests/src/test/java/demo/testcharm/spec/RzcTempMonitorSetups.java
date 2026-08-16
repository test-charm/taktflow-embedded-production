package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcTempMonitorSetups {

    public static class RzcTempMonitorSetup extends Spec<demo.testcharm.dto.RzcTempMonitorSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcTempMonitorPhase");
        }
    }
}
