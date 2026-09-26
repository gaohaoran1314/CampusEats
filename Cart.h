// [domain] 购物车。一辆车只装一家的菜 —— 外卖 App 都这规矩，
// 跨店结算太麻烦。
#pragma once
#include <vector>

#include "Dish.h"

namespace eats {

    struct CartLine {
        Id          dishId{kNoId};
        std::string name;
        Money       unitPrice;
        int         qty{1};

        Money subtotal() const { return unitPrice * qty; }
    };

    class Cart {
    public:
        bool empty() const { return lines_.empty(); }
        void clear();

        Id   merchantId() const { return merchantId_; }
        bool conflictsWith(Id mid) const { return !empty() && merchantId_ != mid; }

        const std::vector<CartLine>& lines() const { return lines_; }
        int   totalQty() const;
        Money goodsTotal() const;

        void add(Id merchantId, const Dish& dish, int qty = 1);
        void setQty(Id dishId, int qty);   // qty <= 0 等于删掉
        void remove(Id dishId);

    private:
        Id merchantId_{kNoId};
        std::vector<CartLine> lines_;
    };

}  // namespace eats