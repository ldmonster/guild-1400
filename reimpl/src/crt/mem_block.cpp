#include "crt/mem_block.h"

namespace guild::crt {

// gilde.exe 0x1421a20 — VIBE_Mem_Set (__cdecl(a1,a2,a3))
//   Aligns the head, fills dwords with (0x01010101 * byte), then the tail.
//   Observable behaviour is std::memset; the alignment/word steps are perf only.
void* MemSet(void* dst, u8 value, std::size_t n) {
    u8* p = static_cast<u8*>(dst);
    for (std::size_t i = 0; i < n; ++i)
        p[i] = value;
    return dst;
}

// gilde.exe 0x1422010 — VIBE_Mem_Compare (__cdecl(a1,a2,a3))
//   The original races a word-at-a-time fast path (when both pointers are 4-byte
//   aligned) and a 2-byte path otherwise, but always resolves the FIRST differing
//   byte and returns sign(a[i]-b[i]) ∈ {-1,0,+1} (via -v6-(v6-1)).
int MemCompare(const void* a, const void* b, std::size_t n) {
    const u8* pa = static_cast<const u8*>(a);
    const u8* pb = static_cast<const u8*>(b);
    for (std::size_t i = 0; i < n; ++i) {
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

// gilde.exe 0x1421fd0 — VIBE_Mem_CompareBounded (__cdecl(a1,a2,a3))
//   Pass 1: walk `a` up to `n` bytes or the first NUL -> run length L = a3 - v3.
//   Pass 2: compare `a`/`b` over L bytes; on the first difference return the sign
//   of the byte difference (a3 trick: ~0 == -1 for a>b, ~(-2)==1 for a<b). If the
//   whole bounded run matches, return the residual count v3 (0 when n was 0/empty).
int MemCompareBounded(const void* a, const void* b, int n) {
    const u8* pa = static_cast<const u8*>(a);
    const u8* pb = static_cast<const u8*>(b);
    int v3 = n;                       // residual after the NUL scan
    if (n == 0)
        return 0;
    // Pass 1: find the run length up to the first NUL within n bytes.
    {
        const u8* w = pa;
        while (v3) {
            bool isNul = (*w++ == 0);
            --v3;
            if (isNul)
                break;
        }
    }
    int v6 = n - v3;                  // v6 = a3 - v3 (bytes to compare, incl. NUL)
    const u8* va = pa;                // v7 = a1
    const u8* vb = pb;               // a2
    bool eq = true;                   // v5 (the loop condition)
    while (v6) {                      // do { if(!v6) break; ... } while(v5)
        eq = (*vb++ == *va++);
        --v6;
        if (!eq)
            break;
    }
    // The original reads the LAST bytes consumed: v9 = *(a2-1), cmp *(v7-1).
    u8 lastB = *(vb - 1);             // v9
    u8 lastA = *(va - 1);            // *(v7-1)
    if (lastB > lastA)
        return -1;                    // return ~0
    if (lastB != lastA)
        return 1;                     // return ~(-2)
    return 0;                         // bounded run matched -> v3 reset to 0
}

// gilde.exe 0x1421340 — VIBE_Mem_MoveOverlapping (__cdecl(a1=dst,a2=src,a3=n))
//   if (dst > src && dst < src+n) copy BACKWARD from the high end; else forward.
//   Both via dword-unrolled stores. Exactly std::memmove. Returns dst.
void* MemMoveOverlapping(void* dst, const void* src, std::size_t n) {
    auto a1 = reinterpret_cast<std::uintptr_t>(dst);
    auto a2 = reinterpret_cast<std::uintptr_t>(src);
    u8* d = static_cast<u8*>(dst);
    const u8* s = static_cast<const u8*>(src);
    if (a1 > a2 && a1 < a2 + n) {
        // overlap with dst inside [src, src+n): copy backward
        for (std::size_t i = n; i-- > 0;)
            d[i] = s[i];
    } else {
        for (std::size_t i = 0; i < n; ++i)
            d[i] = s[i];
    }
    return dst;
}

} // namespace guild::crt
