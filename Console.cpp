#include "Console.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "Errors.h"
#include "Text.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace eats::ui {
    namespace {

        bool g_vt = false;        // 这个终端支不支持 ANSI 转义序列
        int  g_eofStreak = 0;     // 连续读不到输入次数，见 ask()

        constexpr const char* kReset  = "\033[0m";
        constexpr const char* kDim    = "\033[90m";
        constexpr const char* kRed    = "\033[91m";
        constexpr const char* kGreen  = "\033[92m";
        constexpr const char* kYellow = "\033[93m";
        constexpr const char* kCyan   = "\033[96m";

        void cprint(const char* color, std::string_view msg) {
            if (g_vt) std::cout << color;
            std::cout << msg;
            if (g_vt) std::cout << kReset;
            std::cout << '\n';
        }

    }  // namespace

    void initConsole() {
#ifdef _WIN32
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
        if (const HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE); h && h != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (::GetConsoleMode(h, &mode)) {
                // Win10 1511 之后的控制台都吃 ANSI，开了就不用退化成 system("cls")
                g_vt = ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
            }
        }
#endif
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
    }

    void clearScreen() {
        if (g_vt) {
            std::cout << "\033[2J\033[H";
        } else {
            std::system("cls");   // 输出被重定向到文件时，至少不会喷一屏转义码
        }
        std::cout << std::flush;
    }

    void pause(std::string_view hint) {
        if (g_vt) std::cout << kDim;
        std::cout << "\n" << hint << " ...";
        if (g_vt) std::cout << kReset;
        std::cout << std::flush;

        std::string tmp;
        std::getline(std::cin, tmp);
    }

    void info(std::string_view msg)  { cprint(kCyan, msg); }
    void ok(std::string_view msg)    { cprint(kGreen, msg); }
    void warn(std::string_view msg)  { cprint(kYellow, msg); }
    void error(std::string_view msg) { cprint(kRed, msg); }

    std::string ask(const std::string& prompt, const std::string& defaultVal) {
        std::cout << prompt;
        if (!defaultVal.empty()) std::cout << "（默认 " << defaultVal << "）";
        std::cout << "\n> " << std::flush;

        std::string line;
        if (!std::getline(std::cin, line)) {
            // 控制台里 Ctrl+Z 之后还能接着用，所以第一次不算数。
            // 连着三次读不到东西，基本就是重定向的输入流读完了，
            // 再往下 askInt 会在「读空 → 报格式错 → 再读」里空转刷屏。
            if (++g_eofStreak >= 3) throw InputClosed{};
            std::cin.clear();
            return defaultVal;
        }
        g_eofStreak = 0;

        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        return line.empty() ? defaultVal : line;
    }

    long long askInt(const std::string& prompt, long long lo, long long hi) {
        for (;;) {
            const std::string raw = ask(prompt);
            std::size_t pos = 0;
            try {
                const long long v = std::stoll(raw, &pos);
                if (pos == raw.size() && v >= lo && v <= hi) return v;
            } catch (const std::exception&) {
                // 不是数字，落到下面统一提示
            }
            warn("请输入 " + std::to_string(lo) + " ~ " + std::to_string(hi) + " 之间的整数");
        }
    }

    bool askYesNo(const std::string& prompt) {
        for (;;) {
            const std::string s = ask(prompt + " [y/N]");
            if (s.empty() || s == "n" || s == "N" || s == "no") return false;
            if (s == "y" || s == "Y" || s == "yes" || s == "是") return true;
            warn("回 y 或 n");
        }
    }

    std::optional<int> parseInt(std::string_view s) {
        try {
            std::size_t pos = 0;
            const int v = std::stoi(std::string(s), &pos);
            if (pos == s.size()) return v;
        } catch (const std::exception&) {
            // 本来就不是数字，属正常输入
        }
        return std::nullopt;
    }

    void printLine(char c, int n) {
        std::cout << std::string(static_cast<std::size_t>(n), c) << '\n';
    }

    void printTable(const std::vector<std::string>& headers,
                    const std::vector<std::vector<std::string>>& rows,
                    const std::vector<int>& widths) {
        auto emit = [&](const std::vector<std::string>& cells) {
            std::string out = "  ";
            for (std::size_t i = 0; i < cells.size() && i < widths.size(); ++i) {
                out += text::pad(cells[i], widths[i]);
                out += "  ";
            }
            std::cout << out << '\n';
        };

        int total = 0;
        for (const int w : widths) total += w + 2;

        emit(headers);
        std::cout << "  " << std::string(static_cast<std::size_t>(total), '-') << '\n';
        for (const auto& row : rows) emit(row);
    }

}  // namespace eats::ui