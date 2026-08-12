package demo.testcharm.spec;

import org.testcharm.jfactory.Spec;
import org.testcharm.jfactory.Trait;
import demo.testcharm.entity.Order;
import demo.testcharm.entity.OrderLine;

import static demo.testcharm.entity.Order.OrderStatus.toBeDelivered;

public class Orders {
    public static class 订单 extends Spec<Order> {
        @Override
        public void main() {
            property("id").ignore();
            property("lines").reverseAssociation("order");
        }

        @Trait
        public void 未发货的() {
            property("status").value(toBeDelivered);
            property("deliverNo").value(null);
            property("deliveredAt").value(null);
        }
    }

    public static class 订单项 extends Spec<OrderLine> {
        @Override
        public void main() {
            property("id").ignore();
        }
    }

}
