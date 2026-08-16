package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcComPhases {

    public static class RzcComPhase extends Spec<demo.testcharm.dto.RzcComPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("pduId").defaultValue(null);
            property("data").defaultValue(null);
            property("length").defaultValue(null);
            property("repeats").defaultValue(null);
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("estop").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("torqueCmd").defaultValue(null);
            property("faultMask").defaultValue(null);
            property("torqueEcho").defaultValue(null);
            property("speedRpm").defaultValue(null);
            property("motorDir").defaultValue(null);
            property("motorEnable").defaultValue(null);
            property("motorFault").defaultValue(null);
            property("currentMa").defaultValue(null);
            property("overcurrent").defaultValue(null);
            property("temp1Dc").defaultValue(null);
            property("temp2Dc").defaultValue(null);
            property("deratingPct").defaultValue(null);
            property("batteryMv").defaultValue(null);
            property("batteryStatus").defaultValue(null);
        }
    }
}
