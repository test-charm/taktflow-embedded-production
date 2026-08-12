package demo.testcharm;

import org.testcharm.jfactory.CompositeDataRepository;
import org.testcharm.jfactory.JFactory;
import org.testcharm.jfactory.MemoryDataRepository;
import org.mockserver.client.MockServerClient;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

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
    public JFactory factorySet() {
        return new EntityFactory(new CompositeDataRepository(new MemoryDataRepository()));
    }
}
