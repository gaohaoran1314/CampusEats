// [ui] 控制台输入输出的统一封装。别的 ui 文件都用它，
// 不要各自去玩 std::cin 的格式状态。
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eats::ui {

// Windows 控制台要手动切 UTF-8 代码页，不切中文全是问号
    void initConsole();

    void clearScreen();
    void pause(std::string_view hint = "按回车继续");

    void info(std::string_view msg);
    void ok(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);

    std::string ask(const std::string& prompt, const std::string& defaultVal = {});
    long long   askInt(const std::string& prompt, long long lo, long long hi);
    bool        askYesNo(const std::string& prompt);
    std::optional<int> parseInt(std::string_view s);

// 简易表格，列宽自己给；超宽的格子会被截断加省略号
    void printTable(const std::vector<std::string>& headers,
                    const std::vector<std::vector<std::string>>& rows,
                    const std::vector<int>& widths);

    void printLine(char c = '-', int n = 62);

}  // namespace eats::ui