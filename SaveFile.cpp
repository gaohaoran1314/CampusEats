#include "SaveFile.h"

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "Order.h"
#include "Text.h"
#include "Types.h"

namespace eats::save {
    namespace {

// ============================ 字段编解码 ============================

// 值里可能出现的特殊字符：竖线是分隔符，换行会把记录切碎，反斜杠是转义符本身
        std::string escape(std::string_view s) {
            std::string out;
            out.reserve(s.size() + 8);
            for (const char c : s) {
                switch (c) {
                    case '\\': out += "\\\\"; break;
                    case '|':  out += "\\p";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    default:   out += c;      break;
                }
            }
            return out;
        }

        std::string unescape(std::string_view s) {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
                switch (s[++i]) {
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 'p':  out += '|';  break;
                    case '\\': out += '\\'; break;
                    default:   out += s[i]; break;   // 不认识的转义原样留着
                }
            }
            return out;
        }

// 因为竖线已经被转义掉了，这里直接按 | 切就行
        std::vector<std::string> splitFields(std::string_view line) {
            std::vector<std::string> out;
            std::size_t start = 0;
            for (;;) {
                const std::size_t bar = line.find('|', start);
                if (bar == std::string_view::npos) {
                    out.push_back(unescape(line.substr(start)));
                    return out;
                }
                out.push_back(unescape(line.substr(start, bar - start)));
                start = bar + 1;
            }
        }

        std::optional<long long> toInt(std::string_view s) {
            if (s.empty()) return std::nullopt;
            long long v = 0;
            const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
            return v;
        }

// 时间戳按「秒」存。system_clock 的 epoch 标准上没保证，
// 但 MSVC / libstdc++ 实际都是 Unix epoch，够用。
        long long toSeconds(std::chrono::system_clock::time_point tp) {
            return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
        }

        std::chrono::system_clock::time_point fromSeconds(long long secs) {
            return std::chrono::system_clock::time_point{ std::chrono::seconds{ secs } };
        }

// ============================ 写：拼一行 ============================

        class Line {
        public:
            explicit Line(std::string_view tag) {
                text_.push_back('[');
                text_.append(tag);
                text_.push_back(']');
            }

            Line& str(std::string_view key, std::string_view value) {
                text_.push_back('|');
                text_ += key;
                text_ += '=';
                text_ += escape(value);
                return *this;
            }

            Line& num(std::string_view key, long long value)  { return str(key, std::to_string(value)); }
            Line& money(std::string_view key, Money m)        { return num(key, m.cents); }
            Line& flag(std::string_view key, bool value)      { return str(key, value ? "1" : "0"); }
            Line& real(std::string_view key, double value)    { return str(key, text::num(value, 2)); }

            const std::string& text() const { return text_; }

        private:
            std::string text_;
        };

// ============================ 读：拆一行 ============================

        class Record {
        public:
            bool parse(std::string_view line) {
                tag_.clear();
                kv_.clear();

                auto fields = splitFields(line);
                if (fields.empty()) return false;
                if (fields[0].size() < 3 || fields[0].front() != '[' || fields[0].back() != ']') return false;
                tag_ = fields[0].substr(1, fields[0].size() - 2);

                for (std::size_t i = 1; i < fields.size(); ++i) {
                    const std::size_t eq = fields[i].find('=');
                    if (eq == std::string::npos) continue;
                    kv_.emplace_back(fields[i].substr(0, eq), fields[i].substr(eq + 1));
                }
                return true;
            }

            std::string_view tag() const { return tag_; }
            bool has(std::string_view key) const { return !str(key).empty(); }

            std::string_view str(std::string_view key, std::string_view def = {}) const {
                for (const auto& [k, v] : kv_) {
                    if (k == key) return v;
                }
                return def;
            }

            long long integer(std::string_view key, long long def = 0) const {
                const auto v = toInt(str(key));
                return v.value_or(def);
            }

            Money money(std::string_view key) const { return Money{ integer(key) }; }

            double real(std::string_view key, double def = 0.0) const {
                const std::string raw(str(key));
                if (raw.empty()) return def;
                char* end = nullptr;
                const double v = std::strtod(raw.c_str(), &end);
                if (end == raw.c_str() || *end != '\0') return def;
                return v;
            }

