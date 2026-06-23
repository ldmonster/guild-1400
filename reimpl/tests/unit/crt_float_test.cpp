#include "test.h"

#include "crt/dtoa.h"
#include "crt/int64.h"
#include "crt/printf_float.h"
#include "crt/scanf.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace guild::crt;

namespace {

// Oracle: C snprintf for a given conversion.
std::string c_fmt(const char* spec, double v) {
    char b[128];
    std::snprintf(b, sizeof(b), spec, v);
    return std::string(b);
}

std::string our_fmt(int conv, double v, int prec) {
    char b[128];
    FormatDispatch(v, b, conv, prec);
    return std::string(b);
}

} // namespace

// ---- %f ----------------------------------------------------------------------
// NOTE: the original CRT rounds halves away from zero (MSVC behaviour), whereas
// glibc's printf rounds half-to-even. We therefore exclude values whose digit at
// the rounding boundary is an exact 0.5-ULP tie (e.g. 0.5, 0.25) from the
// byte-exact oracle below; the half-up behaviour itself is checked in
// FixedRoundsHalfUp.
TEST(CrtFloat, FixedMatchesC) {
    const double vals[] = {
        0.0, -0.0, 1.0, -1.0, 3.14159265358979,
        123.456, -123.456, 1000.0, 0.001,
        9999999.0, 1234567.891, 0.1, 0.3, 2.0/3.0,
        100000.0, 1e6, 12345.6789, -0.0001,
    };
    const int precs[] = {0, 1, 2, 3, 4, 6, 8};
    for (double v : vals) {
        for (int p : precs) {
            char spec[16];
            std::snprintf(spec, sizeof(spec), "%%.%df", p);
            std::string want = c_fmt(spec, v);
            std::string got = our_fmt('f', v, p);
            CHECK_EQ(got, want);
        }
    }
}

// The original CRT (MSVC _cfltcvt) rounds exact halves away from zero. Verify
// that documented behaviour directly (glibc would give the half-to-even result).
TEST(CrtFloat, FixedRoundsHalfUp) {
    char b[64];
    FormatFixed(0.5, b, 0);
    CHECK_EQ(std::string(b), std::string("1"));   // glibc: "0"
    FormatFixed(0.25, b, 1);
    CHECK_EQ(std::string(b), std::string("0.3")); // glibc: "0.2"
    FormatFixed(2.5, b, 0);
    CHECK_EQ(std::string(b), std::string("3"));   // glibc: "2"
}

// ---- %e ----------------------------------------------------------------------
TEST(CrtFloat, ExponentialMatchesC) {
    const double vals[] = {
        0.0, 1.0, -1.0, 3.14159265358979, 123.456, -123.456,
        0.001, 1e6, 1e-6, 6.022e23, 1.6e-19, 9.999e9,
        0.1, 1234567.891, 2.0/3.0, 5e-300, 1e300,
    };
    const int precs[] = {0, 1, 2, 3, 6, 9};
    for (double v : vals) {
        for (int p : precs) {
            char spec[16];
            std::snprintf(spec, sizeof(spec), "%%.%de", p);
            std::string want = c_fmt(spec, v);
            std::string got = our_fmt('e', v, p);
            CHECK_EQ(got, want);
        }
    }
}

TEST(CrtFloat, ExponentialUpperMatchesC) {
    const double vals[] = {1.5e10, 2.5e-5, 0.0, -7.25e3};
    for (double v : vals) {
        char spec[8];
        std::snprintf(spec, sizeof(spec), "%%.3E");
        std::string want = c_fmt(spec, v);
        std::string got = our_fmt('E', v, 3);
        CHECK_EQ(got, want);
    }
}

// ---- %g ----------------------------------------------------------------------
TEST(CrtFloat, GeneralMatchesC) {
    const double vals[] = {
        0.0, 1.0, -1.0, 100.0, 100000.0, 1000000.0, 0.0001, 0.00001,
        3.14159265358979, 123456.0, 1234567.0, 0.1, 0.5, 2.0/3.0,
        1e-5, 9.999999e6, 42.0, -0.000123, 7.0,
    };
    const int precs[] = {1, 2, 3, 6, 9};
    for (double v : vals) {
        for (int p : precs) {
            char spec[16];
            std::snprintf(spec, sizeof(spec), "%%.%dg", p);
            std::string want = c_fmt(spec, v);
            std::string got = our_fmt('g', v, p);
            CHECK_EQ(got, want);
        }
    }
}

// ---- dtoa digit core ---------------------------------------------------------
TEST(CrtFloat, StrFltDigits) {
    StrFlt sf;
    DoubleToStrFlt(0.0, &sf);
    CHECK_EQ(std::string(sf.mantissa), std::string("0"));
    CHECK_EQ(sf.decpt, 1);
    CHECK(sf.sign == ' ');

    DoubleToStrFlt(-12.5, &sf);
    CHECK(sf.sign == '-');
    CHECK_EQ(sf.decpt, 2);                 // 1.25 x 10^1
    CHECK(std::strncmp(sf.mantissa, "125", 3) == 0);

    DoubleToStrFlt(0.001, &sf);
    CHECK_EQ(sf.decpt, -2);                // 1.0 x 10^-3 -> decpt = -2
}

