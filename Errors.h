// [core] 业务规则被违反时抛 BizError。UI 层在循环里兜住它，
// 用户点错一下不会让整个程序挂掉。
#pragma once
#include <stdexcept>
#include <string>
#include <utility>

namespace eats {

    class BizError : public std::runtime_error {
    public:
        explicit BizError(std::string msg) : std::runtime_error(std::move(msg)) {}
    };

    [[noreturn]] inline void fail(std::string msg) { throw BizError(std::move(msg)); }

// 输入流断了（重定向的文件读完了、标准输入被关掉）。
// 这个不是业务错误：各层界面都不能兜它，得让它一路抛到 main 收尾，
// 否则控制台菜单会在「读不到输入 → 提示 → 再读」之间空转。
    class InputClosed : public BizError {
    public:
        InputClosed() : BizError("标准输入已经结束") {}
    };

}  // namespace eats