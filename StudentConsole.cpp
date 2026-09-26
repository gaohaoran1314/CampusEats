#include "StudentConsole.h"

#include <cstddef>
#include <iostream>
#include <iterator>    // std::ssize
#include <sstream>
#include <string>

#include "Cart.h"
#include "Console.h"
#include "Errors.h"
#include "Pricing.h"
#include "Text.h"
#include "Views.h"

namespace eats::ui {
    namespace {

// 结算。只看 cart 里的菜和它自己记着的商家，不需要调用方额外传 shop，
// 所以「逛店加菜」和「直接开购物车」两条路都能复用它。
        void checkout(DeliveryService& svc, Id customerId, Cart& cart) {
            if (cart.empty()) { warn("购物车是空的"); return; }

            const Merchant& shop = svc.shop(cart.merchantId());
            const PriceBreakdown pb = quote(cart);

            clearScreen();
            std::cout << "—— 确认订单 ——\n\n";
            std::cout << shop.name << "  " << shop.location << "\n\n";
            for (const auto& line : cart.lines()) {
                std::cout << "  " << text::pad(line.name, 18) << " x" << line.qty
                          << "  " << toString(line.subtotal()) << '\n';
            }
            std::cout << '\n';
            printPriceBar(pb);

            const Customer& me = svc.customer(customerId);
            if (!askYesNo("\n确认下单吗（校园卡余额 " + toString(me.balance) + "）")) return;

            const std::string addr = ask("送到哪儿", me.building);
            const std::string note = ask("备注（可空）");

            const Order& o = svc.placeOrder(customerId, shop.id, cart, addr, note);
            cart.clear();

            ok("下单成功！订单号 #" + std::to_string(o.id) + "，实付 " + toString(o.total));
            info("等商家接单，可以到「我的订单」看进度");
            pause();
        }

// 不经过商家列表，直接把车调出来看 / 结算
        void showCart(DeliveryService& svc, Id customerId, Cart& cart) {
            clearScreen();
            if (cart.empty()) {
                info("购物车是空的");
                pause();
                return;
            }

            const Merchant& shop = svc.shop(cart.merchantId());

            std::cout << "—— 我的购物车 ——\n\n";
            std::cout << shop.name << "  " << shop.location << "\n\n";
            for (const auto& line : cart.lines()) {
                std::cout << "  " << text::pad(line.name, 18) << " x" << line.qty
                          << "  " << toString(line.subtotal()) << '\n';
            }
            std::cout << '\n';
            printPriceBar(quote(cart));
            std::cout << "\n    1  去结算\n";
            std::cout << "    2  清空购物车\n";
            std::cout << "    0  返回\n\n";

            switch (askInt("请选择", 0, 2)) {
                case 1:
                    checkout(svc, customerId, cart);
                    break;
                case 2:
                    if (askYesNo("清空购物车？")) {
                        cart.clear();
                        ok("已清空");
                        pause();
                    }
                    break;
                default:
                    break;
            }
        }

// 逛店 + 加菜 + 结算。cart 是引用传进来的：中途返回、或者
// placeOrder 抛异常，车里的菜都还留着，下次进来接着点。
        void shopAndOrder(DeliveryService& svc, Id customerId, Cart& cart) {
            clearScreen();
            const std::string keyword = ask("搜什么（直接回车看全部）");

            auto list = svc.shops(keyword);
            if (list.empty()) {
                warn("没找到相关商家");
                pause();
                return;
            }

            printMerchantList(list);
            std::cout << '\n';

            const long long pick = askInt("选商家编号（0 = 返回）", 0, std::ssize(list));
            if (pick == 0) return;

            Merchant& shop = *list[static_cast<std::size_t>(pick - 1)];

            // 车上还有别家的东西？问一句再清
            if (cart.conflictsWith(shop.id)) {
                if (!askYesNo("购物车里还有别家的菜，清空后点这家吗")) return;
                cart.clear();
            }

            for (;;) {
                clearScreen();
                printMenu(shop);
                if (!cart.empty()) {
                    std::cout << "\n—— 购物车 ——\n";
                    for (const auto& line : cart.lines()) {
                        std::cout << "  " << text::pad(line.name, 18) << " x" << line.qty
                                  << "  " << toString(line.subtotal()) << '\n';
                    }
                    printPriceBar(quote(cart));
                }
                std::cout << "\n输入 <菜号> <份数> 加菜；d <菜号> <份数> 改份数（0 = 删）；"
                             "c 结算；q 返回\n";

                const std::string cmd = ask("指令");
                if (cmd.empty() || cmd == "q" || cmd == "Q") return;
                if (cmd == "c" || cmd == "C") {
                    if (cart.empty()) { warn("购物车空的，先加点菜"); continue; }
                    break;
                }

                std::istringstream iss(cmd);
                std::string a, b, c;
                iss >> a >> b >> c;

                if (a == "d" || a == "D") {
                    const auto idx = parseInt(b);
                    const auto qty = parseInt(c);
                    if (!idx || !qty || *idx < 1 || *idx > std::ssize(shop.menu)) {
                        warn("格式：d 菜号 份数（份数写 0 就是删掉）");
                        continue;
                    }
                    const Dish& d = shop.menu[static_cast<std::size_t>(*idx - 1)];
                    if (*qty == 0) { cart.remove(d.id); ok("已删除 " + d.name); }
                    else           { cart.setQty(d.id, *qty); ok("已改成 " + std::to_string(*qty) + " 份"); }
                    continue;
                }

                const auto idx = parseInt(a);
                if (!idx || *idx < 1 || *idx > std::ssize(shop.menu)) {
                    warn("看不懂，输菜号加菜，或者用 d / c / q");
                    continue;
                }
                const int qty = parseInt(b).value_or(1);
                const Dish& d = shop.menu[static_cast<std::size_t>(*idx - 1)];
                cart.add(shop.id, d, qty);
                ok("已加入：" + d.name + " x" + std::to_string(qty));
            }

            checkout(svc, customerId, cart);
        }

