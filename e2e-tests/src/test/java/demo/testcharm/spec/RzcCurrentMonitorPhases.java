package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcCurrentMonitorPhases {

    public static class RzcCurrentMonitorPhase extends Spec<demo.testcharm.dto.RzcCurrentMonitorPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("currentMa").defaultValue(null);
        }
    }
}
