package demo.testcharm;

import org.testcharm.jfactory.DataRepository;
import org.testcharm.jfactory.JFactory;
import org.testcharm.jfactory.Spec;
import org.testcharm.util.Classes;

public class EntityFactory extends JFactory {

    public EntityFactory(DataRepository dataRepository) {
        super(dataRepository);
        configFactory();
    }

    private void configFactory() {
        Classes.subTypesOf(Spec.class, "demo.testcharm.spec").forEach(c -> register((Class) c));

        ignoreDefaultValue(p -> p.getName().equals("id"));
        ignoreDefaultValue(p -> p.getName().equals("createdAt"));
    }
}