        void myOrders(DeliveryService& svc, Id customerId) {
            for (;;) {
                clearScreen();
                auto list = svc.myOrders(customerId);
                if (list.empty()) {
                    info("还没有订单");
                    pause();
                    return;
                }
                std::cout << "—— 我的订单 ——\n\n";
                printOrderTable(list, PartySide::Merchant);
                std::cout << '\n';

                const auto id = static_cast<Id>(askInt("输订单号看详情（0 = 返回）", 0, 9999999));
                if (id == 0) return;

                Order& o = svc.order(id);
                if (o.customerId != customerId) { warn("这不是你的订单"); pause(); continue; }

                clearScreen();
                printOrderDetail(o);
                std::cout << '\n';

                if (o.status == OrderStatus::Placed || o.status == OrderStatus::Accepted) {
                    if (askYesNo("要取消这个订单吗")) {
                        svc.cancelOrder(id);
                        ok("已取消，钱退回校园卡");
                        pause();
                    }
                } else if (o.status == OrderStatus::Delivering) {
                    if (askYesNo("已经拿到餐了？")) {
                        svc.confirmReceived(id);
                        ok("收到，感谢使用");
                        pause();
                    }
                } else {
                    pause();
                }
            }
        }

    }  // namespace

    void runStudentConsole(DeliveryService& svc, Id customerId) {
        Cart cart;

        for (;;) {
            const Customer& me = svc.customer(customerId);
            clearScreen();
            std::cout << "【学生端】" << me.name << "（" << me.studentNo << "）"
                      << "   校园卡 " << toString(me.balance) << "\n\n";

            // 菜单上直接写出件数，不用打开就能知道车里有没有东西
            const std::string cartLabel = cart.empty()
                                          ? std::string("购物车")
                                          : "购物车（" + std::to_string(cart.totalQty()) + " 件）";

            std::cout << "    1  逛食堂 / 点单\n";
            std::cout << "    2  我的订单\n";
            std::cout << "    3  充值\n";
            std::cout << "    4  " << cartLabel << "\n";
            std::cout << "    0  返回主菜单\n\n";

            try {
                switch (askInt("请选择", 0, 4)) {
                    case 1:
                        shopAndOrder(svc, customerId, cart);
                        break;
                    case 2:
                        myOrders(svc, customerId);
                        break;
                    case 3: {
                        const int amount = static_cast<int>(askInt("充值金额（元）", 1, 500));
                        svc.topUp(customerId, amount);
                        ok("充值成功，当前余额 " + toString(svc.customer(customerId).balance));
                        pause();
                        break;
                    }
                    case 4:
                        showCart(svc, customerId, cart);
                        break;
                    default:
                        return;
                }
            } catch (const InputClosed&) {
                throw;                     // 输入流断了，别在这儿兜，交给 main
            } catch (const std::exception& e) {
                error(e.what());           // 业务异常只打断这一轮，不退出程序
                pause();
            }
        }
    }

}  // namespace eats::ui