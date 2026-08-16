package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcBatteryPhases {

    public static class RzcBatteryPhase extends Spec<demo.testcharm.dto.RzcBatteryPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("voltageMv").defaultValue(null);
        }
    }
}