            bool flag(std::string_view key, bool def) const {
                const std::string_view raw = str(key);
                if (raw.empty()) return def;
                return raw == "1" || raw == "true";
            }

        private:
            std::string tag_;
            std::vector<std::pair<std::string, std::string>> kv_;
        };

    }  // namespace

    std::filesystem::path dataDir() {
#ifdef CAMPUS_EATS_DATA_DIR
        return CAMPUS_EATS_DATA_DIR;
#else
        return std::filesystem::current_path();   // 宏没定义就退回工作目录
#endif
    }

    std::filesystem::path defaultSavePath() {
        if (const char* env = std::getenv("CAMPUS_EATS_SAVE"); env && *env) {
            return std::filesystem::path(env);
        }
        return dataDir() / "campus-save.txt";
    }

// ============================ 存 ============================

    bool saveStore(const Store& store, const std::filesystem::path& path, std::string& err) {
        err.clear();

        std::error_code ec;
        if (const auto parent = path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }

        const std::filesystem::path tmp = path.string() + ".tmp";
        std::ofstream fout(tmp, std::ios::binary | std::ios::trunc);
        if (!fout) {
            err = "打不开临时文件 " + tmp.string();
            return false;
        }

        fout << "#CampusEats save " << kFormatVersion << '\n';

        for (const Customer* c : store.customers.all()) {
            Line line("customer");
            line.num("id", c->id).str("no", c->studentNo).str("name", c->name)
                    .str("building", c->building).money("balance", c->balance);
            fout << line.text() << '\n';
        }

        // 菜单跟着商家走：先写商家，紧接着写它的菜，人看文件时也直观
        for (const Merchant* m : store.merchants.all()) {
            Line line("merchant");
            line.num("id", m->id).str("name", m->name).str("location", m->location)
                    .real("rating", m->rating).flag("accepting", m->accepting);
            fout << line.text() << '\n';

            for (const Dish& d : m->menu) {
                Line dish("dish");
                dish.num("mid", m->id).num("id", d.id).str("name", d.name)
                        .str("cat", d.category).money("price", d.price)
                        .num("stock", d.stock).flag("on", d.onSale);
                fout << dish.text() << '\n';
            }
        }

        for (const Rider* r : store.riders.all()) {
            Line line("rider");
            line.num("id", r->id).str("no", r->staffNo).str("name", r->name);
            fout << line.text() << '\n';
        }

        for (const Order* o : store.orders.all()) {
            Line line("order");
            line.num("id", o->id)
                    .num("cid", o->customerId).str("cname", o->customerName).str("addr", o->address)
                    .num("mid", o->merchantId).str("mname", o->merchantName)
                    .money("goods", o->goodsTotal).money("pack", o->packFee)
                    .money("deliv", o->deliveryFee).money("disc", o->discount).money("total", o->total)
                    .str("note", o->note)
                    .str("status", statusCode(o->status))
                    .num("created", toSeconds(o->createdAt));
            if (o->riderId)            line.num("rider", *o->riderId);
            if (!o->riderName.empty()) line.str("rname", o->riderName);
            if (o->closedAt)           line.num("closed", toSeconds(*o->closedAt));
            fout << line.text() << '\n';

            for (const OrderItem& item : o->items) {
                Line row("item");
                row.num("oid", o->id).num("did", item.dishId).str("name", item.name)
                        .money("price", item.unitPrice).num("qty", item.qty);
                fout << row.text() << '\n';
            }

            for (const StatusLog& log : o->timeline) {
                Line row("log");
                row.num("oid", o->id).str("status", statusCode(log.status))
                        .str("actor", log.actor).num("at", toSeconds(log.at));
                fout << row.text() << '\n';
            }
        }

        fout.flush();
        const bool wrote = fout.good();
        fout.close();

        if (!wrote) {
            err = "写 " + tmp.string() + " 的时候出错了（磁盘满了或者没权限）";
            std::filesystem::remove(tmp, ec);
            return false;
        }

        // 替换顺序有讲究。原来的写法是「先删旧档，再改名」，中间任何一步失败
        // 都是两头落空：旧的没了，新的也没到位。改成先把旧档挪成 .bak，
        // 再改名；万一改名失败，还能把备份挪回来。
        //
        // 另外别指望 rename 能覆盖同名文件 —— POSIX 和 Windows 的语义在这里
        // 不一致，MinGW 就曾经是「目标存在则直接失败」。
        // 下面的顺序保证改名时目标一定不存在，绕开这个坑。
        const std::filesystem::path backup = path.string() + ".bak";
        bool hasBackup = false;

        if (std::filesystem::exists(path, ec)) {
            ec.clear();
            std::filesystem::remove(backup, ec);   // 上一轮的备份，丢了不心疼
            ec.clear();
            std::filesystem::rename(path, backup, ec);
            hasBackup = !ec;
            ec.clear();
        }

        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            err = "改名到 " + path.string() + " 失败：" + ec.message();
            if (hasBackup) {
                std::error_code undo;
                std::filesystem::rename(backup, path, undo);   // 把旧档挪回来
            }
            return false;
        }
        return true;
    }

