package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class RzcMotorSetups {

    public static class RzcMotorSetup extends Spec<demo.testcharm.dto.RzcMotorSetup> {
        @Override
        public void main() {
            property("phases[]").apply("RzcMotorPhase");
        }
    }
}
