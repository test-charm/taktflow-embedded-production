package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScSelfTestPhases {

    public static class ScSelfTestPhase extends Spec<demo.testcharm.dto.ScSelfTestPhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("b1").defaultValue(null);
            property("b2").defaultValue(null);
            property("b3").defaultValue(null);
            property("b4").defaultValue(null);
            property("b5").defaultValue(null);
            property("b6").defaultValue(null);
            property("b7").defaultValue(null);
            property("flashIncr").defaultValue(null);
            property("dcanErr").defaultValue(null);
            property("readback").defaultValue(null);
            property("corruptCanary").defaultValue(null);
            property("corruptRam").defaultValue(null);
            property("repeats").defaultValue(null);
        }
    }
}
