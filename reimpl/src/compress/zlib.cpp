#include "compress/zlib.h"

namespace guild::compress {

namespace {
constexpr u32 kBase = 65521u; // largest prime smaller than 65536
constexpr u32 kNMax = 5552u;  // largest n with 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1
} // namespace

// VIBE_Zlib_Adler32 @0x5ffaf0 — __usercall(eax=adler, edx=data, ebx=len).
// The binary unrolls the per-block loop by 16 (DO16). The result is identical
// to a plain byte loop, so the readable form is used; the NMAX blocking and the
// modulo placement are preserved exactly (sums are reduced once per <=5552-byte
// block, never inside the unrolled body — relying on 32-bit accumulation).
u32 Adler32(u32 adler, const u8* data, u32 len) {
    if (!data)
        return 1;
    u32 s1 = adler & 0xFFFFu;
    u32 s2 = (adler >> 16) & 0xFFFFu;
    while (len) {
        u32 k = (len < kNMax) ? len : kNMax; // a3 >= 0x15B0 -> 5552
        len -= k;
        while (k) {
            s1 += *data++;
            s2 += s1;
            --k;
        }
        s1 %= kBase;
        s2 %= kBase;
    }
    return s1 | (s2 << 16);
}

} // namespace guild::compress
