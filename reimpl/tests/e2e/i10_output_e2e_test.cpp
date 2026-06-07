#include "test.h"

#include "crt/i10_output.h"

#include <cmath>
#include <string>

// End-to-end exercise of the x87 long-double formatter cluster through the public
// driver VIBE_Crt_FormatExponentFloat @0x6060be -> FloatToDigits @0x5fa7fd ->
// FormatFixedPoint @0x5fac3f / FormatExponent @0x5fae5d.
//
// Golden strings reproduce the game's MSVC `_$I10_OUTPUT` behavior (NOT glibc):
//   * exponents are a MINIMUM of 3 digits (e.g. e+004, e+000), widening to 4;
//   * half-away-from-zero rounding (0.125 -> "0.13", where glibc gives "0.12");
//   * the driver buffer is the complete visible string for normal-magnitude %f
//     and for all %e/%E/nan/inf (sub-one and carry-overflow %f defer leading /
//     trailing zeros to the printf core's field metadata and are covered by the
//     unit tests instead).

using namespace guild::crt;

namespace {

std::string Fmt(char conv, int prec, long double v, bool alt = false) {
    char buf[160];
    char* r = FormatExponentFloat(conv, prec, alt, v, buf);
    return std::string(r);
}

} // namespace

// --- %f, complete-buffer (normal magnitude) -------------------------------
TEST(I10E2E, FixedNormalMagnitudes) {
    CHECK_EQ(Fmt('f', 2, 12345.678L), std::string("12345.68"));
    CHECK_EQ(Fmt('f', 2, 3.14159L),   std::string("3.14"));
    CHECK_EQ(Fmt('f', 2, -42.5L),     std::string("-42.50"));
    CHECK_EQ(Fmt('f', 2, 100.0L),     std::string("100.00"));
    CHECK_EQ(Fmt('f', 0, 7.0L),       std::string("7"));
    CHECK_EQ(Fmt('f', 3, 2.5L),       std::string("2.500"));
    CHECK_EQ(Fmt('f', 6, 1.0L),       std::string("1.000000"));
}

// --- %f, MSVC half-away-from-zero rounding (distinct from glibc's half-even) ---
TEST(I10E2E, FixedRoundHalfAwayFromZero) {
    CHECK_EQ(Fmt('f', 2, 0.125L), std::string("0.13"));   // glibc: "0.12"
    CHECK_EQ(Fmt('f', 0, 2.5L),   std::string("3"));        // glibc: "2"
}

// --- %e / %E, MSVC 3-digit minimum exponent -------------------------------
TEST(I10E2E, ScientificLowercase) {
    CHECK_EQ(Fmt('e', 3, 12345.678L),  std::string("1.235e+004"));
    CHECK_EQ(Fmt('e', 3, 0.000123456L), std::string("1.235e-004"));
    CHECK_EQ(Fmt('e', 3, 6.022e23L),    std::string("6.022e+023"));
    CHECK_EQ(Fmt('e', 0, 5.0L),         std::string("5e+000"));
    CHECK_EQ(Fmt('e', 3, 1.0L),         std::string("1.000e+000"));
}

TEST(I10E2E, ScientificUppercaseAndSign) {
    CHECK_EQ(Fmt('E', 2, -42.5L), std::string("-4.25E+001"));
    CHECK_EQ(Fmt('e', 4, -1.5L),  std::string("-1.5000e+000"));
}

// --- %e carry rolls the exponent (9.9999 -> 1.000e+001) -------------------
TEST(I10E2E, ScientificCarryRollsExponent) {
    CHECK_EQ(Fmt('e', 3, 9.9999L),  std::string("1.000e+001"));
    CHECK_EQ(Fmt('e', 2, 999.5L),   std::string("1.00e+003"));
}

// --- non-finite -----------------------------------------------------------
TEST(I10E2E, NonFinite) {
    CHECK_EQ(Fmt('f', 2, std::nanl("")), std::string("nan"));
    CHECK_EQ(Fmt('e', 6, HUGE_VALL),     std::string("inf"));
    // The sign byte (*ctrl+20) is set BEFORE classification, so the driver still
    // prepends '-' for -inf, matching the original's field bookkeeping.
    CHECK_EQ(Fmt('g', 6, -HUGE_VALL),    std::string("-inf"));
}
