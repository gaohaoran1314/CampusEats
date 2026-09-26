// [ui] 顶层菜单：选身份 → 进对应控制台。
// 负责两件事：启动时把存档读进来，退出时写回去。
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "DeliveryService.h"
#include "Store.h"

namespace eats::ui {

    class App {
    public:
        App();
        ~App();          // 兜底保存：走哪条路退出都会经过这里
        void run();

    private:
        void studentEntry();
        void merchantEntry();
        void riderEntry();
        void reportEntry();
        void saveNow(bool announce, bool waitForEnter);

        std::filesystem::path    savePath_;
        std::string              dataNote_;        // 主菜单上那一行"存档状态"
        std::vector<std::string> startupWarnings_; // 读存档时跳过的坏记录

        Store           store_;   // 必须先声明：svc_ 要拿它初始化
        DeliveryService svc_;
    };

}  // namespace eats::ui