// ============================ 读 ============================

    bool loadStore(Store& store, const std::filesystem::path& path,
                   LoadReport& report, std::string& err) {
        report = LoadReport{};
        err.clear();

        // 任何一条失败路径都要把 store 清干净，
        // 免得调用方拿到半截数据再往上面灌演示数据
        const auto giveUp = [&](std::string why) {
            store = Store{};
            err = std::move(why);
            return false;
        };

        std::ifstream fin(path, std::ios::binary);
        if (!fin) return giveUp("打不开文件 " + path.string());

        std::string header;
        if (!std::getline(fin, header)) return giveUp("存档是空的");
        if (!header.empty() && header.back() == '\r') header.pop_back();

        {
            constexpr std::string_view kMagic = "#CampusEats save ";
            if (header.rfind(kMagic, 0) != 0) return giveUp("这不像 CampusEats 的存档（缺文件头）");

            const auto version = toInt(std::string_view(header).substr(kMagic.size()));
            if (!version) return giveUp("文件头里的版本号读不出来");
            if (*version != kFormatVersion) {
                return giveUp("存档版本是 " + std::to_string(*version)
                              + "，本程序只认 " + std::to_string(kFormatVersion));
            }
        }

        std::vector<std::string> lines;
        std::string raw;
        while (std::getline(fin, raw)) {
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            if (raw.empty() || raw.front() == '#') continue;
            lines.push_back(raw);
        }

        // 每段扫一遍全文件。记录不多，这样写最省心：
        // 就算文件里 [dish] 排在它的 [merchant] 前面也能正确挂上。
        const auto forEach = [&lines](std::string_view want, auto&& fn) {
            Record rec;
            for (const std::string& line : lines) {
                if (!rec.parse(line)) continue;
                if (rec.tag() != want) continue;
                fn(rec);
            }
        };

        forEach("customer", [&](const Record& r) {
            Customer c;
            c.id        = static_cast<Id>(r.integer("id"));
            c.studentNo = std::string(r.str("no"));
            c.name      = std::string(r.str("name"));
            c.building  = std::string(r.str("building"));
            c.balance   = r.money("balance");

            if (c.id == kNoId || c.studentNo.empty()) {
                report.warnings.push_back("跳过一条学生记录：缺 id 或学号");
                return;
            }
            if (store.customers.contains(c.id)) {
                report.warnings.push_back("学号 " + c.studentNo + " 的 id 重复，跳过");
                return;
            }
            advanceIdTo(c.id);
            store.customers.add(std::move(c));
            ++report.customers;
        });

        forEach("rider", [&](const Record& r) {
            Rider man;
            man.id      = static_cast<Id>(r.integer("id"));
            man.staffNo = std::string(r.str("no"));
            man.name    = std::string(r.str("name"));

            if (man.id == kNoId || man.staffNo.empty()) {
                report.warnings.push_back("跳过一条骑手记录：缺 id 或工号");
                return;
            }
            if (store.riders.contains(man.id)) return;
            advanceIdTo(man.id);
            store.riders.add(std::move(man));
            ++report.riders;
        });

        forEach("merchant", [&](const Record& r) {
            Merchant m;
            m.id        = static_cast<Id>(r.integer("id"));
            m.name      = std::string(r.str("name"));
            m.location  = std::string(r.str("location"));
            m.rating    = r.real("rating", 5.0);
            m.accepting = r.flag("accepting", true);

            if (m.id == kNoId || m.name.empty()) {
                report.warnings.push_back("跳过一条商家记录：缺 id 或店名");
                return;
            }
            if (store.merchants.contains(m.id)) return;
            advanceIdTo(m.id);
            store.merchants.add(std::move(m));
            ++report.merchants;
        });

        forEach("dish", [&](const Record& r) {
            Merchant* m = store.merchants.find(static_cast<Id>(r.integer("mid")));
            if (!m) {
                report.warnings.push_back("菜品「" + std::string(r.str("name"))
                                          + "」找不到归属商家，跳过");
                return;
            }
            Dish d;
            d.id       = static_cast<Id>(r.integer("id"));
            d.name     = std::string(r.str("name"));
            d.category = std::string(r.str("cat"));
            d.price    = r.money("price");
            d.stock    = static_cast<int>(r.integer("stock", -1));
            d.onSale   = r.flag("on", true);

            if (d.id == kNoId) {
                report.warnings.push_back("菜品「" + d.name + "」没有 id，跳过");
                return;
            }
            advanceIdTo(d.id);
            m->menu.push_back(std::move(d));
            ++report.dishes;
        });

        forEach("order", [&](const Record& r) {
            Order o;
            o.id           = static_cast<Id>(r.integer("id"));
            o.customerId   = static_cast<Id>(r.integer("cid"));
            o.customerName = std::string(r.str("cname"));
            o.address      = std::string(r.str("addr"));
            o.merchantId   = static_cast<Id>(r.integer("mid"));
            o.merchantName = std::string(r.str("mname"));
            o.goodsTotal   = r.money("goods");
            o.packFee      = r.money("pack");
            o.deliveryFee  = r.money("deliv");
            o.discount     = r.money("disc");
            o.total        = r.money("total");
            o.note         = std::string(r.str("note"));
            o.createdAt    = fromSeconds(r.integer("created"));

            const auto status = statusFromCode(r.str("status"));
            if (!status) {
                report.warnings.push_back("订单 #" + std::to_string(o.id)
                                          + " 的状态认不出来，整单跳过");
                return;
            }
            o.status = *status;

            if (r.has("rider")) o.riderId = static_cast<Id>(r.integer("rider"));
            o.riderName = std::string(r.str("rname"));
            if (r.has("closed")) o.closedAt = fromSeconds(r.integer("closed"));

            if (o.id == kNoId) {
                report.warnings.push_back("跳过一条没有 id 的订单");
                return;
            }
            if (store.orders.contains(o.id)) return;
            advanceIdTo(o.id);
            store.orders.add(std::move(o));
            ++report.orders;
        });

        forEach("item", [&](const Record& r) {
            Order* o = store.orders.find(static_cast<Id>(r.integer("oid")));
            if (!o) return;   // 订单都被跳过了，明细也就没地方挂

            OrderItem item;
            item.dishId    = static_cast<Id>(r.integer("did"));
            item.name      = std::string(r.str("name"));
            item.unitPrice = r.money("price");
            item.qty       = static_cast<int>(r.integer("qty", 1));
            o->items.push_back(std::move(item));
        });

        forEach("log", [&](const Record& r) {
            Order* o = store.orders.find(static_cast<Id>(r.integer("oid")));
            if (!o) return;

            const auto status = statusFromCode(r.str("status"));
            if (!status) return;

            o->timeline.push_back(StatusLog{ .status = *status,
                    .actor = std::string(r.str("actor")),
                    .at = fromSeconds(r.integer("at")) });
        });

        if (report.customers == 0 && report.merchants == 0
            && report.riders == 0 && report.orders == 0) {
            return giveUp("存档里没有一条能用的记录");
        }
        return true;
    }

}  // namespace eats::save