#include "MerchantConsole.h"

#include <cstddef>
#include <iostream>
#include <iterator>    // std::ssize
#include <string>

#include "Console.h"
#include "Errors.h"
#include "Views.h"

namespace eats::ui {
    namespace {

        void handleOrders(DeliveryService& svc, Id merchantId) {
            for (;;) {
                clearScreen();
                auto list = svc.shopOrders(merchantId);
                if (list.empty()) {
                    info("还没有订单");
                    pause();
                    return;
                }

                std::cout << "—— 本店订单 ——\n\n";
                printOrderTable(list, PartySide::Customer);

                // 超时提醒。就几行代码，但对商家体验影响很大
                for (const Order* o : list) {
                    if (o->status == OrderStatus::Placed && o->waitedMinutes() >= 10) {
                        warn("订单 #" + std::to_string(o->id) + " 已经等了 "
                             + std::to_string(o->waitedMinutes()) + " 分钟还没接单");
                    }
                }
                std::cout << '\n';

                const auto id = static_cast<Id>(askInt("输订单号处理（0 = 返回）", 0, 9999999));
                if (id == 0) return;

                Order& o = svc.order(id);
                if (o.merchantId != merchantId) { warn("这不是你家的订单"); pause(); continue; }

                clearScreen();
                printOrderDetail(o);
                std::cout << '\n';

                const std::string cmd = ask("操作：a 接单 / b 出餐 / r 拒单（回车返回）");
                if (cmd == "a" && o.status == OrderStatus::Placed) {
                    svc.acceptOrder(id);
                    ok("已接单，开始备餐");
                } else if (cmd == "b" && o.status == OrderStatus::Accepted) {
                    svc.finishCooking(id);
                    ok("已出餐，等骑手来取");
                } else if (cmd == "r") {
                    svc.rejectOrder(id, ask("拒单原因（可空）"));
                    warn("已拒单，钱已原路退回");
                } else if (!cmd.empty()) {
                    warn("当前状态「" + std::string(statusName(o.status)) + "」，不能这么做");
                }
                pause();
            }
        }

        void manageMenu(DeliveryService& svc, Id merchantId) {
            for (;;) {
                Merchant& shop = svc.shop(merchantId);
                clearScreen();
                printMenu(shop);
                std::cout << "\n    1  改库存 / 上下架\n";
                std::cout << "    2  新增菜品\n";
                std::cout << "    0  返回\n\n";

                const long long op = askInt("请选择", 0, 2);
                if (op == 0) return;

                if (op == 2) {
                    Dish d;
                    d.name     = ask("菜名");
                    d.category = ask("分类", "主食");
                    d.price    = yuan(static_cast<double>(askInt("单价（整数元）", 1, 999)));
                    d.stock    = static_cast<int>(askInt("库存（-1 表示不限量）", -1, 9999));
                    svc.addDish(merchantId, d);
                    ok("已上新：" + d.name);
                    pause();
                    continue;
                }

                const long long idx = askInt("第几个菜（0 = 返回）", 0, std::ssize(shop.menu));
                if (idx == 0) continue;

                Dish& d = shop.menu[static_cast<std::size_t>(idx - 1)];
                std::cout << "\n  " << d.name << "  当前 " << d.stockText()
                          << (d.onSale ? "  在售" : "  已下架") << '\n';
                std::cout << "    1  改库存\n";
                std::cout << (d.onSale ? "    2  下架\n" : "    2  上架\n");
                std::cout << "    0  返回\n\n";

                switch (askInt("请选择", 0, 2)) {
                    case 1: {
                        const int stock = static_cast<int>(askInt("新库存（-1 表示不限量）", -1, 9999));
                        svc.updateStock(merchantId, d.id, stock);
                        ok("库存已更新");
                        break;
                    }
                    case 2:
                        svc.toggleDish(merchantId, d.id, !d.onSale);
                        ok(d.onSale ? "已上架" : "已下架");   // d.onSale 已经是新值
                        break;
                    default:
                        break;
                }
                pause();
            }
        }

        void showShopStats(DeliveryService& svc, Id merchantId) {
            clearScreen();
            const MerchantStats s = svc.shopStats(merchantId);

            std::cout << "—— 经营数据 ——\n\n";
            std::cout << "  订单总数   " << s.orderCount << '\n';
            std::cout << "  已完成     " << s.done << '\n';
            std::cout << "  进行中     " << s.doing << '\n';
            std::cout << "  已取消     " << s.canceled << '\n';
            std::cout << "  菜品收入   " << toString(s.income) << "（不含配送费）\n";
            pause();
        }

    }  // namespace

    void runMerchantConsole(DeliveryService& svc, Id merchantId) {
        for (;;) {
            const Merchant& shop = svc.shop(merchantId);
            clearScreen();
            std::cout << "【商家后台】" << shop.name << "  " << shop.statusText()
                      << "   菜品 " << shop.menu.size() << " 道\n\n";
            std::cout << "    1  订单处理\n";
            std::cout << "    2  菜单管理\n";
            std::cout << "    3  切换营业状态\n";
            std::cout << "    4  经营数据\n";
            std::cout << "    0  返回主菜单\n\n";

            try {
                switch (askInt("请选择", 0, 4)) {
                    case 1: handleOrders(svc, merchantId); break;
                    case 2: manageMenu(svc, merchantId); break;
                    case 3:
                        svc.setAccepting(merchantId, !shop.accepting);
                        // setAccepting 已经改过值了，照新状态报就行
                        ok(shop.accepting ? "现在是营业中" : "现在是休息中");
                        pause();
                        break;
                    case 4: showShopStats(svc, merchantId); break;
                    default: return;
                }
            } catch (const InputClosed&) {
                throw;
            } catch (const std::exception& e) {
                error(e.what());
                pause();
            }
        }
    }

}  // namespace eats::ui