// 校园外卖 · HTTP 服务端
//
// 和控制台版 CampusEats 共用同一份 eats_core，区别只是它不读键盘，
// 而是把 Store 里的数据以 JSON 的形式发出去，再顺带把 www/ 里的网页发出去。
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "Cart.h"
#include "DeliveryService.h"
#include "Dish.h"
#include "Errors.h"
#include "Merchant.h"
#include "Money.h"
#include "Order.h"
#include "Repo.h"
#include "SaveFile.h"
#include "Store.h"
#include "Types.h"
#include "Users.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// windows.h 必须排在 httplib.h 后面：httplib 会先拉进 winsock2.h，
// 反过来的话老的 winsock.h 也会被拽进来，报一屏重定义。
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

    using namespace eats;

    constexpr int kPort = 8080;

// 监听地址。
//
// "127.0.0.1" 只收本机发的包，另一个人拿手机或者另一台笔记本是连不上的；
// "0.0.0.0" 是「这块网卡上所有地址都收」，两台设备才能各用各的。
//
// 【代价说清楚】这批接口没有登录，没有会话，没有 token。听了 0.0.0.0
// 之后，同一个网络里谁能连上这个端口，谁就能下单、充值、抢单、改库存 ——
// 路由上根本不区分身份。宿舍自己的路由器下无所谓；要是这台电脑接的是
// 校园公共 WiFi，同网段的人都能动这份存档。那种场合把这个常量改回
// "127.0.0.1"，代价是只有本机能打开网页。真要做防护，得先有登录态。
    constexpr const char* kBindAddr = "0.0.0.0";

    constexpr const char* kJson = "application/json; charset=utf-8";

// 全服务端共用一把锁。
//
// DeliveryService 是按「一次只有一件事在做」写的（控制台天然如此），
// 而 httplib 是拿线程池接请求的 —— 手快连点两下就有两个 handler 同时在跑。
// 把整个 handler 罩住是最省心的做法：eats_core 一行都不用改。
//
// 【更正】这段注释以前写的是「插一条会触发重新分桶，手里那个指针就野了」。
// 读过 Repo.h 之后发现理由不对：unordered_map 的节点地址是稳定的，
// 插入不会让已有的 T* 失效 —— Repo.h 自己也把这条写进了注释里。
// 真正怕的是它那个 keys_（std::vector<Id>）：Repo::all() / where() 是
// 一边遍历它一边取元素，此时另一个线程 add() 一下就可能让 vector 重新
// 分配，遍历到一半数组搬走 —— 那是未定义行为，可能当场崩。
// 结论没变（只读接口照样上锁），只是理由换成对的这个。
//
// 顺带的好处：一个 handler 从头到尾握着锁，它读到的所有东西都是同一瞬间的，
// 不会出现「余额读到新值、订单列表还是旧的」这种半新半旧。
    std::mutex g_mtx;

// 金额内部用「分」存，这里也照发「分」—— 整数不会漂。
// 另外给一个 text，是给人用浏览器看的时候能一眼认出是多少钱。
    nlohmann::json moneyJson(Money m) {
        return nlohmann::json{{"cents", m.cents}, {"text", toString(m)}};
    }

    nlohmann::json dishJson(const Dish& d) {
        return nlohmann::json{
                {"id",        d.id},
                {"name",      d.name},
                {"category",  d.category},
                {"price",     moneyJson(d.price)},
                {"stock",     d.stock},      // -1 表示不限量，前端别直接拿去做减法
                {"stockText", d.stockText()},
                {"onSale",    d.onSale},
                {"available", d.available()},
        };
    }

    nlohmann::json merchantJson(const Merchant& m) {
        nlohmann::json menu = nlohmann::json::array();
        for (const Dish& d : m.menu)
            menu.push_back(dishJson(d));

        return nlohmann::json{
                {"id",         m.id},
                {"name",       m.name},
                {"location",   m.location},
                {"rating",     m.rating},
                {"accepting",  m.accepting},
                {"statusText", m.statusText()},
                {"dishCount",  m.menu.size()},
                {"menu",       std::move(menu)},
        };
    }

// 学号对外一律叫 studentNo，和 POST /api/orders 的请求字段对上。
// 存档文件里那边是 no=（SaveFile 自己的格式），别被它带偏。
    nlohmann::json customerJson(const Customer& c) {
        return nlohmann::json{
                {"id",        c.id},
                {"studentNo", c.studentNo},
                {"name",      c.name},
                {"building",  c.building},
                {"balance",   moneyJson(c.balance)},
        };
    }

// staffNo 只发着给人看，不做查询键 —— 存档里两个骑手的工号一模一样
// （都是 344886608，/api/riders 的返回和存档原文上都验过），拿它查
// 根本分不出谁是谁。定位骑手一律用 id。
    nlohmann::json riderJson(const Rider& r) {
        return nlohmann::json{
                {"id",      r.id},
                {"staffNo", r.staffNo},
                {"name",    r.name},
        };
    }

