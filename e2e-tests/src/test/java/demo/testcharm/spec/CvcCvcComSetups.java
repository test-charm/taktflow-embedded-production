package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;

public class CvcCvcComSetups {

    public static class CvcCvcComSetup extends Spec<demo.testcharm.dto.CvcCvcComSetup> {
        @Override
        public void main() {
            property("phases[]").apply("CvcCvcComPhase");
        }
    }
}
