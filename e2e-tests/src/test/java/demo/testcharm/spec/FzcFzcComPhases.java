package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcFzcComPhases {

    public static class FzcFzcComPhase extends Spec<demo.testcharm.dto.FzcFzcComPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("data").defaultValue(null);
            property("dataId").defaultValue(null);
            property("length").defaultValue(null);
            property("repeats").defaultValue(null);
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("vehicleState").defaultValue(null);
            property("faultMask").defaultValue(null);
            property("steerAngle").defaultValue(null);
            property("steerFault").defaultValue(null);
            property("brakePos").defaultValue(null);
            property("brakeFault").defaultValue(null);
            property("motorCutoff").defaultValue(null);
            property("lidarZone").defaultValue(null);
            property("lidarDist").defaultValue(null);
            property("lidarSignal").defaultValue(null);
        }
    }
}
