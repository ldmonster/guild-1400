#include "crt/int64.h"

namespace guild::crt {

// byte_64C198 @0x64c198 — recovered byte-for-byte (0x30..0x39,0x61..0x7a):
//   0x30 0x31 0x32 0x33 0x34 0x35 0x36 0x37 0x38 0x39  -> "0123456789"
//   0x61 0x62 ... 0x7a                                 -> "abc...xyz"
const char kDigit36Table[37] = "0123456789abcdefghijklmnopqrstuvwxyz";

// gilde.exe 0x1427b10 — VIBE_Math_UInt64Divide
// Original control flow: if the high dword of the divisor is zero, do a simple
// 64/32 division; otherwise normalise both operands right until the divisor fits
// in 32 bits, estimate the quotient with a 32-bit divide, then correct down by
// one if the estimate*divisor overflows or exceeds the dividend.
u32 UInt64Divide(u64 a, u64 b) {
    const u32 b_hi = static_cast<u32>(b >> 32);
    if (b_hi != 0) {
        u32 v4 = b_hi;          // ecx — shrinking high word of divisor
        u32 v5 = static_cast<u32>(b); // ebx — shrinking low word
        u64 v6 = a;             // dividend, shifted in lockstep
        do {
            u32 carry = v4 & 1;
            v4 >>= 1;
            v5 = (v5 >> 1) | (carry << 31);
            v6 >>= 1;
        } while (v4 != 0);
        u32 v8 = static_cast<u32>(v6 / v5);             // quotient estimate
        u64 v9 = static_cast<u64>(v8) * static_cast<u32>(b); // low product
        // Correct: if b_hi*v8 overflows into the carry, or the full product
        // exceeds the dividend, decrement.
        u64 full = static_cast<u64>(b) * v8;            // 64-bit b * v8
        // __CFADD__(b_hi*v8, HIDWORD(v9)) models the carry out of the high add.
        u32 hi_prod = b_hi * v8;
        bool carry_out = (hi_prod + static_cast<u32>(v9 >> 32)) < hi_prod;
        if (carry_out || full > a)
            --v8;
        return v8;
    }
    // Simple 64 / 32 path: replace the high word with (hi % b_lo) then divide.
    const u32 b_lo = static_cast<u32>(b);
    u64 v3 = static_cast<u32>(a);
    v3 |= static_cast<u64>(static_cast<u32>(a >> 32) % b_lo) << 32;
    return static_cast<u32>(v3 / b_lo);
}

// gilde.exe 0x1427b80 — VIBE_Math_UInt64Modulo
u64 UInt64Modulo(u64 a, u64 b) {
    const u32 b_hi = static_cast<u32>(b >> 32);
    if (b_hi != 0) {
        u32 v4 = b_hi;
        u32 v5 = static_cast<u32>(b);
        u64 v6 = a;
        do {
            u32 carry = v4 & 1;
            v4 >>= 1;
            v5 = (v5 >> 1) | (carry << 31);
            v6 >>= 1;
        } while (v4 != 0);
        u32 q = static_cast<u32>(v6 / v5);              // quotient estimate
        u64 v9 = b * q;                                 // q * divisor (64-bit)
        // The original recomputes the carry of (b_hi*q + HIDWORD(q*b_lo)); if it
        // overflows or v9 > a, subtract one divisor's worth.
        u32 hi_prod = b_hi * q;
        u64 lo_prod = static_cast<u64>(static_cast<u32>(b)) * q;
        bool carry_out = (hi_prod + static_cast<u32>(lo_prod >> 32)) < hi_prod;
        if (carry_out || v9 > a)
            v9 -= b;
        return a - v9;
    }
    const u32 b_lo = static_cast<u32>(b);
    u64 v2 = static_cast<u32>(a);
    v2 |= static_cast<u64>(static_cast<u32>(a >> 32) % b_lo) << 32;
    return v2 % b_lo;
}

// gilde.exe 0x5e57e7 — VIBE_Math_UnsignedLongLongDivide
// A bit-serial long division returning the 64-bit quotient. When the high half
// of the divisor is zero (the radix-conversion case used by UInt64ToString) it
// also exposes the remainder in the dividend's high word; otherwise the
// remainder fits in the low word. We compute both directly: native 64-bit
// division reproduces the same quotient/remainder the bit-serial loop yields.
u64 UnsignedLongLongDivide(u64 dividend, u32 divisor_hi, u32 divisor_lo,
                           u64* remainder) {
    const u64 divisor = (static_cast<u64>(divisor_hi) << 32) | divisor_lo;
    if (divisor == 0) {
        if (remainder) *remainder = 0;
        return 0;
    }
    u64 q = dividend / divisor;
    if (remainder) *remainder = dividend % divisor;
    return q;
}

// gilde.exe 0x609550 — VIBE_String_UInt64ToString
char* UInt64ToString(u64 value, char* dst, unsigned radix) {
    char tmp[65];                 // local digit stack (original &v10)
    char* p = tmp;
    u64 v = value;
    do {
        u64 rem = 0;
        // divmod by the radix (the radix high word is always 0 here).
        u64 q = UnsignedLongLongDivide(v, 0, radix, &rem);
        v = q;
        *p++ = kDigit36Table[static_cast<u32>(rem)];
    } while (v != 0);

    // Reverse the digit stack into dst, NUL-terminating (the original copies
    // until it writes the NUL sentinel it pushed).
    char* out = dst;
    do {
        char c = *--p;
        *out++ = c;
    } while (p != tmp);
    *out = '\0';
    return dst;
}

// gilde.exe 0x6095e8 — VIBE_String_Int64ToString
char* Int64ToString(i64 value, char* dst, unsigned radix) {
    char* out = dst;
    u64 mag;
    if (radix == 10 && value < 0) {
        *out++ = '-';
        // Two's-complement negate the 64-bit value (matches the ~hi / -lo +carry).
        mag = static_cast<u64>(-(value + 1)) + 1;
    } else {
        mag = static_cast<u64>(value);
    }
    UInt64ToString(mag, out, radix);
    return dst;
}

} // namespace guild::crt