// RiderStats: 送完的单量和这些单的配送费。
    nlohmann::json riderStatsJson(const RiderStats& s) {
        return nlohmann::json{
                {"done", s.done},
                {"fee",  moneyJson(s.fee)},
        };
    }

    nlohmann::json orderItemJson(const OrderItem& it) {
        return nlohmann::json{
                {"dishId",    it.dishId},
                {"name",      it.name},
                {"unitPrice", moneyJson(it.unitPrice)},
                {"qty",       it.qty},
                {"subtotal",  moneyJson(it.subtotal())},
        };
    }

// 订单对外就长这样。
//
// status 同时给两份：status 是给程序做判断的稳定码（能喂回 statusFromCode），
// statusText 是给人看的中文。两边都不用去解析对方的字符串。
    nlohmann::json orderJson(const Order& o) {
        nlohmann::json items = nlohmann::json::array();
        for (const OrderItem& it : o.items)
            items.push_back(orderItemJson(it));

        nlohmann::json timeline = nlohmann::json::array();
        for (const StatusLog& e : o.timeline)
            timeline.push_back(nlohmann::json{
                    {"status",     std::string{statusCode(e.status)}},
                    {"statusText", std::string{statusName(e.status)}},
                    {"actor",      e.actor},
                    {"at",         formatTime(e.at)},
            });

        nlohmann::json j = {
                {"id",           o.id},
                {"customerId",   o.customerId},
                {"customerName", o.customerName},
                {"address",      o.address},
                {"merchantId",   o.merchantId},
                {"merchantName", o.merchantName},
                {"items",        std::move(items)},
                {"itemsSummary", o.itemsSummary()},
                {"totalQty",     o.totalQty()},
                {"goodsTotal",   moneyJson(o.goodsTotal)},
                {"packFee",      moneyJson(o.packFee)},
                {"deliveryFee",  moneyJson(o.deliveryFee)},
                {"discount",     moneyJson(o.discount)},
                {"total",        moneyJson(o.total)},
                {"note",         o.note},
                {"status",       std::string{statusCode(o.status)}},
                {"statusText",   std::string{statusName(o.status)}},
                {"finished",     o.finished()},
                {"createdAt",    formatTime(o.createdAt)},
                {"timeline",     std::move(timeline)},
        };

        // 还没人接单的时候骑手字段本来就是空的，那干脆别出现 ——
        // 不然前端拿到 "riderId": 0 还得自己去解释这个 0。
        if (o.riderId)
            j["riderId"] = *o.riderId;
        if (!o.riderName.empty())
            j["riderName"] = o.riderName;
        if (o.closedAt)
            j["closedAt"] = formatTime(*o.closedAt);

        return j;
    }

// 一组订单 -> JSON 数组，顺手排成「新的在前」。
//
// 参数收值不收回引用：里面要排序，排的是自己这份副本，不动调用方那个 vector。
// 排序用 createdAt 降序；时间一样的话（测试里很常见，一分钟内下好几单）
// 拿 id 兜底，保证两次请求给出的顺序一样，不会前端刷新一下就换位。
    nlohmann::json ordersJson(std::vector<Order*> list) {
        // 理论上不会有空指针，但排序要先解引用，先滤一遍保平安
        std::erase_if(list, [](const Order* p) { return p == nullptr; });

        std::sort(list.begin(), list.end(), [](const Order* a, const Order* b) {
            if (a->createdAt != b->createdAt)
                return a->createdAt > b->createdAt;
            return a->id > b->id;
        });

        nlohmann::json arr = nlohmann::json::array();
        for (const Order* o : list)
            arr.push_back(orderJson(*o));
        return arr;
    }

// 商家看板的聚合数字。字段名照抄 DeliveryService.h 里的 MerchantStats，
// 免得以后对不上；income 是「已完成订单的菜品收入，不含配送费」——
// 这条口径已经在 /api/shops/1011/orders 上实测过两次：5400 = 1800 + 3600。
    nlohmann::json merchantStatsJson(const MerchantStats& s) {
        return nlohmann::json{
                {"orderCount", s.orderCount},
                {"done",       s.done},
                {"doing",      s.doing},
                {"canceled",   s.canceled},
                {"income",     moneyJson(s.income)},
        };
    }

// 用 from_chars 而不是 stoull：它不抛异常，正好符合
// 「参数格式不对是客户端的事，不该让服务端炸一下再回 500」。
    std::optional<Id> parseId(std::string_view s) {
        if (s.empty())
            return std::nullopt;

        Id v = kNoId;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || p != s.data() + s.size())
            return std::nullopt;
        return v;
    }

// 请求体里的整数：得是整数，而且不能是负的。
//
// 单独拎出来是因为下面每个字段都要判一遍；摊在主流程里，真正的业务逻辑
// 会被淹在「检查类型」里看不见。
//
// 注意 is_number_integer() 对有符号、无符号都返回 true（JSON 里 1007 这种
// 正数 nlohmann 是按无符号存的），所以两种都得接住。
    std::optional<Id> jsonId(const nlohmann::json& v) {
        if (!v.is_number_integer())
            return std::nullopt;                 // "1007"、1.5、true 一律不要
        if (v.is_number_unsigned())
            return v.get<Id>();
        const long long n = v.get<long long>();
        if (n < 0)
            return std::nullopt;
        return static_cast<Id>(n);
    }

