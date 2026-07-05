#include "test.h"

#include "world/money_format.h"

#include <string>

using namespace guild;
using namespace guild::world;

namespace {
// Render the glyph (0x11) as a readable "<G>" so golden strings stay printable.
std::string Show(const std::string& s) {
    std::string out;
    for (char c : s)
        out += (c == kCurrencyGlyph) ? std::string("<G>") : std::string(1, c);
    return out;
}
} // namespace

// Golden vectors derived directly from the faithful translation of the original
// grouping loop (verified with a Python re-implementation of 0x58f798).
TEST(WorldMoneyFormat, GroupingGoldens) {
    CHECK_EQ(Show(MoneyFormatWithSeparators(0)),       "0<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(5)),       "5<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(999)),     "999<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(1000)),    "1.000<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(1234)),    "1.234<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(12345)),   "12.345<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(123456)),  "123.456<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(1234567)), "1.234.567<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(2000000)), "2.000.000<G>");
}

TEST(WorldMoneyFormat, NegativesAndZero) {
    // Negative magnitude rounds the absolute value, then a leading '-' is added.
    CHECK_EQ(Show(MoneyFormatWithSeparators(-5)),       "-5<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(-1234)),    "-1.234<G>");
    CHECK_EQ(Show(MoneyFormatWithSeparators(-999999)),  "-999.999<G>");
    // Rounded magnitude 0 always prints "0<G>" with NO sign (both branches).
    CHECK_EQ(Show(MoneyFormatWithSeparators(0)),  "0<G>");
}

TEST(WorldMoneyFormat, RateRoundsHalfUp) {
    // rate divides first, then round-half-up: 2500/1000 = 2.5 -> 3.
    CHECK_EQ(Show(MoneyFormatWithSeparators(2500, 1000)), "3<G>");
    // 2499/1000 = 2.499 -> 2.
    CHECK_EQ(Show(MoneyFormatWithSeparators(2499, 1000)), "2<G>");
    // 1500/1000 = 1.5 -> 2 (half-up).
    CHECK_EQ(Show(MoneyFormatWithSeparators(1500, 1000)), "2<G>");
    // Large amount with a rate, crossing 1000 after division: 5_000_000/100 = 50000.
    CHECK_EQ(Show(MoneyFormatWithSeparators(5000000, 100)), "50.000<G>");
    // Negative with rate.
    CHECK_EQ(Show(MoneyFormatWithSeparators(-2500, 1000)), "-3<G>");
    // rate 0 is treated as identity (1).
    CHECK_EQ(Show(MoneyFormatWithSeparators(1234, 0)), "1.234<G>");
}

// HARDENING: extreme magnitudes. gilde.exe 0x58f7a8-0x58f7ab computes abs32 via
// `cdq; xor eax,edx; sub eax,edx` (INT_MIN wraps to INT_MIN) and 0x58f7dd loads
// it back with a SIGNED `fild` — so INT_MIN's magnitude is -2147483648.0, the
// rounded value is negative, takes the `< 1000` "%i" path, and the "-%i%c"
// format doubles the sign.
// [old pins ("-2.147.483.648<G>" / "-2.147.484<G>") assumed an unsigned widen;
//  PROVEN wrong against the binary at 0x58f7a8/0x58f7dd.]
TEST(WorldMoneyFormat, ExtremeMagnitudes) {
    // INT_MAX = 2147483647 -> "2.147.483.647".
    CHECK_EQ(Show(MoneyFormatWithSeparators(2147483647)), "2.147.483.647<G>");
    // INT_MIN: mag = INT_MIN; -2147483648.0/1 + 0.5 -> trunc -2147483647;
    // a1 < 0 branch, v24 < 1000 -> "-%i%c" with a negative v24.
    CHECK_EQ(Show(MoneyFormatWithSeparators(-2147483648)), "--2147483647<G>");
    // INT_MIN with a rate divisor: -2147483648/1000 + 0.5 = -2147483.148 ->
    // trunc -2147483 -> "-%i%c" again doubles the sign.
    CHECK_EQ(Show(MoneyFormatWithSeparators(-2147483648, 1000)), "--2147483<G>");
    // The grouping core on the full 32-bit unsigned range.
    CHECK_EQ(MoneyGroupThousands(4294967295u), std::string("4.294.967.295"));
    CHECK_EQ(MoneyGroupThousands(2147483648u), std::string("2.147.483.648"));
}

TEST(WorldMoneyFormat, GroupCoreOnly) {
    // The exposed grouping core (no sign, no glyph).
    CHECK_EQ(MoneyGroupThousands(0u),       std::string("0"));
    CHECK_EQ(MoneyGroupThousands(42u),      std::string("42"));
    CHECK_EQ(MoneyGroupThousands(999u),     std::string("999"));
    CHECK_EQ(MoneyGroupThousands(1000u),    std::string("1.000"));
    CHECK_EQ(MoneyGroupThousands(10000u),   std::string("10.000"));
    CHECK_EQ(MoneyGroupThousands(100000u),  std::string("100.000"));
    CHECK_EQ(MoneyGroupThousands(1000000u), std::string("1.000.000"));
    CHECK_EQ(MoneyGroupThousands(123456789u), std::string("123.456.789"));
}
