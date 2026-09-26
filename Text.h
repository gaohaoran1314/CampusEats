// [core] 控制台对齐中文是老大难：UTF-8 里一个汉字占 3 字节，
// 显示宽度却是 2 列。用 std::string::size() 算列宽，表格必歪。
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace eats::text {

    inline int charWidth(char32_t cp) {
        if (cp < 0x1100) return 1;
        // 只覆盖常见的宽字符区间，够用了
        if ((cp >= 0x1100 && cp <= 0x115F) ||    // 谚文字母
            (cp >= 0x2E80 && cp <= 0xA4CF) ||    // 中日韩部首、汉字
            (cp >= 0xAC00 && cp <= 0xD7A3) ||    // 谚文音节
            (cp >= 0xF900 && cp <= 0xFAFF) ||    // 兼容汉字
            (cp >= 0xFE30 && cp <= 0xFE6F) ||    // 兼容形式
            (cp >= 0xFF00 && cp <= 0xFF60) ||    // 全角形式
            (cp >= 0xFFE0 && cp <= 0xFFE6) ||
            (cp >= 0x20000 && cp <= 0x3FFFD)) {  // 扩展 B 及以后
            return 2;
        }
        return 1;
    }

// 解一个 UTF-8 码点，i 往后退
    inline char32_t nextCodePoint(std::string_view s, std::size_t& i) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        char32_t cp = lead;
        int extra = 0;
        if (lead >= 0xF0)      { cp = lead & 0x07u; extra = 3; }
        else if (lead >= 0xE0) { cp = lead & 0x0Fu; extra = 2; }
        else if (lead >= 0xC0) { cp = lead & 0x1Fu; extra = 1; }
        ++i;
        while (extra-- > 0 && i < s.size()) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);
            ++i;
        }
        return cp;
    }

    inline int width(std::string_view s) {
        int w = 0;
        for (std::size_t i = 0; i < s.size();) w += charWidth(nextCodePoint(s, i));
        return w;
    }

// 超宽就截断。按码点切，不会把一个汉字劈成两半
    inline std::string clip(std::string_view s, int wish) {
        if (width(s) <= wish) return std::string(s);
        if (wish <= 1) return "…";

        std::string out;
        int w = 0;
        for (std::size_t i = 0; i < s.size();) {
            const std::size_t start = i;
            const int cw = charWidth(nextCodePoint(s, i));
            if (w + cw > wish - 1) break;      // 留一列给省略号
            out.append(s.substr(start, i - start));
            w += cw;
        }
        out += "…";
        return out;
    }

    inline std::string pad(std::string_view s, int wish) {
        std::string out = clip(s, wish);
        const int w = width(out);
        if (w < wish) out.append(static_cast<std::size_t>(wish - w), ' ');
        return out;
    }

// 定点小数，打印评分 / 导 CSV 都用它
    inline std::string num(double v, int decimals = 1) {
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        return buf;
    }

}  // namespace eats::text