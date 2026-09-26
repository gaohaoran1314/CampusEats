// [service] 业务规则的唯一入口。UI 只负责收集输入和打印结果，
// 不写任何判断。
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include "Cart.h"
#include "Dish.h"
#include "Money.h"
#include "Store.h"

namespace eats {

    struct Stats {
        std::size_t orderCount{0};
        std::size_t activeCount{0};
        std::size_t completedCount{0};
        Money turnover;    // 已完成订单的成交额
        Money avgTicket;   // 客单价
    };
    // 商家看板的聚合数据。这些数原先是在 MerchantConsole 里现场遍历订单算的，
// 挪上来是因为它跟界面无关 —— 控制台、网页、以后的 GUI 用的是同一份规则。
    struct MerchantStats {
        std::size_t orderCount{0};
        std::size_t done{0};        // 已完成
        std::size_t doing{0};       // 进行中（含待接单、备餐中、待取餐、配送中）
        std::size_t canceled{0};    // 已取消 / 被拒单
        Money income;               // 已完成订单的菜品收入，不含配送费
    };

    struct RiderStats {
        std::size_t done{0};        // 已送达单量
        Money fee;                  // 这些单的配送费
    };

    class DeliveryService {
    public:
        explicit DeliveryService(Store& store) : store_(store) {}

        Store& store() { return store_; }
        // 数据被改动后的通知钩子。装上它之后由持有 Store 的一方决定怎么落盘。
        // 不装就什么都不发生 —— 将来写单元测试时不需要真的写盘。
        std::function<void()> onChanged;

        // ---------------- 账号 ----------------
        Customer* findCustomer(std::string_view studentNo);
        Customer& createCustomer(std::string_view studentNo, std::string_view name,
                                 std::string_view building);
        Customer& customer(Id id);

        Rider* findRider(std::string_view staffNo);
        Rider& createRider(std::string_view staffNo, std::string_view name);
        Rider& rider(Id id);

        // ---------------- 浏览 ----------------
        std::vector<Merchant*> shops(std::string_view keyword = {});
        Merchant& shop(Id id);

        // ---------------- 学生端 ----------------
        Order& placeOrder(Id customerId, Id merchantId, const Cart& cart,
                          std::string address, std::string note);
        std::vector<Order*> myOrders(Id customerId);
        void cancelOrder(Id orderId);
        void confirmReceived(Id orderId);
        void topUp(Id customerId, int amountYuan);

        // ---------------- 商家端 ----------------
        std::vector<Order*> shopOrders(Id merchantId);
        void acceptOrder(Id orderId);
        void finishCooking(Id orderId);
        void rejectOrder(Id orderId, std::string reason);
        void setAccepting(Id merchantId, bool on);
        void updateStock(Id merchantId, Id dishId, int stock);
        void toggleDish(Id merchantId, Id dishId, bool onSale);
        Id   addDish(Id merchantId, Dish d);   // 返回新菜的 id，不返回引用
        Merchant& createMerchant(std::string name, std::string location, double rating = 5.0);

        // ---------------- 骑手端 ----------------
        std::vector<Order*> pickupBoard();              // 已出餐、还没人接的单
        std::vector<Order*> riderTasks(Id riderId);
        void grabOrder(Id orderId, Id riderId);
        void markDelivered(Id orderId, Id riderId);

        // ---------------- 杂项 ----------------
        Order& order(Id id);
        Stats stats() const;

        MerchantStats shopStats(Id merchantId) const;
        RiderStats    riderStats(Id riderId) const;

    private:
        void transit(Order& o, OrderStatus to, std::string_view actor);
        void notifyChanged() { if (onChanged) onChanged(); }
        void restoreStock(const Order& o);

        Store& store_;
    };

}  // namespace eats