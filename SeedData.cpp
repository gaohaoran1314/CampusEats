#include "SeedData.h"

#include <string>
#include <utility>
#include <vector>

namespace eats {
    namespace {

        Dish dish(std::string name, std::string category, double price, int stock = -1) {
            Dish d;
            d.id       = nextId();
            d.name     = std::move(name);
            d.category = std::move(category);
            d.price    = yuan(price);
            d.stock    = stock;
            return d;
        }

        Merchant shop(std::string name, std::string location, double rating,
                      std::vector<Dish> menu, bool accepting = true) {
            Merchant m;
            m.name      = std::move(name);
            m.location  = std::move(location);
            m.rating    = rating;
            m.accepting = accepting;
            m.menu      = std::move(menu);
            return m;
        }

    }  // namespace

    void seedDemoData(Store& store) {
        store.merchants.add(shop("老王面馆", "一食堂 1 楼 3 号窗口", 4.8, {
                dish("红烧牛肉面", "主食", 12.0),
                dish("番茄鸡蛋面", "主食", 10.0),
                dish("炸酱面",     "主食", 11.0),
                dish("凉拌黄瓜",   "小吃", 5.0),
                dish("卤蛋",       "小吃", 2.0, 60),
        }));

        store.merchants.add(shop("川味香锅", "三食堂 2 楼东侧", 4.6, {
                dish("香锅单人餐", "套餐", 18.0, 40),
                dish("香锅双人餐", "套餐", 33.0, 20),
                dish("米饭",       "主食", 1.0),
                dish("酸梅汤",     "饮品", 3.0, 80),
        }));

        store.merchants.add(shop("轻食沙拉吧", "二食堂 3 楼", 4.3, {
                dish("鸡胸肉沙拉",     "轻食", 16.0, 15),
                dish("全麦火腿三明治", "轻食", 13.0, 15),
                dish("无糖豆浆",       "饮品", 4.0),
        }));

        // 故意留一家休息中的，方便看各种状态的展示效果
        store.merchants.add(shop("北门炸鸡", "北门商业街 12 号", 4.1, {
                dish("香辣鸡腿堡", "小吃", 15.0),
                dish("鸡米花",     "小吃", 9.0, 0),   // 售罄
                dish("冰可乐",     "饮品", 5.0),
        }, /*accepting=*/false));
    }

}  // namespace eats