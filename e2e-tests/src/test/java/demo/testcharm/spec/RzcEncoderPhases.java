package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcEncoderPhases {

    public static class RzcEncoderPhase extends Spec<demo.testcharm.dto.RzcEncoderPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("count").defaultValue(null);
            property("deltaPerCycle").defaultValue(null);
            property("encoderDir").defaultValue(null);
            property("commandedDir").defaultValue(null);
            property("torqueEcho").defaultValue(null);
        }
    }
}
