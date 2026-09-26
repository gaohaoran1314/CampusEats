// [core] 全局通用件。这里不放业务逻辑，任何模块都能无脑 include。
#pragma once
#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

namespace eats {

    using Id = std::uint64_t;
    inline constexpr Id kNoId = 0;

// 进程内自增主键。以后接数据库换成 DB 生成，调用方不用改。
    inline Id& idCounter() {
        static Id counter = 1000;
        return counter;
    }
    inline Id nextId() { return ++idCounter(); }

    inline void advanceIdTo(Id seen){
        if (seen > idCounter()) idCounter() = seen;
    }

// MSVC 的 localtime_s 参数顺序和 POSIX 的 localtime_r 正好反着来
    inline std::tm toLocalTime(std::time_t t) {
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        return tm;
    }

    inline std::string formatTime(std::chrono::system_clock::time_point tp,
                                  const char* fmt = "%m-%d %H:%M") {
        const std::time_t t = std::chrono::system_clock::to_time_t(tp);
        const std::tm tm = toLocalTime(t);
        char buf[64]{};
        if (std::strftime(buf, sizeof(buf), fmt, &tm) == 0) return {};
        return buf;
    }

}  // namespace eats