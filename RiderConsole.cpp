#include "RiderConsole.h"

#include <cstddef>
#include <iostream>
#include <string>

#include "Console.h"
#include "Errors.h"
#include "Money.h"
#include "Views.h"

namespace eats::ui {
    namespace {

        void grabBoard(DeliveryService& svc, Id riderId) {
            clearScreen();
            auto list = svc.pickupBoard();
            if (list.empty()) {
                info("现在没有待取餐的单，等会儿再来");
                pause();
                return;
            }

            std::cout << "—— 待取餐 ——\n\n";
            printOrderTable(list, PartySide::Customer);
            std::cout << '\n';

            const auto id = static_cast<Id>(askInt("抢哪单（0 = 返回）", 0, 9999999));
            if (id == 0) return;

            svc.grabOrder(id, riderId);
            ok("抢到了，快去取餐");
            pause();
        }

        void myTasks(DeliveryService& svc, Id riderId) {
            for (;;) {
                clearScreen();
                auto list = svc.riderTasks(riderId);
                if (list.empty()) {
                    info("手上没有单");
                    pause();
                    return;
                }

                std::cout << "—— 我的配送任务 ——\n\n";
                printOrderTable(list, PartySide::Customer);
                std::cout << '\n';

                const auto id = static_cast<Id>(askInt("输订单号（0 = 返回）", 0, 9999999));
                if (id == 0) return;

                Order& o = svc.order(id);
                if (!o.riderId || *o.riderId != riderId) { warn("不是你的单"); pause(); continue; }

                clearScreen();
                printOrderDetail(o);
                std::cout << '\n';
                if (askYesNo("送到楼下了？确认送达")) {
                    const std::string fee = toString(o.deliveryFee);
                    svc.markDelivered(id, riderId);
                    ok("已送达，这单配送费 " + fee);
                    pause();
                }
            }
        }

        void showIncome(DeliveryService& svc, Id riderId) {
            clearScreen();
            const RiderStats s = svc.riderStats(riderId);

            std::cout << "—— 我的成绩 ——\n\n";
            std::cout << "  已完成单量   " << s.done << '\n';
            std::cout << "  配送费收入   " << toString(s.fee) << '\n';
            pause();
        }

    }  // namespace

    void runRiderConsole(DeliveryService& svc, Id riderId) {
        for (;;) {
            const Rider& me = svc.rider(riderId);
            clearScreen();
            std::cout << "【骑手端】" << me.name << "（工号 " << me.staffNo << "）\n\n";
            std::cout << "    1  待取餐 / 抢单\n";
            std::cout << "    2  我的配送任务\n";
            std::cout << "    3  我的成绩\n";
            std::cout << "    0  返回主菜单\n\n";

            try {
                switch (askInt("请选择", 0, 3)) {
                    case 1: grabBoard(svc, riderId); break;
                    case 2: myTasks(svc, riderId); break;
                    case 3: showIncome(svc, riderId); break;
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