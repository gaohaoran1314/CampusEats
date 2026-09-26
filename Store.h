// [store] 所有「表」都挂这儿。以后换 SQLite 只换这一层，
// service 基本不用动。
#pragma once
#include "Merchant.h"
#include "Order.h"
#include "Repo.h"
#include "Users.h"

namespace eats {

    struct Store {
        Repo<Customer> customers;
        Repo<Merchant> merchants;
        Repo<Rider>    riders;
        Repo<Order>    orders;
    };

}  // namespace eats