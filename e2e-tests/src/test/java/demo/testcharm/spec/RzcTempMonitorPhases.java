package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcTempMonitorPhases {

    public static class RzcTempMonitorPhase extends Spec<demo.testcharm.dto.RzcTempMonitorPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("tempDc").defaultValue(null);
            property("temp2Dc").defaultValue(null);
            property("ioFault").defaultValue(null);
            property("temp2Fail").defaultValue(null);
        }
    }
}
