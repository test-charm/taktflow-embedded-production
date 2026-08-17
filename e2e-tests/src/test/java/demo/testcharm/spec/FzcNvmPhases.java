package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcNvmPhases {

    public static class FzcNvmPhase extends Spec<demo.testcharm.dto.FzcNvmPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("repeats").defaultValue(null);
            property("dtcId").defaultValue(null);
            property("steerAngle").defaultValue(null);
            property("brakePos").defaultValue(null);
            property("lidarDist").defaultValue(null);
            property("slot").defaultValue(null);
            property("nullRecord").defaultValue(null);
            property("nullCal").defaultValue(null);
            property("steerCenterOffset").defaultValue(null);
            property("steerGain").defaultValue(null);
            property("brakePosOffset").defaultValue(null);
            property("brakeGain").defaultValue(null);
            property("lidarWarnCm").defaultValue(null);
            property("lidarBrakeCm").defaultValue(null);
            property("lidarEmergencyCm").defaultValue(null);
            property("dataLen").defaultValue(null);
            property("nullCrc").defaultValue(null);
        }
    }
}