// ---- 64-bit divide / modulo --------------------------------------------------
TEST(CrtFloat, UInt64DivMod) {
    struct { std::uint64_t a, b; } cases[] = {
        {0, 1}, {100, 7}, {0xFFFFFFFFFFFFFFFFull, 3},
        {0x123456789ABCDEFull, 0x1000}, {1ull << 40, 1ull << 10},
        {0xFFFFFFFFull, 0xFFFFull}, {12345678901234ull, 1000000ull},
        {0xDEADBEEFCAFEull, 0x10001ull}, {1000000007ull * 1000000009ull, 1000000007ull},
    };
    for (auto& c : cases) {
        std::uint64_t q = c.a / c.b;
        std::uint64_t r = c.a % c.b;
        // UInt64Divide returns the low 32 bits of the quotient.
        CHECK_EQ(UInt64Divide(c.a, c.b), static_cast<std::uint32_t>(q));
        CHECK_EQ(UInt64Modulo(c.a, c.b), r);
    }
}

// ---- int64 -> string ---------------------------------------------------------
TEST(CrtFloat, Int64ToStringMatchesC) {
    struct { std::int64_t v; unsigned radix; } cases[] = {
        {0, 10}, {1, 10}, {-1, 10}, {123456789012345ll, 10},
        {-123456789012345ll, 10}, {255, 16}, {0xABCDEF, 16},
        {0, 16}, {64, 8}, {35, 36}, {1295, 36},
        {INT64_MIN, 10}, {INT64_MAX, 10},
    };
    for (auto& c : cases) {
        char got[80];
        Int64ToString(c.v, got, c.radix);
        char want[80];
        if (c.radix == 10) std::snprintf(want, sizeof(want), "%lld", (long long)c.v);
        else if (c.radix == 16) std::snprintf(want, sizeof(want), "%llx", (unsigned long long)(std::uint64_t)c.v);
        else if (c.radix == 8)  std::snprintf(want, sizeof(want), "%llo", (unsigned long long)(std::uint64_t)c.v);
        else {
            // base 36 via reference
            std::uint64_t u = (std::uint64_t)c.v;
            char tmp[80]; int n = 0;
            do { int d = u % 36; tmp[n++] = d < 10 ? '0'+d : 'a'+d-10; u /= 36; } while (u);
            for (int i = 0; i < n; ++i) want[i] = tmp[n-1-i];
            want[n] = '\0';
        }
        CHECK_EQ(std::string(got), std::string(want));
    }
}

TEST(CrtFloat, UInt64ToStringMatchesC) {
    std::uint64_t vals[] = {0, 1, 9, 10, 0xFFFFFFFFFFFFFFFFull, 18446744073709551615ull};
    for (auto v : vals) {
        char got[80];
        UInt64ToString(v, got, 10);
        char want[80];
        std::snprintf(want, sizeof(want), "%llu", (unsigned long long)v);
        CHECK_EQ(std::string(got), std::string(want));
    }
}

// ---- scanf -------------------------------------------------------------------
TEST(CrtFloat, ScanIntegers) {
    int a = 0, b = 0, n;
    n = Sscanf("  42 -7", "%d %d", &a, &b);
    CHECK_EQ(n, 2);
    CHECK_EQ(a, 42);
    CHECK_EQ(b, -7);

    unsigned u = 0;
    n = Sscanf("0xFF", "%x", &u);
    CHECK_EQ(n, 1);
    CHECK_EQ(u, 255u);

    int o = 0;
    n = Sscanf("0755", "%o", &o);
    CHECK_EQ(n, 1);
    CHECK_EQ(o, 0755);

    int i1 = 0, i2 = 0, i3 = 0;
    n = Sscanf("10 0x10 010", "%i %i %i", &i1, &i2, &i3);
    CHECK_EQ(n, 3);
    CHECK_EQ(i1, 10);
    CHECK_EQ(i2, 16);
    CHECK_EQ(i3, 8);
}

TEST(CrtFloat, ScanFloatsAndStrings) {
    double d = 0; float f = 0;
    int n = Sscanf("3.14159", "%lf", &d);
    CHECK_EQ(n, 1);
    CHECK(std::fabs(d - 3.14159) < 1e-9);

    n = Sscanf("-2.5e3", "%f", &f);
    CHECK_EQ(n, 1);
    CHECK(std::fabs(f - (-2500.0f)) < 1e-3f);

    char word[32] = {0};
    n = Sscanf("  hello world", "%s", word);
    CHECK_EQ(n, 1);
    CHECK_EQ(std::string(word), std::string("hello"));

    char a[8] = {0}, b[8] = {0};
    n = Sscanf("foo,bar", "%[^,],%[^\n]", a, b);
    CHECK_EQ(n, 2);
    CHECK_EQ(std::string(a), std::string("foo"));
    CHECK_EQ(std::string(b), std::string("bar"));
}

