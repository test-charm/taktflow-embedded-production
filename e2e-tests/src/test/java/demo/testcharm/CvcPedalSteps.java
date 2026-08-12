package demo.testcharm;

import io.cucumber.java.zh_cn.而且;
import io.cucumber.java.zh_cn.假如;
import org.springframework.beans.factory.annotation.Autowired;
import org.testcharm.cucumber.restful.RestfulStep;

public class CvcPedalSteps {

    @Autowired
    private RestfulStep restfulStep;

    @假如("车辆状态为 {word}")
    public void vehicleStateIs(String stateName) {
        restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup",
                new VehicleStateRequest(stateName));
    }

    @而且("执行 {int} 个周期")
    public void runCycles(int cycles) {
        restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup",
                new CyclesRequest(cycles));
    }

    @而且("SPI故障注入传感器 {int}")
    public void injectSpiFault(int sensorId) {
        restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup",
                new SpiFaultRequest(sensorId));
    }

    @而且("关闭传感器抖动")
    public void disableDither() {
        restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup",
                new DitherRequest(0));
    }

    @而且("恢复阶段执行 {int} 个周期")
    public void recoverCycles(int cycles) {
        restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup",
                new RecoverCyclesRequest(cycles));
    }

    @SuppressWarnings("unused")
    public static class VehicleStateRequest {
        private final String vehicleState;
        private final boolean resetSpiFault;
        private final boolean resetDither;
        private final boolean resetRecover;
        public VehicleStateRequest(String vehicleState) {
            this.vehicleState = vehicleState;
            this.resetSpiFault = true;
            this.resetDither = true;
            this.resetRecover = true;
        }
        public String getVehicleState() { return vehicleState; }
        public boolean isResetSpiFault() { return resetSpiFault; }
        public boolean isResetDither() { return resetDither; }
        public boolean isResetRecover() { return resetRecover; }
    }

    @SuppressWarnings("unused")
    public static class CyclesRequest {
        private final Integer cycles;
        public CyclesRequest(Integer cycles) { this.cycles = cycles; }
        public Integer getCycles() { return cycles; }
    }

    @SuppressWarnings("unused")
    public static class SpiFaultRequest {
        private final Integer spiFaultSensor;
        public SpiFaultRequest(Integer spiFaultSensor) { this.spiFaultSensor = spiFaultSensor; }
        public Integer getSpiFaultSensor() { return spiFaultSensor; }
    }

    @SuppressWarnings("unused")
    public static class DitherRequest {
        private final Integer ditherAmplitude;
        public DitherRequest(Integer ditherAmplitude) { this.ditherAmplitude = ditherAmplitude; }
        public Integer getDitherAmplitude() { return ditherAmplitude; }
    }

    @SuppressWarnings("unused")
    public static class RecoverCyclesRequest {
        private final Integer recoverCycles;
        public RecoverCyclesRequest(Integer recoverCycles) { this.recoverCycles = recoverCycles; }
        public Integer getRecoverCycles() { return recoverCycles; }
    }
}