// 数量：1~99。上限是拍的 —— 食堂档口没人一次点 100 份，而有了上限，
// unitPrice * qty 这种乘法就不可能溢出。
    std::optional<int> jsonQty(const nlohmann::json& v) {
        const auto n = jsonId(v);
        if (!n || *n < 1 || *n > 99)
            return std::nullopt;
        return static_cast<int>(*n);
    }

    void fail(httplib::Response& res, int status, std::string_view msg) {
        nlohmann::json body;
        body["error"] = std::string(msg);
        res.status = status;
        res.set_content(body.dump(2), kJson);
    }

// 按 id 找商户，找不到回 nullptr。
//
// 为什么不直接用 svc.shop(id)：那个找不到就抛 BizError，而 BizError 里只有
// 一个字符串 ——「id 打错了」和「余额不够」从异常上看不出任何区别。把「在不在」
// 提前、明确地做成空指针，「找不到」就永远是 404，不会跟业务规则错误混码。
//
// 只能在握着 g_mtx 的时候调用，返回的指针也别留到解锁之后 ——
// 虽然 Repo.h 保证了节点地址稳定（插入不会让指针失效），但「不失效」
// 不等于「数据不打架」，读的时候还是得有锁兜着。
    Merchant* findShop(DeliveryService& svc, Id id) {
        for (Merchant* m : svc.shops())
            if (m != nullptr && m->id == id)
                return m;
        return nullptr;
    }

// 每个 handler 的外壳：全程握着锁，而且不让异常漏出去。
//
// 两件事绑在一起是有原因的：
//   1) 业务代码是按单线程写的，请求得串行化；
//   2) httplib 拿线程池接请求，异常一旦漏进它的工作线程，客户端连状态码都
//      拿不到（连接被直接掐断），所以必须在这儿翻译成正经的 HTTP 响应。
//
// BizError 的含义是「请求本身没毛病，是当前状态不允许」—— 余额不够、库存
// 没了、店铺休息中 —— 这类回 409；剩下的才是服务端自己的锅，回 500。
    template <class Body>
    void guarded(httplib::Response& res, Body&& body) {
        std::lock_guard<std::mutex> lk{g_mtx};
        try {
            body();
        } catch (const BizError& e) {
            fail(res, 409, e.what());
        } catch (const std::exception& e) {
            fail(res, 500, std::string{"服务端内部错误："} + e.what());
        } catch (...) {
            // 非 std::exception 的异常也得接住：漏出去就是整个进程被终止。
            fail(res, 500, "服务端内部错误");
        }
    }

// 订单流转那一批 POST 的公共骨架。
//
// 顺序固定：id 能不能解析(400) -> 订单在不在(404) -> 状态转不转得动(409)。
// 前两步在这儿做，第三步由传进来的 action 交给服务层 —— 它会走 canTransit，
// 转不动的抛 BizError，一路冒到 guarded 翻成 409。三类错误各归各的码。
//
// 用 store.orders.find() 而不是 svc.order()：后者找不到也抛 BizError，
// 那就跟「状态不对」撞成同一个 409 了，客户端分不出到底是单号打错还是
// 这单不能这么改。Repo::find 给 nullptr，正好对应 404。
//
// 回执统一 {"order": {...}}：客户端拿整份新订单覆盖页面上那张卡片，
// 不用自己去猜哪几个字段变了。
    template <class Action>
    void withOrder(Store& store, const httplib::Request& req, httplib::Response& res, Action&& action) {
        const auto id = parseId(req.path_params.at("id"));
        if (!id) {
            fail(res, 400, "订单 id 得是数字");
            return;
        }

        Order* o = store.orders.find(*id);
        if (o == nullptr) {
            fail(res, 404, "没有这个订单");
            return;
        }

        action(*o);

        // 动作跑完，状态、时间线、落盘都已经在服务层做完了，这里只管回话。
        // 锁还在 guarded 手里，这个引用是稳的。
        nlohmann::json out;
        out["order"] = orderJson(*o);
        res.set_content(out.dump(2), kJson);
    }

// 骑手的两个动作（grab / deliver）请求体里都要一个 riderId，
// 三种 400 情形在这儿一次处理掉：body 是空的 / JSON 坏了 / riderId 缺了
// 或者不是整数。失败时它自己把 res 填好，返回 false 让调用方直接收工。
    bool readRiderId(const httplib::Request& req, httplib::Response& res, Id& out) {
        if (req.body.empty()) {
            fail(res, 400, "请求体是空的，应该发 {\"riderId\":1021}");
            return false;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::parse_error&) {
            fail(res, 400, "请求体不是合法的 JSON");
            return false;
        }

        if (!body.is_object() || !body.contains("riderId")) {
            fail(res, 400, "请求体得是 {\"riderId\":1021}");
            return false;
        }

        const auto id = jsonId(body["riderId"]);
        if (!id) {
            fail(res, 400, "riderId 得是整数");
            return false;
        }

        out = *id;
        return true;
    }

}  // namespace

