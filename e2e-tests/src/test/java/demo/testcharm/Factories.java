package demo.testcharm;

import demo.testcharm.dto.CvcEStopSetup;
import demo.testcharm.dto.CvcCvcComSetup;
import demo.testcharm.dto.CvcCanMonitorSetup;
import demo.testcharm.dto.CvcHeartbeatSetup;
import demo.testcharm.dto.CvcSelfTestSetup;
import demo.testcharm.dto.CvcSchedulerSetup;
import demo.testcharm.dto.CvcWatchdogSetup;
import demo.testcharm.dto.CvcPedalSetup;
import demo.testcharm.dto.CvcVehicleStateSetup;
import demo.testcharm.dto.FzcSteeringSetup;
import demo.testcharm.dto.FzcBrakeSetup;
import demo.testcharm.dto.FzcLidarSetup;
import demo.testcharm.dto.FzcHeartbeatSetup;
import demo.testcharm.dto.FzcCanMonitorSetup;
import demo.testcharm.dto.FzcSafetySetup;
import demo.testcharm.dto.RzcMotorSetup;
import demo.testcharm.dto.RzcBatterySetup;
import demo.testcharm.dto.RzcTempMonitorSetup;
import demo.testcharm.dto.RzcComSetup;
import org.mockserver.client.MockServerClient;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Lazy;
import org.testcharm.cucumber.restful.RestfulStep;
import org.testcharm.jfactory.CompositeDataRepository;
import org.testcharm.jfactory.JFactory;
import org.testcharm.jfactory.MemoryDataRepository;

@Configuration
public class Factories {

    @Bean
    public MockServerClient createMockServerClient() {
        return new MockServerClient("127.0.0.1", 1080) {
            @Override
            public MockServerClient reset() {
                return this;
            }

            @Override
            public void close() {
            }
        };
    }

    @Bean
    public JFactory factorySet(@Lazy RestfulStep restfulStep) {
        return new EntityFactory(new CompositeDataRepository(new MemoryDataRepository())
                .registerByType(CvcPedalSetup.class, new CvcPedalSetupDataRepository(restfulStep))
                .registerByType(CvcVehicleStateSetup.class, new CvcVehicleStateSetupDataRepository(restfulStep))
                .registerByType(CvcEStopSetup.class, new CvcEStopSetupDataRepository(restfulStep))
                .registerByType(CvcCvcComSetup.class, new CvcCvcComSetupDataRepository(restfulStep))
                .registerByType(CvcHeartbeatSetup.class, new CvcHeartbeatSetupDataRepository(restfulStep))
                .registerByType(CvcCanMonitorSetup.class, new CvcCanMonitorSetupDataRepository(restfulStep))
                .registerByType(CvcWatchdogSetup.class, new CvcWatchdogSetupDataRepository(restfulStep))
                .registerByType(CvcSelfTestSetup.class, new CvcSelfTestSetupDataRepository(restfulStep))
                .registerByType(CvcSchedulerSetup.class, new CvcSchedulerSetupDataRepository(restfulStep))
                .registerByType(FzcSteeringSetup.class, new FzcSteeringSetupDataRepository(restfulStep))
                .registerByType(FzcBrakeSetup.class, new FzcBrakeSetupDataRepository(restfulStep))
                .registerByType(FzcLidarSetup.class, new FzcLidarSetupDataRepository(restfulStep))
                .registerByType(FzcHeartbeatSetup.class, new FzcHeartbeatSetupDataRepository(restfulStep))
                .registerByType(FzcCanMonitorSetup.class, new FzcCanMonitorSetupDataRepository(restfulStep))
                .registerByType(FzcSafetySetup.class, new FzcSafetySetupDataRepository(restfulStep))
                .registerByType(RzcMotorSetup.class, new RzcMotorSetupDataRepository(restfulStep))
                .registerByType(RzcBatterySetup.class, new RzcBatterySetupDataRepository(restfulStep))
                .registerByType(RzcTempMonitorSetup.class, new RzcTempMonitorSetupDataRepository(restfulStep))
                .registerByType(RzcComSetup.class, new RzcComSetupDataRepository(restfulStep)));
    }

    public static class CvcPedalSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcPedalSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/pedal-torque/setup", object);
        }
    }

    public static class CvcVehicleStateSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcVehicleStateSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/vehicle-state/setup", object);
        }
    }

    public static class CvcEStopSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcEStopSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/estop/setup", object);
        }
    }

    public static class CvcCvcComSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcCvcComSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/cvccom/setup", object);
        }
    }

    public static class CvcHeartbeatSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcHeartbeatSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/heartbeat/setup", object);
        }
    }

    public static class CvcCanMonitorSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcCanMonitorSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/canmonitor/setup", object);
        }
    }

    public static class CvcWatchdogSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcWatchdogSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/watchdog/setup", object);
        }
    }

    public static class CvcSelfTestSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcSelfTestSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/selftest/setup", object);
        }
    }

    public static class CvcSchedulerSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public CvcSchedulerSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/cvc/scheduler/setup", object);
        }
    }

    public static class FzcSteeringSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcSteeringSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/steering/setup", object);
        }
    }

    public static class FzcBrakeSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcBrakeSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/brake/setup", object);
        }
    }

    public static class FzcLidarSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcLidarSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/lidar/setup", object);
        }
    }

    public static class FzcHeartbeatSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcHeartbeatSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/heartbeat/setup", object);
        }
    }

    public static class FzcCanMonitorSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcCanMonitorSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/canmonitor/setup", object);
        }
    }

    public static class FzcSafetySetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public FzcSafetySetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/fzc/safety/setup", object);
        }
    }

    public static class RzcMotorSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public RzcMotorSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/rzc/motor/setup", object);
        }
    }

    public static class RzcBatterySetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public RzcBatterySetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/rzc/battery/setup", object);
        }
    }

    public static class RzcTempMonitorSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public RzcTempMonitorSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/rzc/temponitor/setup", object);
        }
    }

    public static class RzcComSetupDataRepository extends MemoryDataRepository {

        private final RestfulStep restfulStep;

        public RzcComSetupDataRepository(RestfulStep restfulStep) {
            this.restfulStep = restfulStep;
        }

        @Override
        public void save(Object object) {
            super.save(object);
            restfulStep.postObjectInJson("/api/test/asw/rzc/rzccom/setup", object);
        }
    }
}
