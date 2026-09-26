#include "DeliveryService.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

#include "Errors.h"
#include "Pricing.h"

namespace eats {

// ============================ 账号 ============================

    Customer* DeliveryService::findCustomer(std::string_view studentNo) {
        const std::string key(studentNo);
        auto hits = store_.customers.where(
                [&key](const Customer& c) { return c.studentNo == key; });
        return hits.empty() ? nullptr : hits.front();
    }

    Customer& DeliveryService::createCustomer(std::string_view studentNo,
                                              std::string_view name,
                                              std::string_view building) {
        if (findCustomer(studentNo)) fail("学号 " + std::string(studentNo) + " 已经注册过了");

        Customer c;
        c.studentNo = std::string(studentNo);
        c.name      = name.empty() ? "同学" + std::string(studentNo) : std::string(name);
        c.building  = building.empty() ? "未填宿舍楼" : std::string(building);
        return store_.customers.add(std::move(c));
    }

    Customer& DeliveryService::customer(Id id) {
        auto* c = store_.customers.find(id);
        if (!c) fail("用户不存在");
        return *c;
    }

    Rider* DeliveryService::findRider(std::string_view staffNo) {
        const std::string key(staffNo);
        auto hits = store_.riders.where([&key](const Rider& r) { return r.staffNo == key; });
        return hits.empty() ? nullptr : hits.front();
    }

    Rider& DeliveryService::createRider(std::string_view staffNo, std::string_view name) {
        if (findRider(staffNo)) fail("工号 " + std::string(staffNo) + " 已经注册过了");

        Rider r;
        r.staffNo = std::string(staffNo);
        r.name    = name.empty() ? "骑手" + std::string(staffNo) : std::string(name);
        return store_.riders.add(std::move(r));
    }

    Rider& DeliveryService::rider(Id id) {
        auto* r = store_.riders.find(id);
        if (!r) fail("骑手不存在");
        return *r;
    }

// ============================ 浏览 ============================

    std::vector<Merchant*> DeliveryService::shops(std::string_view keyword) {
        auto list = store_.merchants.all();
        if (!keyword.empty()) {
            std::erase_if(list, [keyword](const Merchant* m) {
                return m->name.find(keyword) == std::string::npos
                       && m->location.find(keyword) == std::string::npos;
            });
        }
        std::sort(list.begin(), list.end(), [](const Merchant* a, const Merchant* b) {
            if (a->accepting != b->accepting) return a->accepting;   // 营业中的排前面
            return a->rating > b->rating;
        });
        return list;
    }

    Merchant& DeliveryService::shop(Id id) {
        auto* m = store_.merchants.find(id);
        if (!m) fail("商家不存在");
        return *m;
    }

    Order& DeliveryService::order(Id id) {
        auto* o = store_.orders.find(id);
        if (!o) fail("订单不存在");
        return *o;
    }

// ============================ 下单 ============================

    Order& DeliveryService::placeOrder(Id customerId, Id merchantId, const Cart& cart,
                                       std::string address, std::string note) {
        auto& who = customer(customerId);
        auto& mch = shop(merchantId);

        if (cart.empty())   fail("购物车是空的");
        if (!mch.accepting) fail(mch.name + " 现在休息中，换一家吧");

        // 先全量校验，再统一扣库存 —— 扣到一半抛异常会留下脏数据
        for (const auto& line : cart.lines()) {
            const Dish* d = mch.findDish(line.dishId);
            if (!d)              fail("「" + line.name + "」已经从菜单上下去了");
            if (!d->available()) fail("「" + d->name + "」" + d->stockText() + "，下不了单");
            if (d->stock >= 0 && d->stock < line.qty) {
                fail("「" + d->name + "」" + d->stockText()
                     + "，不够你要的 " + std::to_string(line.qty) + " 份");
            }
        }

        const PriceBreakdown price = quote(cart);
        if (who.balance < price.total()) {
            fail("余额不够，还差 " + toString(price.total() - who.balance) + "，先去充点钱");
        }

        for (const auto& line : cart.lines()) {
            if (Dish* d = mch.findDish(line.dishId); d && d->stock >= 0) {
                d->stock -= line.qty;      // 下单即锁库存，取消 / 拒单时再还回去
            }
        }

        Order o;
        o.customerId   = who.id;
        o.customerName = who.name;
        o.address      = address.empty() ? who.building : std::move(address);
        o.merchantId   = mch.id;
        o.merchantName = mch.name;
        o.items.reserve(cart.lines().size());
        for (const auto& line : cart.lines()) {
            o.items.push_back(OrderItem{ .dishId = line.dishId, .name = line.name,
                    .unitPrice = line.unitPrice, .qty = line.qty });
        }
        o.goodsTotal  = price.goods;
        o.packFee     = price.packFee;
        o.deliveryFee = price.deliveryFee;
        o.discount    = price.discount;
        o.total       = price.total();
        o.note        = std::move(note);
        o.createdAt   = std::chrono::system_clock::now();

        Order& saved = store_.orders.add(std::move(o));
        saved.push(OrderStatus::Placed, "学生");

        who.balance -= saved.total;   // 校园卡先扣款，取消 / 拒单时退回
        notifyChanged();
        return saved;
    }

