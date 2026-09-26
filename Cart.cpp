#include "Cart.h"

#include "Errors.h"

namespace eats {

    void Cart::clear() {
        lines_.clear();
        merchantId_ = kNoId;
    }

    int Cart::totalQty() const {
        int n = 0;
        for (const auto& line : lines_) n += line.qty;
        return n;
    }

    Money Cart::goodsTotal() const {
        Money sum;
        for (const auto& line : lines_) sum += line.subtotal();
        return sum;
    }

    void Cart::add(Id merchantId, const Dish& dish, int qty) {
        if (qty <= 0) return;
        if (!dish.available()) fail("「" + dish.name + "」" + dish.stockText() + "，加不进去");
        if (!empty() && merchantId_ != merchantId) fail("购物车里已经有别家的菜了");
        if (dish.stock >= 0 && qty > dish.stock) {
            fail("「" + dish.name + "」" + dish.stockText() + "，你要不了这么多");
        }

        merchantId_ = merchantId;
        for (auto& line : lines_) {
            if (line.dishId == dish.id) {
                line.qty += qty;
                return;
            }
        }
        lines_.push_back(CartLine{ .dishId = dish.id, .name = dish.name,
                .unitPrice = dish.price, .qty = qty });
    }

    void Cart::setQty(Id dishId, int qty) {
        if (qty <= 0) { remove(dishId); return; }
        for (auto& line : lines_) {
            if (line.dishId == dishId) { line.qty = qty; return; }
        }
    }

    void Cart::remove(Id dishId) {
        std::erase_if(lines_, [dishId](const CartLine& line) { return line.dishId == dishId; });
        if (lines_.empty()) merchantId_ = kNoId;
    }

}  // namespace eats