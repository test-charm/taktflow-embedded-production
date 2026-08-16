package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcCanMonitorPhases {

    public static class CvcCanMonitorPhase extends Spec<demo.testcharm.dto.CvcCanMonitorPhase> {
        @Override
        public void main() {
            property("cycles").defaultValue(null);
            property("skipInit").defaultValue(null);
            property("isBusOff").defaultValue(null);
            property("rxMsgCount").defaultValue(null);
            property("rxInc").defaultValue(null);
            property("errorWarning").defaultValue(null);
            property("timeStartMs").defaultValue(null);
            property("timeStepMs").defaultValue(null);
            property("recovery").defaultValue(null);
            property("recoveryTimeMs").defaultValue(null);
        }
    }
}
