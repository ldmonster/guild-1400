#include "util/mem_ops.h"

namespace guild::util {

// gilde.exe 0x5d9310 — VIBE_Util_MemMove  (__usercall, eax=(dst@eax,src@edx,n@ebx))
//   if (src != dst):
//     if (src >= dst || src+n <= dst)   -> forward qmemcpy
//     else (overlap with src < dst)     -> backward copy, 2 bytes at a time then
//                                          the trailing odd byte.
//   Returns dst. This is exactly std::memmove; the direction split is the
//   overlap-correctness logic and the word steps are a perf detail.
void* MemMove(void* dst, const void* src, std::size_t n) {
    char* d = static_cast<char*>(dst);
    const char* s = static_cast<const char*>(src);
    if (s == d)                                   // a2 == result
        return dst;
    if (s >= d || s + n <= d) {                   // non-overlapping / safe-forward
        for (std::size_t i = 0; i < n; ++i)       // qmemcpy(result, a2, a3)
            d[i] = s[i];
    } else {                                      // overlap, src < dst: copy backward
        // The original walks two WORD-sized cursors from the high end:
        //   v3 = &src[n-2]; v4 = &dst[n-2]; for (n>>1) { *v4=*v3; v3-=2; v4-=2; }
        //   then one trailing byte if (n & 1).
        std::size_t i = n;
        while (i >= 2) {                           // word steps: read both, then write
            i -= 2;
            char b0 = s[i];                        // *(WORD*)v3 — read 2 bytes first
            char b1 = s[i + 1];
            d[i]     = b0;                          // *(WORD*)v4 = ...
            d[i + 1] = b1;
        }
        if (i)                                     // n & 1 trailing byte
            d[0] = s[0];
    }
    return dst;
}

// gilde.exe 0x1427370 — VIBE_Util_MemMove_27370  (__cdecl(uint dst, _BYTE* src, uint n))
//   if (dst > src && dst < src+n)  -> overlapping with dst above src: copy
//                                     BACKWARD (the unrolled dword-from-high path).
//   else                           -> copy FORWARD (aligned dword fast path +
//                                     unrolled tail).
//   Returns dst. The 8-way unrolling and dword stores are perf only; the
//   observable result is std::memmove.
void* MemMove2(void* dst, const void* src, std::size_t n) {
    char* d = static_cast<char*>(dst);
    const char* s = static_cast<const char*>(src);
    // Replicate the original's direction decision exactly: backward only when the
    // destination starts strictly inside (src, src+n).
    if (d > s && d < s + n) {                      // a1 > a2 && a1 < a2+a3
        std::size_t i = n;
        while (i--) {                              // copy from high to low
            d[i] = s[i];
        }
    } else {
        for (std::size_t i = 0; i < n; ++i)        // forward
            d[i] = s[i];
    }
    return dst;
}

} // namespace guild::util
