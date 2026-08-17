package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcCurrentMonitorSetups {

    public static class RzcCurrentMonitorSetup extends Spec<demo.testcharm.dto.RzcCurrentMonitorSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcCurrentMonitorPhase");
        }
    }
}
