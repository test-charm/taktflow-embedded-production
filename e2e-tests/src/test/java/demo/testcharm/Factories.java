package demo.testcharm;

import demo.testcharm.dto.CvcPedalSetup;
import demo.testcharm.dto.CvcVehicleStateSetup;
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
                .registerByType(CvcVehicleStateSetup.class, new CvcVehicleStateSetupDataRepository(restfulStep)));
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
}
