package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcVehicleStateSetups {

    public static class CvcVehicleStateSetup extends Spec<demo.testcharm.dto.CvcVehicleStateSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcVehicleStatePhase");
        }
    }
}
