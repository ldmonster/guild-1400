#include "render/math_bigint96.h"

#include <cstring>

namespace guild::render {

// ===========================================================================
// 0x1426f14 — VIBE_Math_BigIntCopy96
//   do { dst[i] = src[i]; } while(--3);  returns advanced src cursor.
// The original walks `result` (= src) and writes through the (dst-src) delta,
// returning the post-increment src pointer.
// ===========================================================================
u32* BigIntCopy96(const u32* src, u32* dst) {
    const u32* result = src;
    int v3 = 3;
    do {
        dst[result - src] = *result;
        ++result;
        --v3;
    } while (v3);
    // The original returns the advanced `result` (src cursor). Cast away const
    // to match the _DWORD* return type; callers only ever use it as an end mark.
    return const_cast<u32*>(result);
}

// ===========================================================================
// 0x1426f2f — VIBE_Math_BigIntZero96
// ===========================================================================
i32 BigIntZero96(u32* a) {
    a[0] = 0;
    a[1] = 0;
    a[2] = 0;
    return 0;
}

// ===========================================================================
// 0x1426f3b — VIBE_Math_BigIntIsZero96
//   while(!*a){++count; ++a; if(count>=3) return 1;} return 0;
// ===========================================================================
i32 BigIntIsZero96(const u32* a) {
    int v2 = 0;
    while (!*a) {
        ++v2;
        ++a;
        if (v2 >= 3)
            return 1;
    }
    return 0;
}

// ===========================================================================
// 0x1428e2d — VIBE_Math_BigIntShiftLeft96  (in-place <<1, drop top)
//   v2 = a[1]; v3 = a[0];
//   a[0] = 2*a[0];
//   v4 = (v3 >> 31) | (2*v2);
//   v5 = a[2]; a[1] = v4;
//   a[2] = (v2 >> 31) | (2*v5);
// ===========================================================================
u32* BigIntShiftLeft96(u32* a) {
    u32 v2 = a[1];
    u32 v3 = a[0];
    a[0] = 2u * v3;
    u32 v4 = (v3 >> 31) | (2u * v2);
    u32 v5 = a[2];
    a[1] = v4;
    a[2] = (v2 >> 31) | (2u * v5);
    return a;
}

// ===========================================================================
// 0x1428e5b — VIBE_Math_BigIntShiftRightOne96  (in-place >>1)
//   v2 = a[2]; v3 = a[1];
//   a[1] = (int64 at &a[0]) >> 1;     // arithmetic >>1 of the low 64 bits,
//                                     //   store result back into a[1]
//   v4 = (v3 << 31) | (a[0] >> 1);
//   a[2] = v2 >> 1;
//   a[0] = v4;
// The decompiler's `a[1] = *(__int64*)(a+1) >> 1` reads the 64-bit word at
// a[1..2], arithmetic-shifts it right by one, and stores the LOW dword into
// a[1]. We reproduce that exactly.
// ===========================================================================
u32* BigIntShiftRightOne96(u32* a) {
    u32 v2 = a[2];
    u32 v3 = a[1];
    i64 hi64;
    std::memcpy(&hi64, &a[1], sizeof(hi64));   // signed 64-bit at a[1..2]
    i64 shifted = hi64 >> 1;                    // arithmetic shift
    a[1] = static_cast<u32>(shifted);           // low dword stored back into a[1]
    u32 v4 = (v3 << 31) | (a[0] >> 1);
    a[2] = v2 >> 1;
    a[0] = v4;
    return a;
}

// ===========================================================================
// 0x1426f56 — VIBE_Math_BigIntShiftRight96  (in-place >>n bits)
//   First pass: shift each dword right by (n%32), threading the bits that fall
//   off the bottom of dword i into the top of dword i+1.
//   Second pass: apply the whole-word skip of (n/32) words, zero-filling the
//   high end.
// Reproduced 1:1, including the original's odd eax-tracked return value.
// ===========================================================================
i32* BigIntShiftRight96(i32* a, i32 nbits) {
    i32* v2 = a;
    int v10 = 3;
    int v8 = nbits / 32;
    int v3 = nbits % 32;
    int v11 = 0;
    do {
        int v9 = ~(-1 << v3) & *v2;                                   // captured low bits
        *v2 = v11 | static_cast<i32>(static_cast<u32>(*v2) >> v3);    // logical >>n%32
        ++v2;
        bool last = (v10-- == 1);
        v11 = v9 << (32 - v3);
        if (last) break;
    } while (true);

    int v5 = 2;
    i32* result = a;
    for (int i = 2; i >= 0; --i) {
        if (v5 < v8) {
            result = a;
            a[i] = 0;
        } else {
            // a[i - v8] read as an int and re-stored; the original returns the
            // value treated as a pointer in eax, which we mirror as the address
            // computed by the indexed load. We only need numeric fidelity of a[].
            i32 moved = a[i - v8];
            a[i] = moved;
            result = reinterpret_cast<i32*>(static_cast<intptr_t>(moved));
        }
        --v5;
    }
    return result;
}

// ===========================================================================
// 0x14166f0 — VIBE_Math_CompareFloat
//   if (*a >  (double)*b) return  1;
//   if (*a >= (double)*b) return  0;
//   return -1;
// ===========================================================================
i32 CompareFloat(const float* a, const float* b) {
    double da = static_cast<double>(*a);
    double db = static_cast<double>(*b);
    if (da > db)
        return 1;
    if (da >= db)
        return 0;
    return -1;
}

// ===========================================================================
// 0x606890 — VIBE_Math_DoubleToFloatBits
// Integer-domain IEEE double -> float bit conversion with round-to-nearest-even
// and saturation, exactly as the original works over the 64-bit pattern:
//   if ((bits & 0x7FF0000000000000) == 0) return 0;          // exp == 0 -> 0
//   sign = (top bit of bits) << 31;
//   v2 = 2*bits + 0x20000000;                                // drop sign, add RNE bias
//   if (HIDWORD(v2)==0 || HIDWORD(v2) >= 0x8FE00000) return sign | 0x7F800000;  // inf
//   if (HIDWORD(v2) <  0x70200000) return 0;                 // underflow -> 0
//   HIDWORD(v2) -= 0x70000000;                               // re-bias exponent
//   return sign | (u32)(v2 >> 30);
// ===========================================================================
u32 DoubleToFloatBits(u64 doubleBits) {
    const u64 a1 = doubleBits;
    if ((a1 & 0x7FF0000000000000ULL) == 0)
        return 0;

    // carry out of (a1 + a1) is the top (sign) bit of a1.
    u32 carry = static_cast<u32>(a1 >> 63);
    u32 v1 = carry << 31;

    u64 v2 = 2ULL * a1 + 0x20000000ULL;
    u32 hi = static_cast<u32>(v2 >> 32);

    if (hi == 0 || hi >= 0x8FE00000u)
        return v1 | 0x7F800000u;
    if (hi < 0x70200000u)
        return 0;

    hi -= 0x70000000u;                       // 1879048192
    v2 = (static_cast<u64>(hi) << 32) | (v2 & 0xFFFFFFFFULL);
    return v1 | static_cast<u32>(v2 >> 30);
}

} // namespace guild::render
