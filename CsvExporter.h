// [service] 订单导出。只依赖 domain 和 core，不碰 UI。
#pragma once
#include <filesystem>
#include <vector>

#include "Order.h"

namespace eats::csv {

// 把订单导成 CSV，返回写出的文件路径；失败返回空 path。
// 带 UTF-8 BOM，否则 Excel 打开中文是乱码。
    std::filesystem::path exportOrders(const std::vector<Order*>& orders,
                                       const std::filesystem::path& dir = "data");

}  // namespace eats::csv