// [domain] 订单 + 状态机。状态怎么流转只由 canTransit 说了算，
// 别的层不许自己判状态。
#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Money.h"
#include "Types.h"

namespace eats {

    enum class OrderStatus {
        Placed,      // 待商家接单
        Accepted,    // 已接单，备餐中
        Ready,       // 已出餐，等骑手取
        Delivering,  // 骑手配送中
        Completed,   // 已送达
        Canceled,    // 已取消 / 被拒单
    };

    std::string_view statusName(OrderStatus s);
    std::string_view statusCode(OrderStatus s);
    std::optional<OrderStatus> statusFromCode(std::string_view code);
    bool isFinal(OrderStatus s);
    bool canTransit(OrderStatus from, OrderStatus to);

    struct OrderItem {
        Id          dishId{kNoId};
        std::string name;        // 冗余存一份，菜单改名不影响历史订单
        Money       unitPrice;
        int         qty{1};

        Money subtotal() const { return unitPrice * qty; }
        std::string text() const { return name + " x" + std::to_string(qty); }
    };

    struct StatusLog {
        OrderStatus status{OrderStatus::Placed};
        std::string actor;                                  // 学生 / 商家 / 骑手
        std::chrono::system_clock::time_point at{};
    };

    struct Order {
        Id          id{kNoId};
        Id          customerId{kNoId};
        std::string customerName;
        std::string address;

        Id          merchantId{kNoId};
        std::string merchantName;

        std::vector<OrderItem> items;
        Money goodsTotal, packFee, deliveryFee, discount, total;

        std::string note;
        OrderStatus status{OrderStatus::Placed};
        std::optional<Id> riderId;
        std::string riderName;

        std::chrono::system_clock::time_point createdAt{};
        std::optional<std::chrono::system_clock::time_point> closedAt;

        std::vector<StatusLog> timeline;

        bool finished() const { return isFinal(status); }
        int  totalQty() const;
        long long waitedMinutes() const;
        std::string itemsSummary() const;

        // 改状态 + 记一笔时间线。只允许 service 层调用，
        // 因为合法性检查在 canTransit 里，别绕过。
        void push(OrderStatus s, std::string_view actor);
    };

}  // namespace eats