int main() {
#ifdef _WIN32
    // 控制台默认按 GBK 解释程序输出的字节，源码里的中文是 UTF-8，
    // 不切一下启动信息就是一片乱码。
    SetConsoleOutputCP(CP_UTF8);
#endif

    const std::filesystem::path savePath = save::defaultSavePath();

    Store store;
    DeliveryService svc{store};

    // ---------------- 读存档 ----------------
    if (!std::filesystem::exists(savePath)) {
        std::cerr << "找不到存档：" << savePath.string() << "\n"
                  << "服务端只往外发已有的数据，不会凭空造一份。\n"
                  << "先跑一次控制台版 CampusEats，让它把存档生成出来。\n";
        return 1;
    }

    save::LoadReport report;
    std::string err;
    if (!save::loadStore(store, savePath, report, err)) {
        // 这里故意不灌演示数据，也故意不把坏文件改名 —— 那两件事都是控制台的活。
        // 服务端是常驻进程：灌了假数据它会带着假数据一直跑，
        // 而且第一次改动就会把假的写回磁盘，把真存档盖掉。
        // 控制台做错了你当场看得见，服务端做错了你根本不会知道。
        std::cerr << "读存档失败：" << err << "\n"
                  << "文件：" << savePath.string() << "\n";
        return 1;
    }

    // ---------------- 一改就落盘 ----------------
    // 控制台还有析构函数兜底（正常退出会写一次），服务端没有：
    // 被 Ctrl+C、被强杀、崩掉的时候什么都不剩，只有这个钩子能保住数据。
    //
    // 注意这里不能再加锁：钩子是在 handler 正握着 g_mtx 的时候被调用的
    // （placeOrder 之类的内部会调 notifyChanged），std::mutex 不可重入，
    // 在这儿再锁一次就是当场自死锁。
    svc.onChanged = [&] {
        std::string e;
        if (!save::saveStore(store, savePath, e))
            std::cerr << "落盘失败：" << e << "\n";
    };

    const nlohmann::json bootInfo = {
            {"loadedAt",  formatTime(std::chrono::system_clock::now())},
            {"customers", report.customers},
            {"merchants", report.merchants},
            {"dishes",    report.dishes},
            {"riders",    report.riders},
            {"orders",    report.orders},
    };

    std::cout << "存档：" << savePath.string() << "\n"
              << "载入：" << report.customers << " 名学生、"
              << report.merchants << " 家商户（" << report.dishes << " 道菜）、"
              << report.riders << " 名骑手、"
              << report.orders << " 笔订单\n";
    for (const std::string& w : report.warnings)
        std::cout << "  ! " << w << "\n";

    // ---------------- 接口 ----------------
    // 捕获一律用 [&]：这些东西都活在 main 里，而 listen() 会一直阻塞到
    // 服务端关掉，它们的生命周期肯定比任何 handler 长。
    //
    // 每个 handler 都整体塞进 guarded()：进去先上锁，出来前把异常翻成
    // 状态码。别在中间手动解锁，也别自己再开 try/catch。
    httplib::Server svr;

    svr.Get("/api/ping", [&](const httplib::Request&, httplib::Response& res) {
        guarded(res, [&] {
            nlohmann::json j = bootInfo;   // 启动那一刻读到的存档概况
            j["ok"]       = true;
            j["name"]     = "CampusEats";
            j["role"]     = "server";
            j["saveFile"] = savePath.string();
            res.set_content(j.dump(2), kJson);
        });
    });

    svr.Get("/api/shops", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            // ?q= 传关键词就是搜索，不传就是全部
            const std::string q = req.has_param("q") ? req.get_param_value("q") : std::string{};

            nlohmann::json arr = nlohmann::json::array();
            for (const Merchant* m : svc.shops(q))
                if (m != nullptr)
                    arr.push_back(merchantJson(*m));

            res.set_content(arr.dump(2), kJson);
        });
    });

    svr.Get("/api/shops/:id", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const auto id = parseId(req.path_params.at("id"));
            if (!id) {
                fail(res, 400, "商户 id 得是数字");
                return;
            }

            const Merchant* m = findShop(svc, *id);
            if (m == nullptr) {
                fail(res, 404, "没有这个商户");
                return;
            }
            res.set_content(merchantJson(*m).dump(2), kJson);
        });
    });

    // GET /api/shops/<id>/orders
    // 商家看板：这一家的订单列表 + 统计。
    //
    // 打包成一份而不是拆两个接口，是因为看板本来就要两个一起显示，
    // 分开取的话中间要是有一单被接/被取消，两个数字就对不上了。
    svr.Get("/api/shops/:id/orders", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const auto id = parseId(req.path_params.at("id"));
            if (!id) {
                fail(res, 400, "商户 id 得是数字");
                return;
            }
            if (findShop(svc, *id) == nullptr) {
                fail(res, 404, "没有这个商户");
                return;
            }

            nlohmann::json out;
            out["stats"]  = merchantStatsJson(svc.shopStats(*id));
            out["orders"] = ordersJson(svc.shopOrders(*id));
            res.set_content(out.dump(2), kJson);
        });
    });

    // GET /api/customers
    // 网页「我是谁」的下拉框用。没有分页 —— 这个规模整套数据都在内存里，
    // 一次全发比加个分页参数省事，也省得前端写翻页。
    svr.Get("/api/customers", [&](const httplib::Request&, httplib::Response& res) {
        guarded(res, [&] {
            nlohmann::json arr = nlohmann::json::array();
            // Repo::all() 按插入顺序给（它里面那个 keys_ 就是干这个的），
            // 所以这个列表每次刷新的次序都一样，不会跳来跳去。
            for (const Customer* c : store.customers.all())
                arr.push_back(customerJson(*c));
            res.set_content(arr.dump(2), kJson);
        });
    });

    // GET /api/riders —— 和 /api/customers 是一对，给网页的「我是谁」用。
    // 拿到的 id 就是后面 grab / deliver 要填的那个 riderId。
    svr.Get("/api/riders", [&](const httplib::Request&, httplib::Response& res) {
        guarded(res, [&] {
            nlohmann::json arr = nlohmann::json::array();
            for (const Rider* r : store.riders.all())
                arr.push_back(riderJson(*r));
            res.set_content(arr.dump(2), kJson);
        });
    });

    // GET /api/orders?studentNo=xxx
    // 「我的订单」。顺带把这个人也带上：余额和订单列表是在同一个锁里取的，
    // 读到的是一致的一瞬间，不会出现「余额已经是 145 了、订单列表还没更新」。
    svr.Get("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            if (!req.has_param("studentNo")) {
                fail(res, 400, "要带查询参数 ?studentNo=xxx");
                return;
            }
            const std::string studentNo = req.get_param_value("studentNo");
            if (studentNo.empty()) {
                fail(res, 400, "studentNo 是空的");
                return;
            }

            Customer* cust = svc.findCustomer(studentNo);
            if (cust == nullptr) {
                fail(res, 404, "没有学号是 " + studentNo + " 的学生");
                return;
            }

            nlohmann::json out;
            out["customer"] = customerJson(*cust);
            out["orders"]   = ordersJson(svc.myOrders(cust->id));
            res.set_content(out.dump(2), kJson);
        });
    });

    svr.Get("/api/stats", [&](const httplib::Request&, httplib::Response& res) {
        guarded(res, [&] {
            const Stats s = svc.stats();
            const nlohmann::json j = {
                    {"orderCount",     s.orderCount},
                    {"activeCount",    s.activeCount},
                    {"completedCount", s.completedCount},
                    {"turnover",       moneyJson(s.turnover)},
                    {"avgTicket",      moneyJson(s.avgTicket)},
            };
            res.set_content(j.dump(2), kJson);
        });
    });

    // POST /api/shops/<id>/accepting
    // 请求体：{"accepting":true} 开门 / {"accepting":false} 打烊
    //
    // 状态单独开一个子路径，而不是 PUT 整个商户：商家页上就一个开关，
    // 让它只发这一个字段，别的字段（菜单、库存）就不可能被顺手改坏。
    svr.Post("/api/shops/:id/accepting", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const auto id = parseId(req.path_params.at("id"));
            if (!id) {
                fail(res, 400, "商户 id 得是数字");
                return;
            }

            // 先确认店在不在，再谈别的。放最前面是为了让「店不存在」永远是
            // 404，不至于跟后面业务层抛出来的规则错误混成同一个码。
            Merchant* m = findShop(svc, *id);
            if (m == nullptr) {
                fail(res, 404, "没有这个商户");
                return;
            }

            if (req.body.empty()) {
                fail(res, 400, "请求体是空的，应该发 {\"accepting\":true}");
                return;
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::parse_error&) {
                fail(res, 400, "请求体不是合法的 JSON");
                return;
            }

            // 只认真布尔。JSON 里没有「真值」那套转换规则，收下 "yes" 或者 1
            // 只会让后面每个读这个字段的地方都得自己再判一遍。
            if (!body.is_object() || !body.contains("accepting") ||
                !body["accepting"].is_boolean()) {
                fail(res, 400, "请求体得是 {\"accepting\":true} 或 {\"accepting\":false}");
                return;
            }

            // 内部会 notifyChanged() -> SaveFile 落盘，这里不用自己写文件。
            svc.setAccepting(*id, body["accepting"].get<bool>());

            res.set_content(merchantJson(*m).dump(2), kJson);
        });
    });

    // POST /api/orders
    // 请求体：
    //   {
    //     "studentNo":  "2023001",          // 用学号找下单人，和控制台登录一个规矩
    //     "merchantId": 1011,
    //     "address":    "三号楼 502",        // 可省，省了就送到学生自己的宿舍楼
    //     "note":       "少放辣",            // 可省
    //     "items": [ {"dishId": 1007, "qty": 2} ]
    //   }
    //
    // 两条铁律：
    //   1) 菜名和单价只认服务端菜单里的，客户端发来的价钱一概不看 ——
    //      否则谁都能花一分钱买一碗面；
    //   2) 「东西在不在」在这儿判（404），「能不能买」交给
    //      DeliveryService::placeOrder（休息中、没库存、余额不够，抛 BizError -> 409）。
    //      两件事分清楚，前端才知道该改请求、还是该等店家开门。
    svr.Post("/api/orders", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            if (req.body.empty()) {
                fail(res, 400, "请求体是空的");
                return;
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::parse_error&) {
                fail(res, 400, "请求体不是合法的 JSON");
                return;
            }
            if (!body.is_object()) {
                fail(res, 400, "请求体得是一个 JSON 对象");
                return;
            }

            // ---- 下单人 ----
            // 用学号而不是内部 id：网页上填的是学号，控制台登录也是学号。
            if (!body.contains("studentNo") || !body["studentNo"].is_string()) {
                fail(res, 400, "studentNo 得是字符串，比如 \"2023001\"");
                return;
            }
            const std::string studentNo = body["studentNo"].get<std::string>();
            Customer* cust = svc.findCustomer(studentNo);
            if (cust == nullptr) {
                fail(res, 404, "没有学号是 " + studentNo + " 的学生");
                return;
            }

            // ---- 商户 ----
            if (!body.contains("merchantId")) {
                fail(res, 400, "缺 merchantId");
                return;
            }
            const auto merchantId = jsonId(body["merchantId"]);
            if (!merchantId) {
                fail(res, 400, "merchantId 得是整数");
                return;
            }
            Merchant* shop = findShop(svc, *merchantId);
            if (shop == nullptr) {
                fail(res, 404, "没有这个商户");
                return;
            }

            // ---- 收货地址：不给就落回学生自己的宿舍楼 ----
            std::string address = cust->building;
            if (body.contains("address")) {
                if (!body["address"].is_string()) {
                    fail(res, 400, "address 得是字符串");
                    return;
                }
                const std::string want = body["address"].get<std::string>();
                if (!want.empty())
                    address = want;
            }
            if (address.empty()) {
                fail(res, 400, "收货地址是空的：请求里带上 address，或者给这个学生填上宿舍楼");
                return;
            }

            std::string note;
            if (body.contains("note")) {
                if (!body["note"].is_string()) {
                    fail(res, 400, "note 得是字符串");
                    return;
                }
                note = body["note"].get<std::string>();
            }

            // ---- 菜品 ----
            if (!body.contains("items") || !body["items"].is_array()) {
                fail(res, 400, "items 得是数组");
                return;
            }
            const nlohmann::json& lines = body["items"];
            if (lines.empty()) {
                fail(res, 400, "items 是空的，至少点一道菜");
                return;
            }

            Cart cart;
            std::vector<Id> seen;   // 一单没几道菜，线性查重足够

            for (std::size_t i = 0; i < lines.size(); ++i) {
                const nlohmann::json& line = lines[i];
                // 报错时带上第几项，客户端一眼知道是哪行写坏了
                const std::string at = "items[" + std::to_string(i) + "]";

                if (!line.is_object()) {
                    fail(res, 400, at + " 得是对象，比如 {\"dishId\":1007,\"qty\":2}");
                    return;
                }
                if (!line.contains("dishId") || !line.contains("qty")) {
                    fail(res, 400, at + " 里得有 dishId 和 qty");
                    return;
                }

                const auto dishId = jsonId(line["dishId"]);
                if (!dishId) {
                    fail(res, 400, at + " 的 dishId 得是整数");
                    return;
                }
                const auto qty = jsonQty(line["qty"]);
                if (!qty) {
                    fail(res, 400, at + " 的 qty 得是 1~99 的整数");
                    return;
                }

                // 同一道菜拆成两条会走到 Cart::add 的合并逻辑上去，而那个行为
                // 我们没打算依赖 —— 退回去让客户端并成一条，比自己在这里猜强。
                if (std::find(seen.begin(), seen.end(), *dishId) != seen.end()) {
                    fail(res, 400, at + " 和前面的菜重了，同样的菜请并成一条，把 qty 加起来");
                    return;
                }
                seen.push_back(*dishId);

                const Dish* dish = shop->findDish(*dishId);
                if (dish == nullptr) {
                    fail(res, 404, "商户 " + std::to_string(*merchantId) +
                                   " 的菜单里没有菜品 " + std::to_string(*dishId));
                    return;
                }

                // 这里只把「有这道菜」写进购物车。上架、库存、余额、店铺营业状态
                // 这些判断全在 placeOrder 里 —— 规则只能有一份，抄第二遍迟早对不上。
                cart.add(*merchantId, *dish, *qty);
            }

            Order& order = svc.placeOrder(cust->id, *merchantId, cart, address, note);

            // placeOrder 里已经 notifyChanged() 落过盘了，这儿只管回话。
            nlohmann::json out;
            out["order"]   = orderJson(order);
            out["balance"] = moneyJson(cust->balance);   // 扣完钱的余额
            res.set_content(out.dump(2), kJson);
        });
    });

    // ---------------- 订单流转 ----------------
    //
    // 【这批接口没有鉴权】谁都能调，路由上不区分身份，也不检查「这单是不是
    // 你的」。校内自用、两个人各开一个页面，靠页面上只显示自己那几个按钮来
    // 约束，不靠服务端拦 —— 前端少显示一个按钮只是提前告知，服务端那道
    // canTransit 才是真正拦得住的地方。真要做权限得先有登录态（会话或 token），
    // 那是另一件事，不在这轮里。
    //
    // 五个动作都是「拿订单 -> 改状态 -> 回整份订单」。状态合不合法全交给
    // 服务层（内部走 canTransit），转不动的抛 BizError，由 guarded 翻成 409。

    // 待接单 -> 备餐中
    svr.Post("/api/orders/:id/accept", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            withOrder(store, req, res, [&](Order& o) { svc.acceptOrder(o.id); });
        });
    });

    // 备餐中 -> 待取餐。到这一步骑手才看得见（对应 pickupBoard）
    svr.Post("/api/orders/:id/finish", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            withOrder(store, req, res, [&](Order& o) { svc.finishCooking(o.id); });
        });
    });

    // 学生取消。退钱、退库存是服务层的事，handler 不重复做一遍
    svr.Post("/api/orders/:id/cancel", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            withOrder(store, req, res, [&](Order& o) { svc.cancelOrder(o.id); });
        });
    });

    // 确认收货 —— 配送中订单的两条收尾路之一（另一条是骑手点送达）。
    // 谁先点谁算，后点的那次由状态机挡下来（409）。
    svr.Post("/api/orders/:id/receive", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            withOrder(store, req, res, [&](Order& o) { svc.confirmReceived(o.id); });
        });
    });

    // 商家拒单。请求体 {"reason": "..."} 可以省 —— 省了用一句默认的。
    // 拒单这个动作本身不需要理由才成立，理由只是写给学生看的。
    svr.Post("/api/orders/:id/reject", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            std::string reason = "商家暂时忙不过来";

            if (!req.body.empty()) {
                nlohmann::json body;
                try {
                    body = nlohmann::json::parse(req.body);
                } catch (const nlohmann::json::parse_error&) {
                    fail(res, 400, "请求体不是合法的 JSON");
                    return;
                }
                if (!body.is_object()) {
                    fail(res, 400, "请求体得是一个 JSON 对象（或者干脆不发）");
                    return;
                }
                if (body.contains("reason")) {
                    if (!body["reason"].is_string()) {
                        fail(res, 400, "reason 得是字符串");
                        return;
                    }
                    const std::string want = body["reason"].get<std::string>();
                    if (!want.empty())
                        reason = want;
                }
            }

            withOrder(store, req, res, [&](Order& o) { svc.rejectOrder(o.id, reason); });
        });
    });

    // 充值：POST /api/customers/<学号>/topup，请求体 {"amountYuan": 50}
    //
    // 单位是「元」，和服务层 topUp(Id, int amountYuan) 对齐 —— 换算成「分」
    // 是服务层自己的事，这一层别替它算，不然两处单位不一致时最难查。
    // 上限 1000 是拍的：挡手滑多打一个零，不是挡坏人。
    svr.Post("/api/customers/:studentNo/topup", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const std::string studentNo = req.path_params.at("studentNo");
            Customer* c = svc.findCustomer(studentNo);
            if (c == nullptr) {
                fail(res, 404, "没有学号是 " + studentNo + " 的学生");
                return;
            }

            if (req.body.empty()) {
                fail(res, 400, "请求体是空的，应该发 {\"amountYuan\":50}");
                return;
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body);
            } catch (const nlohmann::json::parse_error&) {
                fail(res, 400, "请求体不是合法的 JSON");
                return;
            }
            if (!body.is_object() || !body.contains("amountYuan")) {
                fail(res, 400, "请求体得是 {\"amountYuan\":50}");
                return;
            }

            const auto amount = jsonId(body["amountYuan"]);
            if (!amount) {
                fail(res, 400, "amountYuan 得是整数");
                return;
            }
            if (*amount < 1 || *amount > 1000) {
                fail(res, 400, "amountYuan 得在 1~1000 之间");
                return;
            }

            svc.topUp(c->id, static_cast<int>(*amount));

            nlohmann::json out;
            out["customer"]   = customerJson(*c);    // 充完之后的余额
            out["amountYuan"] = static_cast<int>(*amount);
            res.set_content(out.dump(2), kJson);
        });
    });

    // ---------------- 骑手 ----------------
    //
    // 全用内部 id（1021 / 1023）定位骑手，不用工号 —— 存档里那两行的工号
    // 一模一样（344886608），按 staffNo 查分不出谁是谁。这不是推测：
    // /api/riders 和 campus-save.txt 上各看过一遍。

    // GET /api/rider/board —— 等取餐的单（已出餐、还没人接）。
    // 路径用单数 rider，免得跟上面 /api/riders/<id>/tasks 的 :id 打架 ——
    // 真写成 /api/riders/board 的话，httplib 得先决定 board 是 id 还是字面量。
    svr.Get("/api/rider/board", [&](const httplib::Request&, httplib::Response& res) {
        guarded(res, [&] {
            res.set_content(ordersJson(svc.pickupBoard()).dump(2), kJson);
        });
    });

    // GET /api/riders/<id>/tasks —— 这个骑手手上的单，外加他的战绩。
    // 和商家看板一样打包成一份，理由也一样：一次锁里取到的才是同一瞬间的。
    svr.Get("/api/riders/:id/tasks", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            const auto id = parseId(req.path_params.at("id"));
            if (!id) {
                fail(res, 400, "骑手 id 得是数字");
                return;
            }
            const Rider* r = store.riders.find(*id);
            if (r == nullptr) {
                fail(res, 404, "没有这个骑手");
                return;
            }

            nlohmann::json out;
            out["rider"]  = riderJson(*r);
            out["stats"]  = riderStatsJson(svc.riderStats(*id));
            out["orders"] = ordersJson(svc.riderTasks(*id));
            res.set_content(out.dump(2), kJson);
        });
    });

    // 抢单：待取餐 -> 配送中。请求体 {"riderId":1021}
    //
    // 骑手存在性在这儿判（404），「这单还在不在板上」交给服务层
    // （grabOrder 里会走状态机，被别人抢先了就抛 BizError -> 409）。
    svr.Post("/api/orders/:id/grab", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            Id riderId = kNoId;
            if (!readRiderId(req, res, riderId))
                return;

            if (store.riders.find(riderId) == nullptr) {
                fail(res, 404, "没有这个骑手");
                return;
            }

            withOrder(store, req, res, [&](Order& o) { svc.grabOrder(o.id, riderId); });
        });
    });

    // 送达：配送中 -> 已送达。请求体 {"riderId":1021}
    //
    // 重复点击由服务层挡：markDelivered 内部走 canTransit，已经送达的单
    // 第二次调用一定抛 BizError -> 409。（这个函数以前绕过状态机直接 push，
    // 同一单能被"送达"两遍，时间线里多出一行重复的 completed —— 已修。）
    svr.Post("/api/orders/:id/deliver", [&](const httplib::Request& req, httplib::Response& res) {
        guarded(res, [&] {
            Id riderId = kNoId;
            if (!readRiderId(req, res, riderId))
                return;

            if (store.riders.find(riderId) == nullptr) {
                fail(res, 404, "没有这个骑手");
                return;
            }

            withOrder(store, req, res, [&](Order& o) { svc.markDelivered(o.id, riderId); });
        });
    });

    // ---------------- 静态网页 ----------------
    //
    // 网页放在存档旁边的 www/ 里。用存档路径推出来，不去猜"当前工作目录" ——
    // CLion 从哪启动的、双击 exe 时的工作目录，都可能不一样，靠相对路径
    // 迟早踩一次（存档路径那个坑就是这么来的）。
    //
    // API 全在 /api/ 底下，静态文件在根上，两边路径不重叠 —— 不管 httplib
    // 内部先匹配哪一边，都不会打架。唯一要守的规矩：别在 www/ 里建 api/ 目录。
    //
    // 挂不上也不 return：网页打不开和 API 不能用，严重程度不一样，
    // 不该因为少了个目录就把整个服务端按死。
    const std::filesystem::path wwwDir = savePath.parent_path() / "www";
    if (std::filesystem::is_directory(wwwDir)) {
        if (svr.set_mount_point("/", wwwDir.string()))
            std::cout << "网页：" << wwwDir.string() << "\n";
        else
            std::cerr << "网页目录挂不上：" << wwwDir.string() << "（API 照常）\n";
    } else {
        std::cout << "没有网页目录 " << wwwDir.string() << "，只开 API。\n";
    }

    std::cout << "\n接口：\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/ping\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/shops\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/shops/<id>\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/shops/<id>/orders\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/customers\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/riders\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/riders/<id>/tasks\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/rider/board\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/orders?studentNo=<学号>\n"
              << "  GET  http://127.0.0.1:" << kPort << "/api/stats\n"
              << "  POST http://127.0.0.1:" << kPort << "/api/shops/<id>/accepting\n"
              << "  POST http://127.0.0.1:" << kPort << "/api/orders\n"
              << "  POST http://127.0.0.1:" << kPort << "/api/orders/<id>/accept|finish|reject|cancel|receive\n"
              << "  POST http://127.0.0.1:" << kPort << "/api/orders/<id>/grab|deliver\n"
              << "  POST http://127.0.0.1:" << kPort << "/api/customers/<学号>/topup\n"
              << "\n网页：本机 http://127.0.0.1:" << kPort << "/\n"
              << "      另一台设备把 127.0.0.1 换成这台电脑的局域网 IP（ipconfig 里看）\n"
              << "按 Ctrl+C 停止\n";

    // 听哪个地址见文件开头的 kBindAddr。
    if (!svr.listen(kBindAddr, kPort)) {
        std::cerr << "监听 " << kBindAddr << ":" << kPort
                  << " 失败，可能端口被占用了。\n";
        return 1;
    }
    return 0;
}