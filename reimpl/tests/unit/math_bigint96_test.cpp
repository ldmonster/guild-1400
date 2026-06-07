#include "test.h"

#include "render/math_bigint96.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// VIBE_Math_BigIntCopy96 / Zero96 / IsZero96
// ===========================================================================
TEST(BigInt96, CopyAndAdvanceCursor) {
    u32 src[3] = {0x11111111u, 0x22222222u, 0x33333333u};
    u32 dst[3] = {0, 0, 0};
    u32* ret = BigIntCopy96(src, dst);
    CHECK_EQ(dst[0], 0x11111111u);
    CHECK_EQ(dst[1], 0x22222222u);
    CHECK_EQ(dst[2], 0x33333333u);
    // original returns the advanced src cursor (src + 3).
    CHECK_EQ(ret, src + 3);
}

TEST(BigInt96, ZeroClearsAllThree) {
    u32 a[3] = {1, 2, 3};
    CHECK_EQ(BigIntZero96(a), 0);
    CHECK_EQ(a[0], 0u);
    CHECK_EQ(a[1], 0u);
    CHECK_EQ(a[2], 0u);
}

TEST(BigInt96, IsZeroPredicate) {
    u32 z[3] = {0, 0, 0};
    CHECK_EQ(BigIntIsZero96(z), 1);
    u32 a[3] = {0, 0, 1};      // nonzero high word
    CHECK_EQ(BigIntIsZero96(a), 0);
    u32 b[3] = {1, 0, 0};      // nonzero low word (short-circuits immediately)
    CHECK_EQ(BigIntIsZero96(b), 0);
    u32 c[3] = {0, 5, 0};      // nonzero mid word
    CHECK_EQ(BigIntIsZero96(c), 0);
}

// ===========================================================================
// VIBE_Math_BigIntShiftLeft96  (<<1 across 96 bits, top bit dropped)
// ===========================================================================
TEST(BigInt96, ShiftLeftCarryProp) {
    // 0x00000001_80000000_00000001  << 1 = 0x00000003_00000000_00000002
    u32 a[3] = {0x00000001u, 0x80000000u, 0x00000001u};   // [lo, mid, hi]
    u32* ret = BigIntShiftLeft96(a);
    CHECK_EQ(ret, a);
    CHECK_EQ(a[0], 0x00000002u);   // lo*2
    CHECK_EQ(a[1], 0x00000000u);   // mid<<1 drops its top, no carry in from lo
    CHECK_EQ(a[2], 0x00000003u);   // hi<<1 | carry from mid (1)
}

TEST(BigInt96, ShiftLeftFullCarryChain) {
    // 0x40000000_80000000_80000000 << 1 = 0x80000001_00000001_00000000
    u32 a[3] = {0x80000000u, 0x80000000u, 0x40000000u};
    BigIntShiftLeft96(a);
    CHECK_EQ(a[0], 0x00000000u);
    CHECK_EQ(a[1], 0x00000001u);   // carry from lo
    CHECK_EQ(a[2], 0x80000001u);   // hi<<1 | carry from mid
}

// ===========================================================================
// VIBE_Math_BigIntShiftRightOne96
//   low 64 bits arithmetic >>1, hi logical >>1.
// ===========================================================================
TEST(BigInt96, ShiftRightOneLogical) {
    // 0x00000002_00000000_00000004 >> 1 = 0x00000001_00000000_00000002
    u32 a[3] = {0x00000004u, 0x00000000u, 0x00000002u};
    u32* ret = BigIntShiftRightOne96(a);
    CHECK_EQ(ret, a);
    CHECK_EQ(a[0], 0x00000002u);
    CHECK_EQ(a[1], 0x00000000u);
    CHECK_EQ(a[2], 0x00000001u);
}

TEST(BigInt96, ShiftRightOneArithLow64) {
    // The original computes a[1] = (int64 at &a[1]) >> 1, where that int64 reads
    // a[1] (low dword) and a[2] (high dword): here = 0x00000000_80000000.
    // Arithmetic >>1 = 0x00000000_40000000; the LOW dword (0x40000000) is stored
    // back into a[1].
    u32 a[3] = {0x00000000u, 0x80000000u, 0x00000000u};
    BigIntShiftRightOne96(a);
    CHECK_EQ(a[1], 0x40000000u);
    // a[0] = (old a[1]<<31) | (old a[0]>>1) = (0x80000000<<31) | 0 = 0.
    CHECK_EQ(a[0], 0x00000000u);
    // a[2] = old a[2] >> 1 = 0.
    CHECK_EQ(a[2], 0x00000000u);
}

