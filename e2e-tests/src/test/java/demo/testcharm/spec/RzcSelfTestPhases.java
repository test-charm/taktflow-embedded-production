package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcSelfTestPhases {

    public static class RzcSelfTestPhase extends Spec<demo.testcharm.dto.RzcSelfTestPhase> {
        @Override
        public void main() {
            property("skipInit").defaultValue(null);
            property("initNull").defaultValue(null);
            property("bts7960").defaultValue(null);
            property("acs723").defaultValue(null);
            property("ntc").defaultValue(null);
            property("encoder").defaultValue(null);
            property("can").defaultValue(null);
            property("mpu").defaultValue(null);
            property("canary").defaultValue(null);
            property("ram").defaultValue(null);
        }
    }
}
