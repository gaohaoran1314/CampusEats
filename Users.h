// [domain] 学生和骑手。
#pragma once
#include <string>

#include "Money.h"
#include "Types.h"

namespace eats {

// 学生（下单的人）
    struct Customer {
        Id          id{kNoId};
        std::string studentNo;           // 学号，拿来当登录名
        std::string name;
        std::string building;            // 默认收货楼栋
        Money       balance{yuan(200)};  // 校园卡余额，先白送 200 方便试
    };

// 骑手
    struct Rider {
        Id          id{kNoId};
        std::string staffNo;
        std::string name;
    };

}  // namespace eats