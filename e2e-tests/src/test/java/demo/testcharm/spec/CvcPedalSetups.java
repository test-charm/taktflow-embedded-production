package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcPedalSetups {

    public static class CvcPedalSetup extends Spec<demo.testcharm.dto.CvcPedalSetup> {
        @Override
        public void main() {
            property("resetSpiFault").defaultValue(true);
            property("resetDither").defaultValue(true);
            property("resetRecover").defaultValue(true);
            property("cycles").defaultValue(null);
            property("spiFaultSensor").defaultValue(null);
            property("recoverCycles").defaultValue(null);
            property("ditherAmplitude").defaultValue(null);
        }
    }
}