TEST(CrtFloat, ScanSuppressWidthCountAnd64) {
    // Assignment suppression with '*'.
    int a = -1, b = -1;
    int n = Sscanf("12 34 56", "%*d %d %d", &a, &b);
    CHECK_EQ(n, 2);
    CHECK_EQ(a, 34);
    CHECK_EQ(b, 56);

    // Field width limits the integer.
    int w = 0;
    n = Sscanf("123456", "%3d", &w);
    CHECK_EQ(n, 1);
    CHECK_EQ(w, 123);

    // %n reports the number of characters consumed so far.
    int x = 0, cnt = 0;
    n = Sscanf("789abc", "%d%n", &x, &cnt);
    CHECK_EQ(x, 789);
    CHECK_EQ(cnt, 3);

    // 64-bit integer via I64.
    std::uint64_t big = 0;
    n = Sscanf("12345678901234", "%I64d", &big);
    CHECK_EQ(n, 1);
    CHECK_EQ(big, 12345678901234ull);

    // Hex 64-bit.
    std::uint64_t hx = 0;
    n = Sscanf("FFFFFFFFFF", "%I64x", &hx);
    CHECK_EQ(n, 1);
    CHECK_EQ(hx, 0xFFFFFFFFFFull);
}

TEST(CrtFloat, ScanMatchesCSscanf) {
    const char* inputs[] = {
        "123 456", "  -10   20", "3.14 2.71", "0xABC 17",
        "hello 42", "1.5e-3 done",
    };
    const char* fmts[] = {
        "%d %d", "%d %d", "%lf %lf", "%x %d",
        "%s %d", "%lf %s",
    };
    for (int k = 0; k < 6; ++k) {
        // integer/double combos: compare assignment counts against C sscanf.
        if (k == 0 || k == 1 || k == 3) {
            int a1 = 0, a2 = 0, b1 = 0, b2 = 0;
            int rc1 = std::sscanf(inputs[k], fmts[k], &a1, &a2);
            int rc2 = Sscanf(inputs[k], fmts[k], &b1, &b2);
            CHECK_EQ(rc1, rc2);
            CHECK_EQ(a1, b1);
            CHECK_EQ(a2, b2);
        } else if (k == 2) {
            double a1=0,a2=0,b1=0,b2=0;
            int rc1 = std::sscanf(inputs[k], fmts[k], &a1, &a2);
            int rc2 = Sscanf(inputs[k], fmts[k], &b1, &b2);
            CHECK_EQ(rc1, rc2);
            CHECK(std::fabs(a1 - b1) < 1e-12);
            CHECK(std::fabs(a2 - b2) < 1e-12);
        }
    }
}

// ---------------------------------------------------------------------------
// Wave-11 hardening: ReadFloat collected the numeric token into a fixed 128-byte
// stack buffer with no bound — an unbounded "%f" of a very long digit run blew
// the stack. The collect is now capped; this must run clean under ASAN and the
// value of a 200-zero-then-something token must still convert sensibly.
// ---------------------------------------------------------------------------
TEST(CrtFloat, ScanFloatOverlongTokenNoOverflow) {
    std::string big(300, '1'); // 300 '1' chars — far past the 128-byte buffer
    big += ".5";
    double d = -1.0;
    int n = Sscanf(big.c_str(), "%lf", &d);
    CHECK_EQ(n, 1);
    CHECK(d > 0.0); // converted (value is ~1.111e299 -> inf is acceptable too)
}

// Width-bounded float read of a long input: only `width` chars consumed; no
// overflow either way.
TEST(CrtFloat, ScanFloatWidthBounded) {
    double d = 0.0;
    int n = Sscanf("123456789.0", "%5lf", &d);
    CHECK_EQ(n, 1);
    CHECK(std::fabs(d - 12345.0) < 1e-9);
}

// Integer scan with an overlong digit run (200 nines): the 2's-complement wrap
// must not trip signed-overflow UB.
TEST(CrtFloat, ScanIntegerOverlongNoUB) {
    std::string big(200, '9');
    int v = 0;
    int n = Sscanf(big.c_str(), "%d", &v);
    CHECK_EQ(n, 1);
    volatile int sink = v; // value is the low-32-bit wrap; just no crash/UB
    (void)sink;
}

// INT_MIN integer scan (the safe-negate path in ReadInteger).
TEST(CrtFloat, ScanIntMin) {
    int v = 0;
    int n = Sscanf("-2147483648", "%d", &v);
    CHECK_EQ(n, 1);
    CHECK_EQ(v, -2147483647 - 1);
}

// Empty input to sscanf: no conversions, returns 0 (EOF-before-any-match).
TEST(CrtFloat, ScanEmptyInput) {
    int v = 7;
    int n = Sscanf("", "%d", &v);
    CHECK(n <= 0);   // EOF: 0 or -1
    CHECK_EQ(v, 7);  // untouched
}
