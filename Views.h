// [ui] 展示层：把领域对象打成表 / 详情。
// 只读数据，不改任何状态 —— 改状态一律走 DeliveryService。
#pragma once
#include <vector>

#include "Merchant.h"
#include "Order.h"
#include "Pricing.h"   // PriceBreakdown 的定义在这儿

namespace eats::ui {

// 订单表里「对方」那一列显示谁：学生看商家，商家看学生
    enum class PartySide { Merchant, Customer };

    void printMerchantList(const std::vector<Merchant*>& shops);
    void printMenu(const Merchant& shop);
    void printOrderTable(const std::vector<Order*>& orders, PartySide side);
    void printOrderDetail(const Order& o);
    void printPriceBar(const PriceBreakdown& pb);

}  // namespace eats::ui