    std::vector<Order*> DeliveryService::myOrders(Id customerId) {
        auto list = store_.orders.where(
                [customerId](const Order& o) { return o.customerId == customerId; });
        std::sort(list.begin(), list.end(),
                  [](const Order* a, const Order* b) { return a->id > b->id; });  // 新单在前
        return list;
    }

    void DeliveryService::cancelOrder(Id orderId) {
        Order& o = order(orderId);

        // 能不能取消，只由 canTransit 说了算。这里以前手写「状态是 Placed 或 Accepted」，
        // 等于把流转规则抄了第二份 —— 今天两份恰好一样，改状态表那天就不一定了。
        //（Order.h 开头写着：状态怎么流转只由 canTransit 说了算，别的层不许自己判。）
        if (!canTransit(o.status, OrderStatus::Canceled)) {
            fail("订单已经「" + std::string(statusName(o.status)) + "」了，取消不了");
        }

        restoreStock(o);
        customer(o.customerId).balance += o.total;   // 全额退
        o.push(OrderStatus::Canceled, "学生");
        o.closedAt = std::chrono::system_clock::now();
        notifyChanged();
    }

    void DeliveryService::confirmReceived(Id orderId) {
        Order& o = order(orderId);

        // 这是「配送中」订单的第二条收尾路：骑手忘了点送达时，学生自己确认。
        // 所以它不是死代码 —— 只要单子还停在 Delivering，学生这边随时能收尾；
        // 谁先点谁算，两条路都走 Delivering -> Completed，都归 canTransit 管。
        //
        // 终态先单独说清，别掉到下面那句「还没开始配送呢」上去 ——
        // 对一单「已送达」的订单说这句，纯属误导。
        if (isFinal(o.status)) {
            fail("订单已经「" + std::string(statusName(o.status)) + "」了，不用再确认");
        }
        if (!canTransit(o.status, OrderStatus::Completed)) {
            fail("订单还没开始配送呢");
        }

        o.push(OrderStatus::Completed, "学生确认收货");
        o.closedAt = std::chrono::system_clock::now();
        notifyChanged();
    }

    void DeliveryService::topUp(Id customerId, int amountYuan) {
        if (amountYuan <= 0) fail("充值金额不对");
        customer(customerId).balance += yuan(amountYuan);
        notifyChanged();
    }

// ============================ 商家端 ============================

    std::vector<Order*> DeliveryService::shopOrders(Id merchantId) {
        auto list = store_.orders.where(
                [merchantId](const Order& o) { return o.merchantId == merchantId; });
        std::sort(list.begin(), list.end(), [](const Order* a, const Order* b) {
            if (a->finished() != b->finished()) return !a->finished();  // 没处理完的排前面
            return a->id > b->id;
        });
        return list;
    }

    void DeliveryService::acceptOrder(Id orderId) {
        transit(order(orderId), OrderStatus::Accepted, "商家接单");
    }

    void DeliveryService::finishCooking(Id orderId) {
        transit(order(orderId), OrderStatus::Ready, "商家出餐");
    }

    void DeliveryService::rejectOrder(Id orderId, std::string reason) {
        Order& o = order(orderId);

        // 拒单的实质就是「取消」，合法性跟 cancelOrder 是同一件事，
        // 一样交回 canTransit，别在这儿再手写一遍判断。
        if (!canTransit(o.status, OrderStatus::Canceled)) {
            fail("这单已经「" + std::string(statusName(o.status)) + "」了，没法拒了");
        }

        restoreStock(o);
        customer(o.customerId).balance += o.total;
        if (!reason.empty()) o.note += "  [商家：" + reason + "]";
        o.push(OrderStatus::Canceled, "商家拒单");
        o.closedAt = std::chrono::system_clock::now();
        notifyChanged();
    }

    void DeliveryService::setAccepting(Id merchantId, bool on) {
        shop(merchantId).accepting = on;
        notifyChanged();
    }

    void DeliveryService::updateStock(Id merchantId, Id dishId, int stock) {
        if (stock < -1) fail("库存不能小于 -1（-1 表示不限量）");
        Dish* d = shop(merchantId).findDish(dishId);
        if (!d) fail("没这道菜");
        d->stock = stock;
        notifyChanged();
    }

    void DeliveryService::toggleDish(Id merchantId, Id dishId, bool onSale) {
        Dish* d = shop(merchantId).findDish(dishId);
        if (!d) fail("没这道菜");
        d->onSale = onSale;
        notifyChanged();
    }

