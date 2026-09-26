#include "Views.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "Console.h"
#include "Money.h"
#include "Text.h"
#include "Types.h"

namespace eats::ui {

    void printMerchantList(const std::vector<Merchant*>& shops) {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(shops.size());
        for (std::size_t i = 0; i < shops.size(); ++i) {
            const Merchant& m = *shops[i];
            rows.push_back({
                                   std::to_string(i + 1),
                                   m.name,
                                   m.location,
                                   text::num(m.rating),
                                   m.statusText(),
                                   m.menu.empty() ? "-" : toString(m.minPrice()) + " 起",
                           });
        }
        printTable({"#", "商家", "位置", "评分", "状态", "参考价"},
                   rows, {2, 14, 18, 4, 6, 12});
    }

    void printMenu(const Merchant& shop) {
        std::cout << shop.name << "  " << shop.location
                  << "  评分 " << text::num(shop.rating)
                  << "  " << shop.statusText() << "\n\n";

        std::vector<std::vector<std::string>> rows;
        rows.reserve(shop.menu.size());
        for (std::size_t i = 0; i < shop.menu.size(); ++i) {
            const Dish& d = shop.menu[i];
            rows.push_back({
                                   std::to_string(i + 1),
                                   d.name,
                                   d.category,
                                   toString(d.price),
                                   d.stockText(),
                           });
        }
        printTable({"#", "菜名", "分类", "单价", "库存"}, rows, {2, 18, 6, 9, 8});
    }

    void printOrderTable(const std::vector<Order*>& orders, PartySide side) {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(orders.size());
        for (const Order* o : orders) {
            const std::string& party =
                    (side == PartySide::Merchant) ? o->merchantName : o->customerName;
            rows.push_back({
                                   "#" + std::to_string(o->id),
                                   std::string(statusName(o->status)),
                                   party,
                                   toString(o->total),
                                   std::to_string(o->waitedMinutes()) + " 分",
                                   o->itemsSummary(),
                           });
        }
        const char* partyHeader = (side == PartySide::Merchant) ? "商家" : "学生";
        printTable({"订单号", "状态", partyHeader, "金额", "已等待", "菜品"},
                   rows, {8, 7, 14, 10, 7, 30});
    }

    void printOrderDetail(const Order& o) {
        printLine('=');
        std::cout << "订单 #" << o.id << "    " << statusName(o.status) << '\n';
        std::cout << "商家：" << o.merchantName << "     学生：" << o.customerName << '\n';
        std::cout << "地址：" << o.address;
        if (!o.note.empty()) std::cout << "     备注：" << o.note;
        std::cout << '\n';
        if (!o.riderName.empty()) std::cout << "骑手：" << o.riderName << '\n';
        printLine('-');

        for (const auto& item : o.items) {
            std::cout << "  " << text::pad(item.name, 18) << " x" << item.qty
                      << "   " << text::pad(toString(item.unitPrice), 9)
                      << " = " << toString(item.subtotal()) << '\n';
        }
        printLine('-');

        std::cout << "商品 " << toString(o.goodsTotal)
                  << "   打包 " << toString(o.packFee)
                  << "   配送 " << toString(o.deliveryFee)
                  << "   优惠 -" << toString(o.discount) << '\n';
        std::cout << "实付 " << toString(o.total)
                  << "   共 " << o.totalQty() << " 件\n";

        if (!o.timeline.empty()) {
            printLine('-');
            for (const auto& log : o.timeline) {
                std::cout << "  " << formatTime(log.at, "%m-%d %H:%M")
                          << "  " << text::pad(statusName(log.status), 8)
                          << "  " << log.actor << '\n';
            }
        }
        printLine('=');
    }

    void printPriceBar(const PriceBreakdown& pb) {
        std::cout << "  商品 " << toString(pb.goods)
                  << "   打包 " << toString(pb.packFee)
                  << "   配送 " << toString(pb.deliveryFee)
                  << "   优惠 -" << toString(pb.discount) << '\n';
        std::cout << "  合计 " << toString(pb.total()) << '\n';
    }

}  // namespace eats::ui