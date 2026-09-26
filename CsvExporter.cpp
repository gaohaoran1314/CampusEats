#include "CsvExporter.h"

#include <chrono>
#include <fstream>
#include <string>

#include "Text.h"
#include "Types.h"

namespace eats::csv {
    namespace {

// 字段里有逗号或引号时，按 CSV 规矩套引号
        std::string esc(const std::string& s) {
            if (s.find_first_of(",\"\n") == std::string::npos) return s;
            std::string out = "\"";
            for (const char c : s) {
                if (c == '"') out += '"';   // 引号自身要翻倍
                out += c;
            }
            out += "\"";
            return out;
        }

        std::string amount(Money m) {
            return text::num(static_cast<double>(m.cents) / 100.0, 2);
        }

    }  // namespace

    std::filesystem::path exportOrders(const std::vector<Order*>& orders,
                                       const std::filesystem::path& dir) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);   // 已存在不算错

        const std::filesystem::path path =
                dir / ("orders_" + formatTime(std::chrono::system_clock::now(), "%Y%m%d_%H%M%S") + ".csv");

        std::ofstream fout(path, std::ios::binary);
        if (!fout) return {};

        fout << "\xEF\xBB\xBF";   // UTF-8 BOM
        fout << "订单号,下单时间,状态,商家,学生,收货地址,菜品,商品金额,打包费,配送费,优惠,实付\n";

        for (const Order* o : orders) {
            fout << o->id << ','
                 << formatTime(o->createdAt, "%Y-%m-%d %H:%M:%S") << ','
                 << statusName(o->status) << ','
                 << esc(o->merchantName) << ','
                 << esc(o->customerName) << ','
                 << esc(o->address) << ','
                 << esc(o->itemsSummary()) << ','
                 << amount(o->goodsTotal) << ','
                 << amount(o->packFee) << ','
                 << amount(o->deliveryFee) << ','
                 << amount(o->discount) << ','
                 << amount(o->total) << '\n';
        }
        return path;
    }

}  // namespace eats::csv