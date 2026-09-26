#include "Merchant.h"

#include <algorithm>

namespace eats {

    Dish* Merchant::findDish(Id dishId) {
        auto it = std::ranges::find(menu, dishId, &Dish::id);
        return it == menu.end() ? nullptr : &*it;
    }

    const Dish* Merchant::findDish(Id dishId) const {
        auto it = std::ranges::find(menu, dishId, &Dish::id);
        return it == menu.end() ? nullptr : &*it;
    }

    Money Merchant::minPrice() const {
        if (menu.empty()) return Money{};
        Money lowest = menu.front().price;
        for (const auto& d : menu) {
            if (d.price < lowest) lowest = d.price;
        }
        return lowest;
    }

}  // namespace eats