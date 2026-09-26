#include "Order.h"

#include <optional>
#include <string>
#include <string_view>

namespace eats {
    namespace {

        struct StatusInfo {
            OrderStatus      value;
            std::string_view code;    // 存档里写的字符串，改这个会读不了老存档
            std::string_view label;   // 给人看的
        };

// 状态表。加新状态只改这里 + canTransit()。
// 显示名和存档编码都从这张表派生，不会出现"改了显示名、忘了改编码"。
        constexpr StatusInfo kStatuses[] = {
                { OrderStatus::Placed,     "placed",     "待接单" },
                { OrderStatus::Accepted,   "accepted",   "备餐中" },
                { OrderStatus::Ready,      "ready",      "待取餐" },
                { OrderStatus::Delivering, "delivering", "配送中" },
                { OrderStatus::Completed,  "completed",  "已送达" },
                { OrderStatus::Canceled,   "canceled",   "已取消" },
        };

    }  // namespace

    std::string_view statusName(OrderStatus s) {
        for (const auto& info : kStatuses) {
            if (info.value == s) return info.label;
        }
        return "未知";
    }

    std::string_view statusCode(OrderStatus s) {
        for (const auto& info : kStatuses) {
            if (info.value == s) return info.code;
        }
        return "unknown";
    }

    std::optional<OrderStatus> statusFromCode(std::string_view code) {
        for (const auto& info : kStatuses) {
            if (info.code == code) return info.value;
        }
        // 认不出来就返回空。老存档里如果留着新版本写的状态，
        // 宁可让调用方把这条记录跳过，也不猜一个状态出来。
        return std::nullopt;
    }

    bool isFinal(OrderStatus s) {
        return s == OrderStatus::Completed || s == OrderStatus::Canceled;
    }

// 合法流转的唯一定义。跟状态表分开写是有意的：
// 那边是"这个状态叫什么"，这里是"它能变成什么"，
// 两件事的改动频率不一样，混在一起反而容易改漏。
    bool canTransit(OrderStatus from, OrderStatus to) {
        switch (from) {
            case OrderStatus::Placed:
                return to == OrderStatus::Accepted || to == OrderStatus::Canceled;
            case OrderStatus::Accepted:
                return to == OrderStatus::Ready    || to == OrderStatus::Canceled;
            case OrderStatus::Ready:
                return to == OrderStatus::Delivering;
            case OrderStatus::Delivering:
                return to == OrderStatus::Completed;
            default:
                return false;
        }
    }

    int Order::totalQty() const {
        int n = 0;
        for (const auto& item : items) n += item.qty;
        return n;
    }

    long long Order::waitedMinutes() const {
        const auto end = closedAt.value_or(std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::minutes>(end - createdAt).count();
    }

    std::string Order::itemsSummary() const {
        std::string out;
        for (const auto& item : items) {
            if (!out.empty()) out += "、";
            out += item.text();
        }
        return out;
    }

    void Order::push(OrderStatus s, std::string_view actor) {
        status = s;
        timeline.push_back(StatusLog{ .status = s,
                .actor = std::string(actor),
                .at = std::chrono::system_clock::now() });
    }

}  // namespace eats