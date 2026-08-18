package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class ScE2ePhases {

    public static class ScE2ePhase extends Spec<demo.testcharm.dto.ScE2ePhase> {
        @Override
        public void main() {
            property("op").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("dataId").defaultValue(null);
            property("msgIndex").defaultValue(null);
            property("dlc").defaultValue(null);
            property("alive").defaultValue(null);
            property("crcCorrupt").defaultValue(null);
            property("dataIdCorrupt").defaultValue(null);
            property("payloadCorrupt").defaultValue(null);
            property("nullData").defaultValue(null);
            property("ticks").defaultValue(null);
            property("len").defaultValue(null);
        }
    }
}
