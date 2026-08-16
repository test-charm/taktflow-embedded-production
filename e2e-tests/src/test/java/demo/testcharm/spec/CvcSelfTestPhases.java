package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcSelfTestPhases {

    public static class CvcSelfTestPhase extends Spec<demo.testcharm.dto.CvcSelfTestPhase> {
        @Override
        public void main() {
            property("spi").defaultValue(null);
            property("can").defaultValue(null);
            property("nvm").defaultValue(null);
            property("oled").defaultValue(null);
            property("mpu").defaultValue(null);
            property("canary").defaultValue(null);
            property("ram").defaultValue(null);
        }
    }
}