    Id DeliveryService::addDish(Id merchantId, Dish d) {
        if (d.name.empty())     fail("菜名不能空着");
        if (d.price.cents <= 0) fail("价格得大于 0");

        auto& menu = shop(merchantId).menu;
        d.id = nextId();
        const Id id = d.id;
        menu.push_back(std::move(d));
        notifyChanged();
        return id;   // 不返回 Dish&：vector 一扩容那个引用就悬空了
    }
    Merchant& DeliveryService::createMerchant(std::string name, std::string location, double rating) {
        if (name.empty()) fail("店名不能空着");

        Merchant m;
        m.name     = std::move(name);
        m.location = location.empty() ? "位置待填" : std::move(location);
        m.rating   = rating;
        // accepting 默认就是 true，新店开门营业，不然学生下单时会被拦下来
        return store_.merchants.add(std::move(m));
    }

// ============================ 骑手端 ============================

    std::vector<Order*> DeliveryService::pickupBoard() {
        auto list = store_.orders.where(
                [](const Order& o) { return o.status == OrderStatus::Ready; });
        std::sort(list.begin(), list.end(),
                  [](const Order* a, const Order* b) { return a->id < b->id; });  // 出餐早的先派
        return list;
    }

    std::vector<Order*> DeliveryService::riderTasks(Id riderId) {
        auto list = store_.orders.where([riderId](const Order& o) {
            return o.riderId.has_value() && *o.riderId == riderId && !o.finished();
        });
        std::sort(list.begin(), list.end(),
                  [](const Order* a, const Order* b) { return a->id < b->id; });
        return list;
    }

    void DeliveryService::grabOrder(Id orderId, Id riderId) {
        Order& o = order(orderId);
        auto& r = rider(riderId);
        if (o.status != OrderStatus::Ready) fail("手慢了，这单已经被抢走了");

        o.riderId   = r.id;
        o.riderName = r.name;
        transit(o, OrderStatus::Delivering, r.name);
    }

    void DeliveryService::markDelivered(Id orderId, Id riderId) {
        Order& o = order(orderId);
        if (!o.riderId || *o.riderId != riderId) fail("这不是你的单");

        // 【修掉一个真 bug】以前这儿直接 o.push(Completed, "骑手")，没走状态机。
        // 判定只剩上面那句「是不是你的单」—— 而重复点送达时它照样成立
        // （骑手字段第一次送达就写好了），于是同一单能被"送达"两遍，时间线里
        // 多出一行重复的 completed。订单 1025 的时间线就是这么长到 6 行的：
        // placed / accepted / ready / delivering / completed / completed。
        // 好在只是时间线难看 —— stats() 是按订单遍历的，钱没被算两次。
        //
        // 现在交回 canTransit：能走到 Completed 的只有 Delivering，
        // 所以第二次调用一定停在这儿。写法和 cancelOrder 一致，单次落盘。
        if (!canTransit(o.status, OrderStatus::Completed)) {
            fail("这单已经「" + std::string(statusName(o.status)) + "」了，不用再送");
        }

        o.push(OrderStatus::Completed, "骑手");
        o.closedAt = std::chrono::system_clock::now();
        notifyChanged();
    }

// ============================ 内部 ============================

    void DeliveryService::transit(Order& o, OrderStatus to, std::string_view actor) {
        if (!canTransit(o.status, to)) {
            fail("订单 #" + std::to_string(o.id) + " 现在是「"
                 + std::string(statusName(o.status)) + "」，不能变成「"
                 + std::string(statusName(to)) + "」");
        }
        o.push(to, actor);
        notifyChanged();
    }

    void DeliveryService::restoreStock(const Order& o) {
        auto* mch = store_.merchants.find(o.merchantId);
        if (!mch) return;
        for (const auto& item : o.items) {
            if (Dish* d = mch->findDish(item.dishId); d && d->stock >= 0) {
                d->stock += item.qty;
            }
        }
    }

    Stats DeliveryService::stats() const {
        Stats s;
        for (const Order* o : std::as_const(store_).orders.all()) {
            ++s.orderCount;
            switch (o->status) {
                case OrderStatus::Completed:
                    ++s.completedCount;
                    s.turnover += o->total;
                    break;
                case OrderStatus::Canceled:
                    break;
                default:
                    ++s.activeCount;
                    break;
            }
        }
        if (s.completedCount > 0) {
            s.avgTicket = Money{ s.turnover.cents / static_cast<long long>(s.completedCount) };
        }
        return s;
    }
    MerchantStats DeliveryService::shopStats(Id merchantId) const {
        MerchantStats s;
        for (const Order* o : std::as_const(store_).orders.all()) {
            if (o->merchantId != merchantId) continue;
            ++s.orderCount;
            switch (o->status) {
                case OrderStatus::Completed: ++s.done;     s.income += o->goodsTotal; break;
                case OrderStatus::Canceled:  ++s.canceled; break;
                default:                     ++s.doing;    break;
            }
        }
        return s;
    }

    RiderStats DeliveryService::riderStats(Id riderId) const {
        RiderStats s;
        for (const Order* o : std::as_const(store_).orders.all()) {
            if (!o->riderId || *o->riderId != riderId) continue;
            if (o->status != OrderStatus::Completed) continue;
            ++s.done;
            s.fee += o->deliveryFee;
        }
        return s;
    }

}  // namespace eats