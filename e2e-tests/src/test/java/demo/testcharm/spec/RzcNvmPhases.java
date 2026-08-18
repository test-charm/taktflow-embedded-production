package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcNvmPhases {

    public static class RzcNvmPhase extends Spec<demo.testcharm.dto.RzcNvmPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("repeats").defaultValue(null);
            property("dtcId").defaultValue(null);
            property("status").defaultValue(null);
            property("timestamp").defaultValue(null);
            property("motorCurrentMa").defaultValue(null);
            property("motorTempDdc").defaultValue(null);
            property("motorSpeedRpm").defaultValue(null);
            property("batteryMv").defaultValue(null);
            property("torqueCmdPct").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("slot").defaultValue(null);
            property("nullFreeze").defaultValue(null);
            property("nullEntry").defaultValue(null);
            property("dataLen").defaultValue(null);
        }
    }
}
