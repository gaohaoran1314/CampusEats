// [domain] 一道菜。纯数据 + 自己的一点点状态判断，不依赖任何上层。
#pragma once
#include <string>

#include "Money.h"
#include "Types.h"

namespace eats {

    struct Dish {
        Id          id{kNoId};
        std::string name;
        std::string category;   // 主食 / 套餐 / 小吃 / 饮品
        Money       price;
        int         stock{-1};  // -1 = 不限量
        bool        onSale{true};

        bool soldOut() const { return stock == 0; }
        bool available() const { return onSale && stock != 0; }

        std::string stockText() const {
            if (!onSale)    return "已下架";
            if (stock < 0)  return "不限量";
            if (stock == 0) return "已售罄";
            return "剩 " + std::to_string(stock) + " 份";
        }
    };

}  // namespace eats