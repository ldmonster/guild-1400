#include "test.h"

#include "render/math_bigint96.h"
#include "render/render_leaves9.h"

#include <cstring>
#include <cstdint>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// Integration: compose the 96-bit big-integer primitives with the 80-bit
// extended-precision normalizer. This mirrors how the original dtoa/strtod
// support layer threads these leaves together: build a magnitude with the
// shift/copy/zero helpers, then hand it to ShiftAccumulate / ParseDecimal.
// ===========================================================================

namespace {
// Convenience: run ShiftAccumulate over a (lo,mid,hi) magnitude.
Extended80 Normalize(u32 lo, u32 mid, u32 hi, i32 exp) {
    Extended80 e{};
    u32 h = 0, l = 0; u16 ex = 0;
    ShiftAccumulate(mid, hi, lo, exp, &h, &l, &ex);
    e.mantissaHi = h; e.mantissaLo = l; e.exponent = ex;
    return e;
}
} // namespace

// Build "8" two ways: (a) ParseDecimal("8"); (b) BigIntZero96 + set lo=1 then
// three BigIntShiftLeft96 (1<<3 = 8) then Normalize with the same base exponent.
// Both must yield the identical 80-bit pattern (significand 0x80000000_00000000,
// exponent 0x4002 = 0x3FFF + 3).
TEST(MathLeavesPipeline, ShiftLeftChainMatchesParseDecimal) {
    Extended80 viaParse{};
    ParseDecimal("8", &viaParse);

    u32 mag[3];
    BigIntZero96(mag);
    mag[0] = 1;
    BigIntShiftLeft96(mag);   // 2
    BigIntShiftLeft96(mag);   // 4
    BigIntShiftLeft96(mag);   // 8
    CHECK_EQ(mag[0], 8u);
    CHECK_EQ(BigIntIsZero96(mag), 0);

    Extended80 viaShift = Normalize(mag[0], mag[1], mag[2], 0x405E);

    CHECK_EQ(viaShift.mantissaHi, viaParse.mantissaHi);
    CHECK_EQ(viaShift.mantissaLo, viaParse.mantissaLo);
    CHECK_EQ(viaShift.exponent,   viaParse.exponent);
    CHECK_EQ(viaShift.mantissaHi, 0x80000000u);
    CHECK_EQ(viaShift.exponent,   (u16)0x4002);
}

// Copy a magnitude, mutate the copy, prove the source is independent, then
// confirm both normalize to their respective expected values.
TEST(MathLeavesPipeline, CopyThenIndependentNormalize) {
    u32 src[3] = {6, 0, 0};            // value 6 -> 1.5 * 2^2 -> exp 0x4001
    u32 dst[3] = {0, 0, 0};
    BigIntCopy96(src, dst);
    BigIntShiftLeft96(dst);            // dst = 12 -> 1.5 * 2^3 -> exp 0x4002

    Extended80 a = Normalize(src[0], src[1], src[2], 0x405E);
    Extended80 b = Normalize(dst[0], dst[1], dst[2], 0x405E);

    CHECK_EQ(a.mantissaHi, 0xC0000000u);   // 1.5
    CHECK_EQ(a.exponent,   (u16)0x4001);
    CHECK_EQ(b.mantissaHi, 0xC0000000u);   // 1.5
    CHECK_EQ(b.exponent,   (u16)0x4002);   // one binary order higher
    CHECK_EQ(src[0], 6u);                  // source untouched by dst mutation
}

// Cross-check ParseDecimal against the host x87 long double for several values.
TEST(MathLeavesPipeline, ParseDecimalMatchesLongDouble) {
    const char* cases[] = {"1", "2", "3", "4", "5", "6", "7", "8",
                           "10", "16", "100", "255", "1000"};
    for (const char* s : cases) {
        Extended80 e{};
        ParseDecimal(s, &e);

        long double v = 0.0L;
        for (const char* p = s; *p; ++p) v = v * 10.0L + (*p - '0');

        unsigned char raw[16]; std::memset(raw, 0, sizeof(raw));
        std::memcpy(raw, &v, sizeof(long double));
        u32 expLo, expHi; u16 expEx;
        std::memcpy(&expLo, raw + 0, 4);
        std::memcpy(&expHi, raw + 4, 4);
        std::memcpy(&expEx, raw + 8, 2);

        CHECK_EQ(e.mantissaLo, expLo);
        CHECK_EQ(e.mantissaHi, expHi);
        CHECK_EQ(e.exponent,   expEx);
    }
}

// DoubleToFloatBits used as a "narrowing pipeline" stage: take a long-double-
// parsed integer's double value and confirm the float-bit conversion matches a
// host float cast for exactly-representable integers.
TEST(MathLeavesPipeline, DoubleToFloatBitsThroughParsedValues) {
    for (int n : {1, 2, 4, 8, 16, 100, 256, 1024}) {
        double d = static_cast<double>(n);
        u64 db; std::memcpy(&db, &d, 8);
        float f = static_cast<float>(n);
        u32 fb; std::memcpy(&fb, &f, 4);
        CHECK_EQ(DoubleToFloatBits(db), fb);
    }
}
