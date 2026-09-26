#include "Pricing.h"

namespace eats {

    PriceBreakdown quote(const Cart& cart) {
        PriceBreakdown pb;
        if (cart.empty()) return pb;

        pb.goods = cart.goodsTotal();
        pb.packFee = pricing::kPackFee;
        if (pb.goods < pricing::kFreeDeliveryFrom) pb.deliveryFee = pricing::kDeliveryFee;
        if (pb.goods >= pricing::kFullCutFrom)     pb.discount    = pricing::kFullCut;

        return pb;
    }

}  // namespace eats