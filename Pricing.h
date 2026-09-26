// [service] 计费规则全集中在这儿。以后加优惠券 / 会员日 / 峰谷配送费，
// 只改这个文件和它的 .cpp，UI 和别的 service 都不用动。
#pragma once
#include "Cart.h"
#include "Money.h"

namespace eats {

    struct PriceBreakdown {
        Money goods;
        Money packFee;
        Money deliveryFee;
        Money discount;

        Money total() const { return goods + packFee + deliveryFee - discount; }
    };

    namespace pricing {

        inline constexpr Money kPackFee{100};            // 打包费 1.00 元（单位：分）
        inline constexpr Money kDeliveryFee{200};        // 校内配送费 2.00 元
        inline constexpr Money kFreeDeliveryFrom{3000};  // 满 30 元免配送费
        inline constexpr Money kFullCutFrom{2000};       // 满 20 元
        inline constexpr Money kFullCut{300};            // 减 3 元

    }  // namespace pricing

    PriceBreakdown quote(const Cart& cart);

}  // namespace eats