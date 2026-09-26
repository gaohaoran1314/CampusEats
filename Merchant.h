// [domain] 食堂窗口 / 校内商铺。
#pragma once
#include <string>
#include <vector>

#include "Dish.h"

namespace eats {

    struct Merchant {
        Id          id{kNoId};
        std::string name;
        std::string location;        // "三食堂 2 楼"
        double      rating{5.0};
        bool        accepting{true}; // 打烊或者爆单时关掉
        std::vector<Dish> menu;

        // 返回的指针在 menu 不被 erase 的前提下一直有效
        Dish*       findDish(Id dishId);
        const Dish* findDish(Id dishId) const;

        bool hasDish(Id dishId) const { return findDish(dishId) != nullptr; }
        Money minPrice() const;

        std::string statusText() const { return accepting ? "营业中" : "休息中"; }
    };

}  // namespace eats