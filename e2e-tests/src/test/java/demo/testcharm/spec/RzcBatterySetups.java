package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcBatterySetups {

    public static class RzcBatterySetup extends Spec<demo.testcharm.dto.RzcBatterySetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcBatteryPhase");
        }
    }
}
