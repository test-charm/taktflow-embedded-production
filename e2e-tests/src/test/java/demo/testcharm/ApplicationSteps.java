package demo.testcharm;

import org.testcharm.cucumber.restful.RestfulStep;
import org.testcharm.dal.Assertions;
import io.cucumber.java.Before;
import io.cucumber.spring.CucumberContextConfiguration;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.test.context.SpringBootContextLoader;
import org.springframework.test.context.ContextConfiguration;

import javax.annotation.PostConstruct;

@ContextConfiguration(classes = {CucumberConfiguration.class}, loader = SpringBootContextLoader.class)
@CucumberContextConfiguration
public class ApplicationSteps {

    @Value("${testcharm.dal.dumpinput:true}")
    private boolean dalDumpInput;

    @Before
    public void disableDALDump() {
        Assertions.dumpInput(dalDumpInput);
    }

    @Autowired
    private RestfulStep restfulStep;

    @PostConstruct
    public void setBaseUrl() {
        restfulStep.setBaseUrl("http://127.0.0.1:8091");
    }
}
