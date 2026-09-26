// [core] 钱一律用「分」存 long long。
// 别拿 double 算金额，早上还好好的，晚上对账就差三分钱。
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace eats {

    struct Money {
        long long cents{0};
    };

// 写常量时 yuan(12.5) 比 1250 直观
    inline Money yuan(double v) {
        return Money{ static_cast<long long>(std::llround(v * 100.0)) };
    }

    inline Money  operator+(Money a, Money b) { return Money{ a.cents + b.cents }; }
    inline Money  operator-(Money a, Money b) { return Money{ a.cents - b.cents }; }
    inline Money  operator*(Money a, int n)   { return Money{ a.cents * n }; }
    inline Money& operator+=(Money& a, Money b) { a.cents += b.cents; return a; }
    inline Money& operator-=(Money& a, Money b) { a.cents -= b.cents; return a; }

    inline bool operator==(Money a, Money b) { return a.cents == b.cents; }
    inline bool operator!=(Money a, Money b) { return a.cents != b.cents; }
    inline bool operator<(Money a, Money b)  { return a.cents < b.cents; }
    inline bool operator>(Money a, Money b)  { return b.cents < a.cents; }
    inline bool operator<=(Money a, Money b) { return a.cents <= b.cents; }
    inline bool operator>=(Money a, Money b) { return b.cents <= a.cents; }

// 打印用。别用 std::to_string(double)，会给你 12.000000
    inline std::string toString(Money m) {
        const bool neg = m.cents < 0;
        const long long v = neg ? -m.cents : m.cents;
        char buf[32]{};
        std::snprintf(buf, sizeof(buf), "%s%lld.%02lld",
                      neg ? "-¥" : "¥", v / 100, v % 100);
        return buf;
    }

}  // namespace eats