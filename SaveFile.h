// [service] 把内存里的 Store 存成文本文件，再从文件读回来。
//
// 格式是自己拍的一行一条记录，长这样：
//   [customer]|id=1001|no=20230001|name=张三|building=3 号楼 306|balance=20000
//   [dish]|mid=1001|id=1005|name=红烧牛肉面|cat=主食|price=1200|stock=-1|on=1
//
// 字段用 | 分隔（值里的 | 会被转义成 \p），金额一律按「分」存整数。
// 写盘走「先写 .tmp 再改名」，中途崩了也不会把原存档写坏。
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "Store.h"

namespace eats::save {

// 存档格式版本。以后字段有增减就 +1，读旧版本时至少能明确拒绝，
// 而不是把不认识的字段当默认值读出垃圾数据。
    inline constexpr int kFormatVersion = 1;

    struct LoadReport {
        std::size_t customers{0};
        std::size_t merchants{0};
        std::size_t dishes{0};
        std::size_t riders{0};
        std::size_t orders{0};

        std::vector<std::string> warnings;   // 跳过的坏记录，给用户看一眼
    };

// 数据目录。默认是编译时固化的工程根目录（CMakeLists 里的 CAMPUS_EATS_DATA_DIR），
// 这样不管从哪儿启动、用哪个构建配置，读写都是同一份数据。
    std::filesystem::path dataDir();

// 存档文件位置：dataDir()/campus-save.txt。
// 想临时拿另一份存档做实验，设环境变量 CAMPUS_EATS_SAVE 指到别处就行。
    std::filesystem::path defaultSavePath();

// 写存档。失败返回 false 并把原因写进 err。
// 刻意不抛异常 —— 它会从 App 的析构里被调用，那儿抛异常会直接 terminate。
    bool saveStore(const Store& store, const std::filesystem::path& path, std::string& err);

// 读存档。读进来的数据直接塞进 store。
// 失败时（文件不存在、版本不对、格式坏了）返回 false，并且 store 已被清空，
// 调用方可以放心地往里面灌演示数据。
    bool loadStore(Store& store, const std::filesystem::path& path,
                   LoadReport& report, std::string& err);

}  // namespace eats::save