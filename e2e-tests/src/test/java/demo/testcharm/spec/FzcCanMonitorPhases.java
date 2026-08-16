package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class FzcCanMonitorPhases {

    public static class FzcCanMonitorPhase extends Spec<demo.testcharm.dto.FzcCanMonitorPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("canMode").defaultValue(null);
            property("tec").defaultValue(null);
            property("rec").defaultValue(null);
            property("notifyRx").defaultValue(null);
        }
    }
}