// ===========================================================================
// VIBE_Math_BigIntShiftRight96  (>>n bits with word skip)
// ===========================================================================
TEST(BigInt96, ShiftRightByBitsIntraWord) {
    // value 0x00000000_00000000_00000010 >> 4 = ..._00000001
    i32 a[3] = {0x10, 0, 0};
    BigIntShiftRight96(a, 4);
    CHECK_EQ(a[0], 0x1);
    CHECK_EQ(a[1], 0);
    CHECK_EQ(a[2], 0);
}

TEST(BigInt96, ShiftRightWordSkipFaithful) {
    // Faithful reconstruction of the original's two passes. With n=36:
    //   v8 = n/32 = 1, v3 = n%32 = 4.
    //   Pass 1 (>> v3 per word, threading low bits UP into the next word):
    //     a[0]=0>>4=0; a[1]=0x10>>4=0x1; a[2]=0>>4=0.
    //   Pass 2 (word move a[i] = a[i - v8], i = 2..0, with a[0]=0):
    //     a[2]=a[1]=0x1; a[1]=a[0]=0; a[0]=0.
    // This is exactly what gilde.exe's loop produces (verified against the
    // disassembly at 0x1426f56), not a textbook ">>36".
    i32 a[3] = {0, 0x10, 0};
    BigIntShiftRight96(a, 36);
    CHECK_EQ(a[0], 0);
    CHECK_EQ(a[1], 0);
    CHECK_EQ(a[2], 0x1);
}

// ===========================================================================
// VIBE_Math_CompareFloat
// ===========================================================================
TEST(MathLeaf, CompareFloatOrdering) {
    float a = 1.5f, b = 2.5f, c = 1.5f;
    CHECK_EQ(CompareFloat(&a, &b), -1);
    CHECK_EQ(CompareFloat(&b, &a), 1);
    CHECK_EQ(CompareFloat(&a, &c), 0);
    float neg = -3.0f, pos = 3.0f;
    CHECK_EQ(CompareFloat(&neg, &pos), -1);
}

// ===========================================================================
// VIBE_Math_DoubleToFloatBits  (IEEE double bits -> float bits, RNE)
// ===========================================================================
namespace {
u64 dbits(double d) { u64 b; std::memcpy(&b, &d, 8); return b; }
u32 fbits(float f)  { u32 b; std::memcpy(&b, &f, 4); return b; }
}

TEST(MathLeaf, DoubleToFloatExactValues) {
    // 1.0, 2.0, 0.5, -1.0, 3.5 are all exactly representable in float.
    CHECK_EQ(DoubleToFloatBits(dbits(1.0)),  fbits(1.0f));
    CHECK_EQ(DoubleToFloatBits(dbits(2.0)),  fbits(2.0f));
    CHECK_EQ(DoubleToFloatBits(dbits(0.5)),  fbits(0.5f));
    CHECK_EQ(DoubleToFloatBits(dbits(-1.0)), fbits(-1.0f));
    CHECK_EQ(DoubleToFloatBits(dbits(3.5)),  fbits(3.5f));
    CHECK_EQ(DoubleToFloatBits(dbits(-3.5)), fbits(-3.5f));
}

TEST(MathLeaf, DoubleToFloatZeroAndRounding) {
    // +0.0 has exponent field 0 -> returns 0 (the "(bits & 0x7FF0..)==0" path).
    CHECK_EQ(DoubleToFloatBits(dbits(0.0)), 0u);
    // 0.1 is not exactly representable; round-to-nearest must match the float.
    CHECK_EQ(DoubleToFloatBits(dbits(0.1)),  fbits(0.1f));
    CHECK_EQ(DoubleToFloatBits(dbits(3.14159265358979)), fbits(3.14159265358979f));
    // Large value -> +inf (overflow to inf in float).
    CHECK_EQ(DoubleToFloatBits(dbits(1e300)), 0x7F800000u);   // +inf float bits
}
