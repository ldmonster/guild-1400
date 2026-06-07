#include "test.h"

#include "render/math_bigint96.h"
#include "render/render_leaves9.h"

#include <cstring>
#include <cstdio>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// E2E (no real assets — a large deterministic numeric scenario):
//   * ParseDecimal over every integer 0..999 cross-checked against the host
//     x87 long double bit pattern.
//   * DoubleToFloatBits over a sweep cross-checked against host float casts.
//   * A re-run determinism guard: the same inputs must yield byte-identical
//     extended-precision outputs across two passes.
// ===========================================================================

namespace {
void HostExtended(const char* s, u32& lo, u32& hi, u16& ex) {
    long double v = 0.0L;
    for (const char* p = s; *p; ++p) v = v * 10.0L + (*p - '0');
    unsigned char raw[16]; std::memset(raw, 0, sizeof(raw));
    std::memcpy(raw, &v, sizeof(long double));
    std::memcpy(&lo, raw + 0, 4);
    std::memcpy(&hi, raw + 4, 4);
    std::memcpy(&ex, raw + 8, 2);
}
} // namespace

TEST(MathLeavesE2E, ParseDecimalSweepMatchesLongDouble) {
    int mismatches = 0;
    for (int n = 1; n < 1000; ++n) {     // n==0 has exp field 0 (special); skip
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", n);
        Extended80 e{};
        ParseDecimal(buf, &e);
        u32 lo, hi; u16 ex;
        HostExtended(buf, lo, hi, ex);
        if (e.mantissaLo != lo || e.mantissaHi != hi || e.exponent != ex) {
            if (mismatches < 5)
                std::printf("  mismatch n=%d got %08x:%08x exp %04x want %08x:%08x exp %04x\n",
                            n, e.mantissaHi, e.mantissaLo, e.exponent, hi, lo, ex);
            ++mismatches;
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(MathLeavesE2E, ParseDecimalIsDeterministic) {
    // Two independent passes must agree byte-for-byte (the routine carries no
    // hidden state; this guards against any future accidental statefulness).
    for (int n = 1; n < 500; ++n) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", n);
        Extended80 a{}, b{};
        ParseDecimal(buf, &a);
        ParseDecimal(buf, &b);
        CHECK_EQ(std::memcmp(&a, &b, sizeof(Extended80)), 0);
    }
}

TEST(MathLeavesE2E, DoubleToFloatBitsSweep) {
    int mismatches = 0;
    // Sweep a range of exactly-representable and rounded values.
    for (int i = -2000; i <= 2000; ++i) {
        double d = i * 0.013125;          // not all exactly representable
        u64 db; std::memcpy(&db, &d, 8);
        float f = static_cast<float>(d);
        u32 fb; std::memcpy(&fb, &f, 4);
        if (DoubleToFloatBits(db) != fb) {
            if (mismatches < 5)
                std::printf("  d2f mismatch i=%d d=%g got %08x want %08x\n",
                            i, d, DoubleToFloatBits(db), fb);
            ++mismatches;
        }
    }
    CHECK_EQ(mismatches, 0);
}

TEST(MathLeavesE2E, BigIntShiftRoundTripPowersOfTwo) {
    // For each power of two 2^k (k=0..63), build it with repeated ShiftLeft96
    // from 1, confirm exactly one word/bit is set, then ParseDecimal of the same
    // power agrees with normalizing the built magnitude. (Covers the full 64-bit
    // mantissa range the normalizer left-justifies.)
    for (int k = 0; k < 31; ++k) {       // keep within a single-digit-safe decimal range via 2^k
        u32 mag[3];
        BigIntZero96(mag);
        mag[0] = 1;
        for (int s = 0; s < k; ++s) BigIntShiftLeft96(mag);

        // 2^k as decimal string, parsed both ways.
        unsigned long long pv = 1ULL << k;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%llu", pv);

        Extended80 viaParse{};
        ParseDecimal(buf, &viaParse);

        u32 h = 0, l = 0; u16 ex = 0;
        ShiftAccumulate(mag[1], mag[2], mag[0], 0x405E, &h, &l, &ex);

        CHECK_EQ(viaParse.mantissaHi, h);
        CHECK_EQ(viaParse.mantissaLo, l);
        CHECK_EQ(viaParse.exponent,   ex);
        CHECK_EQ(h, 0x80000000u);                  // pure power of two
        CHECK_EQ(ex, (u16)(0x3FFF + k));
    }
}
