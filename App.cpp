#include "App.h"


#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>    // std::ssize
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "Console.h"
#include "CsvExporter.h"
#include "Errors.h"
#include "MerchantConsole.h"
#include "RiderConsole.h"
#include "SaveFile.h"
#include "SeedData.h"
#include "StudentConsole.h"
#include "Views.h"
#include <chrono>

namespace eats::ui {

    App::App() : svc_(store_) {
        savePath_ = save::defaultSavePath();

        std::error_code ec;
        const bool hasSave = std::filesystem::exists(savePath_, ec) && !ec;

        std::string      err;
        save::LoadReport report;

        if (hasSave && save::loadStore(store_, savePath_, report, err)) {
            dataNote_ = "存档已读取："
                        + std::to_string(report.customers) + " 名学生、"
                        + std::to_string(report.merchants) + " 家商户（含 "
                        + std::to_string(report.dishes) + " 道菜）、"
                        + std::to_string(report.riders) + " 名骑手、"
                        + std::to_string(report.orders) + " 笔订单";
            startupWarnings_ = std::move(report.warnings);
        } else {
            // 读不懂的存档不能就那么摆着 —— 钩子一装，下一次操作就会把新数据盖上去。
            // 先把它改名留一份，免得哪天格式变了、旧版本一跑就把新档冲掉。
            if (hasSave) {
                const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                const std::filesystem::path moved =
                        savePath_.string() + ".bad-" + std::to_string(secs);

                std::error_code mv;
                std::filesystem::rename(savePath_, moved, mv);

                dataNote_ = mv ? "读存档失败（" + err + "），旧文件也没能挪走"
                               : "读存档失败（" + err + "），旧文件已另存为 "
                                 + moved.filename().string();
            } else {
                dataNote_ = "没有存档，已灌入演示数据（退出时会自动生成存档）";
            }
            seedDemoData(store_);   // loadStore 失败时已经把 store 清空了，灌演示数据是安全的
        }

        // 装上钩子：数据一改就落盘。
        // 必须放在 loadStore / seedDemoData 之后 —— 那两句自己也会写数据，
        // 钩子装早了会在"还没有存档"的时候反复触发落盘。
        svc_.onChanged = [this] {
            std::string ignored;
            save::saveStore(store_, savePath_, ignored);   // 这次失败也别吵，退出时还有一次兜底
        };
    }

    App::~App() {
        // 兜底：菜单选 0、输入流断了、中途抛异常，都会走到这里。
        // 析构里不能抛异常，所以 saveStore 本身也不抛，这里再套一层保险。
        try {
            std::string ignored;
            save::saveStore(store_, savePath_, ignored);
        } catch (...) {
        }
    }

    void App::saveNow(bool announce, bool waitForEnter) {
        std::string err;
        if (save::saveStore(store_, savePath_, err)) {
            if (announce) ok("已保存到 " + std::filesystem::absolute(savePath_).string());
        } else {
            warn("保存失败：" + err);
        }
        if (waitForEnter) pause();
    }

    void App::run() {
        bool firstRound = true;

        for (;;) {
            clearScreen();
            std::cout << "==============================================================\n";
            std::cout << "        校园外卖 CampusEats  ·  控制台演示版\n";
            std::cout << "==============================================================\n\n";
            std::cout << "  " << dataNote_ << '\n';
            std::cout << "  存档文件：" << std::filesystem::absolute(savePath_).string() << "\n\n";

            if (firstRound) {
                firstRound = false;
                for (const std::string& w : startupWarnings_) warn(w);
                if (!startupWarnings_.empty()) std::cout << '\n';
            }

            std::cout << "    1  学生点单\n";
            std::cout << "    2  商家后台 / 商家入驻\n";
            std::cout << "    3  骑手接单\n";
            std::cout << "    4  运营报表 / 导出订单\n";
            std::cout << "    5  保存数据\n";
            std::cout << "    0  退出（自动保存）\n\n";

            switch (askInt("请选择", 0, 5)) {
                case 1: studentEntry(); break;
                case 2: merchantEntry(); break;
                case 3: riderEntry(); break;
                case 4: reportEntry(); break;
                case 5: saveNow(true, true); break;
                default:
                    saveNow(true, false);
                    return;
            }
        }
    }

    void App::studentEntry() {
        clearScreen();
        std::cout << "—— 学生登录 ——\n\n";
        const std::string no = ask("学号");

        Customer* c = svc_.findCustomer(no);
        if (!c) {
            std::cout << "\n没查到学号 " << no << "，先注册一个。\n";
            const std::string name = ask("姓名");
            const std::string bld  = ask("宿舍楼（比如 3 号楼 306）");
            c = &svc_.createCustomer(no, name, bld);
            ok("注册成功，送你 200 元校园卡余额");
            pause();
        }
        runStudentConsole(svc_, c->id);
    }

    void App::merchantEntry() {
        clearScreen();
        std::cout << "—— 商家登录 ——\n\n";

        auto list = svc_.shops();
        if (list.empty()) {
            info("还没有商家，先让一家入驻");
        } else {
            printMerchantList(list);
        }
        std::cout << '\n';
        std::cout << "    0  返回\n";
        std::cout << "    " << (std::ssize(list) + 1) << "  新商家入驻\n\n";

        const long long pick = askInt("选择编号", 0, std::ssize(list) + 1);
        if (pick == 0) return;

        if (pick == std::ssize(list) + 1) {
            const std::string name = ask("店名");
            const std::string loc  = ask("位置（比如 四食堂 1 楼 5 号窗口）");
            Merchant& m = svc_.createMerchant(name, loc);
            ok("入驻成功。菜单还是空的，进去加几道菜吧");
            pause();
            runMerchantConsole(svc_, m.id);
            return;
        }

        runMerchantConsole(svc_, list[static_cast<std::size_t>(pick - 1)]->id);
    }

    void App::riderEntry() {
        clearScreen();
        std::cout << "—— 骑手登录 ——\n\n";
        const std::string no = ask("工号");

        Rider* r = svc_.findRider(no);
        if (!r) {
            const std::string name = ask("名字");
            r = &svc_.createRider(no, name);
        }
        runRiderConsole(svc_, r->id);
    }

    void App::reportEntry() {
        clearScreen();
        const Stats s = svc_.stats();
        std::cout << "—— 运营数据 ——\n\n";
        std::cout << "  订单总数   " << s.orderCount << '\n';
        std::cout << "  进行中     " << s.activeCount << '\n';
        std::cout << "  已完成     " << s.completedCount << '\n';
        std::cout << "  成交额     " << toString(s.turnover) << '\n';
        std::cout << "  客单价     " << toString(s.avgTicket) << "\n\n";

        if (askYesNo("要导出全部订单 CSV 吗")) {
            const auto path = csv::exportOrders(store_.orders.all(), save::dataDir() / "exports");
            if (path.empty()) warn("写文件失败，检查一下当前目录能不能写");
            else              ok("已导出：" + std::filesystem::absolute(path).string());
        }
        pause();
    }

}  // namespace eats::ui