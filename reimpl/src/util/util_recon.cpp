#include "util/util_recon.h"

#include <cstring>

namespace guild::util {

// ---------------------------------------------------------------------------
// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp (__usercall, eax=a@edx, b@eax)
//
// The original is the MSVC word-at-a-time strcmp: it loads 4 bytes at a time,
// compares the dwords, and uses the (~v4 & (v3 - 0x01010101) & 0x80808080)
// zero-byte test to detect a terminator before a mismatch. When the dwords
// differ it falls through to a per-byte comparison that yields the sign of the
// first differing byte, normalised to exactly -1 / +1. We reproduce the
// observable result: 0 on equal, -1 if a<b, +1 if a>b at the first differing
// unsigned byte (terminator counts as a byte). a==b (same pointer) returns 0.
// ---------------------------------------------------------------------------
int ReconStrCmp(const char* a, const char* b) {
    if (a == b)
        return 0; // 0x5d3f16: pointer identity short-circuit
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    while (true) {
        unsigned char ca = *pa;
        unsigned char cb = *pb;
        if (ca != cb) {
            // 0x5d3faa: result = -(ca < cb); LOBYTE |= 1  =>  -1 / +1
            return (ca < cb) ? -1 : 1;
        }
        if (ca == 0)
            return 0; // both terminated, equal
        ++pa;
        ++pb;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x43de48 — VIBE_Util_StrCopyToNormalBuf
//
// Copies *src into a fixed destination buffer (the writable global "normal_s"
// at 0x62d012). The original unrolls the copy two bytes at a time but is exactly
// strcpy(dst, *src). Returns 1.
// ---------------------------------------------------------------------------

// The original destination is a static buffer pre-seeded with "normal_s\0".
// 16 bytes is the size of the slack region after the literal in the binary; the
// engine never copies more than fits because all sources are short tokens, but
// we keep the byte-for-byte initial contents.
static char g_normalBuf[16] = {'n','o','r','m','a','l','_','s',0,0,0,0,0,0,0,0};

char* ReconNormalBuf() {
    return g_normalBuf;
}

int ReconStrCopyToNormalBuf(char* dst, char* const* src) {
    char* d = dst;            // 0x43de4a: edi = &normal_s
    const char* s = *src;     // 0x43de4f: esi = *a1
    // Unrolled two-at-a-time copy, terminating on the first NUL written.
    while (true) {
        char c0 = *s;         // 0x43de52
        *d = c0;              // 0x43de54
        if (c0 == 0)          // 0x43de58
            break;
        char c1 = s[1];       // 0x43de5a
        s += 2;               // 0x43de5d
        d[1] = c1;            // 0x43de60
        d += 2;               // 0x43de63
        if (c1 == 0)          // 0x43de68
            break;
    }
    return 1;                 // 0x43de6b: eax = 1
}

// ---------------------------------------------------------------------------
// gilde.exe 0x58f138 — VIBE_Util_ZeroStruct12
//   *(BYTE*)p = 0; *(DWORD*)(p+4) = 0; *(DWORD*)(p+8) = 0; return p;
// Note: only one byte is cleared at +0 (bytes +1..+3 are left untouched).
// ---------------------------------------------------------------------------
void* ReconZeroStruct12(void* p) {
    unsigned char* b = static_cast<unsigned char*>(p);
    b[0] = 0;                                          // 0x58f138
    std::memset(b + 4, 0, 4);                          // 0x58f13b: dword @ +4
    std::memset(b + 8, 0, 4);                          // 0x58f142: dword @ +8
    return p;                                          // 0x58f149
}

// ---------------------------------------------------------------------------
// gilde.exe 0x59211c — VIBE_Util_BubbleSortRecords
//
// Ascending bubble sort over `count` i32 ids. The original formats each id into
// a label via VIBE_Text_FormatItemLabel @0x59c2e4 and compares the labels with
// VIBE_Util_StrCmp; when StrCmp(label[j], label[i]) == 1 it swaps array[i] and
// array[j]. The label formatter is outside this cluster, so the comparison is
// injected through `cmp`; the default hook returns 0 (no swaps), leaving the
// array untouched until wired to the real formatter.
//
// Control flow mirrors the decompile: skipFlag != 0 -> no-op; otherwise the
// classic i in [0,count-1), j in (i,count) double loop.
// ---------------------------------------------------------------------------
void ReconBubbleSortRecords(i32* array, i32 count, u8 skipFlag,
                            ReconLabelCompare cmp, void* user) {
    if (skipFlag)              // 0x592136: if (a3) return
        return;
    int last = count - 1;      // 0x59213a: --result (count-1)
    for (int i = 0; i < last; ++i) {        // 0x592152: v11 < v9 (== count-1)
        int j = i + 1;                       // 0x592162: v3 = v11 + 1
        if (j < count) {                     // 0x592165: v11+1 < v14
            for (; j < count; ++j) {         // 0x592216: while v3 < v14
                int r;
                if (cmp)
                    r = cmp(array[i], array[j], user);
                else
                    r = 0;                   // inert default hook (no formatter)
                if (r == 1) {                // 0x5921ff: StrCmp == 1
                    i32 tmp = array[i];      // 0x592201..0x592207: swap
                    array[i] = array[j];
                    array[j] = tmp;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5ea040 — VIBE_Util_SortSwapElements
//
// Swap `width` bytes between two non-overlapping blocks. The original exchanges
// dword-at-a-time (using _InterlockedExchange) then a byte tail. For two
// distinct blocks this is a plain byte-wise swap.
// ---------------------------------------------------------------------------
void ReconSortSwapElements(std::size_t width, void* a, void* b) {
    unsigned char* pa = static_cast<unsigned char*>(a);
    unsigned char* pb = static_cast<unsigned char*>(b);
    for (std::size_t i = 0; i < width; ++i) {
        unsigned char t = pa[i];
        pa[i] = pb[i];
        pb[i] = t;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5e9ff0 — VIBE_Util_SortChoosePivot
//
// Median-of-three selector. Translated branch-for-branch from the DISASM (the
// Hex-Rays decompile collapses the comparator into bare v6()/v7()/v8() calls and
// loses which operands are compared — disasm is the reference of record here).
// Register map: eax=a(edi), edx=b(esi), ecx=cmp, ebx=c. The comparator is
// __usercall(eax=first, edx=second).
//   0x5e9ffb call cmp(a,b); jle ->else                       (cmp(a,b) > 0)
//   0x5ea005 call cmp(a,c); jle ->return a                   (cmp(a,c) > 0)
//   0x5ea00f call cmp(b,c); jle ->return c; else return b    (cmp(b,c) > 0)
// else (cmp(a,b) <= 0):
//   0x5ea01b call cmp(a,c); jl  ->second; else return a      (cmp(a,c) < 0)
//   0x5ea029 call cmp(b,c); jle ->return b; else return c    (cmp(b,c) > 0)
// ---------------------------------------------------------------------------
void* ReconSortChoosePivot(void* a, void* b, void* c, ReconSortCompare cmp) {
    if (cmp(a, b) > 0) {                 // 0x5e9fff: jle
        if (cmp(a, c) > 0) {             // 0x5ea009: jle -> return a
            if (cmp(b, c) > 0)           // 0x5ea013: jle -> return c
                return b;                // 0x5ea033 (esi)
            return c;                    // 0x5ea02f (ebx)
        }
        return a;                        // 0x5ea021 (edi)
    }
    if (cmp(a, c) < 0) {                 // 0x5ea01f: jl
        if (cmp(b, c) > 0)               // 0x5ea02d: jle -> return b
            return c;                    // 0x5ea02f (ebx)
        return b;                        // 0x5ea033 (esi)
    }
    return a;                            // 0x5ea021 (edi)
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5ea068 — VIBE_Util_QuickSort (the full MSVC qsort)
//
// Iterative quicksort with an explicit stack (32 entries), median-of-medians
// pivot selection for large runs, a three-way partition keeping equal elements
// adjacent to the pivot, and an insertion sort (implemented as a shrinking-gap
// pass with gaps 3*width, then 2*width) for runs shorter than 16 elements.
//
// Translated structurally from the decompile. Pointer-byte arithmetic is kept
// in bytes; element-count divisions by `width` are preserved. The original's
// inline dword/byte swaps are replaced with the equivalent block swap helper;
// for non-overlapping blocks the result is identical.
// ---------------------------------------------------------------------------
void ReconQuickSort(void* base, std::size_t num, ReconSortCompare cmp, std::size_t width) {
    if (width == 0)
        return;

    auto B = [](void* p) { return static_cast<char*>(p); };
    auto swap = [width](void* a, void* b) {
        if (a != b)
            ReconSortSwapElements(width, a, b);
    };

    // Explicit stack of (base, count) pairs (0x5ea0..: v36[0..31] bases,
    // v36[32..63] counts), depth 32.
    char*       stackLo[33];
    std::size_t stackCnt[33];
    int sp = 0;                                  // v42

    char* lo = B(base);                          // v48
    std::size_t n = num;                         // v45

    // Insertion-sort gaps (0x5ea0b0/0x5ea0c0): v40 = 2*width, v39 = 3*width.
    const std::size_t gap2 = 2 * width;          // v40
    const std::size_t gap3 = 3 * width;          // v39

    while (true) {
        while (true) {
            if (n <= 1)                          // 0x5ea0da
                goto pop;
            if (n < 0x10)                        // 0x5ea0e3: small run -> insertion
                break;

            // ---- median pivot selection (0x5ea21a..0x5ea2f7) ----
            char* mid = lo + width * (n >> 1);   // v11
            char* pivotA = lo;                   // v41
            char* pivotC = lo + width * (n - 1); // v12 (last element)
            if (n > 0x1D) {                      // 0x5ea21f
                if (n > 0x2A) {                  // 0x5ea245: medians of medians
                    std::size_t step = width * (n >> 3); // v44
                    pivotA = static_cast<char*>(
                        ReconSortChoosePivot(lo, lo + step, lo + 2 * step, cmp));
                    mid = static_cast<char*>(
                        ReconSortChoosePivot(mid - step, mid, mid + step, cmp));
                    pivotC = static_cast<char*>(
                        ReconSortChoosePivot(pivotC - 2 * step, pivotC - step, pivotC, cmp));
                }
                mid = static_cast<char*>(
                    ReconSortChoosePivot(pivotA, mid, pivotC, cmp));
            }

            // Move pivot to the front (0x5ea30b): the partition keeps the pivot
            // value at *lo and grows the "equal" region at [lo, loEq).
            swap(mid, lo);

            // Partition pointers (matching the decompile's register vars):
            //   loEq (v53) — boundary where elements == pivot accumulate at the
            //                low end; i (v13) — the low scan pointer.
            //   hi   (v54) — boundary where elements == pivot accumulate at the
            //                high end; hiEq (v55) — the high scan pointer.
            char* loEq = lo;                     // v53
            char* i    = lo;                     // v13
            char* hi   = lo + width * (n - 1);   // v54
            char* hiEq = hi;                     // v55
            std::size_t remaining = n;           // v56 / v14

            // ---- three-way partition (LABEL_29 .. 0x5ea4df) -----------------
            // The original advances the low scan one element at a time (an `if`
            // that restarts the loop with v56 = v14); functionally a while loop
            // that stops when the low scan finds an element > pivot.
            while (true) {
                // 0x5ea37f: scan i upward over elements <= pivot; equal ones get
                // swapped into the low-equal band at loEq.
                while (remaining) {
                    int c = cmp(i, lo);          // operand is the scan ptr v13
                    if (c > 0)                    // > pivot -> stop low scan
                        break;
                    if (c == 0) {                 // == pivot -> stash at low band
                        swap(i, loEq);
                        loEq += width;
                    }
                    --remaining;                  // v14 = v56 - 1
                    i += width;                   // v13 += a4
                }
                // 0x5ea3ec: scan hiEq downward over elements >= pivot; equal ones
                // get swapped into the high-equal band at hi.
                while (remaining) {
                    int c = cmp(hiEq, lo);        // operand is the scan ptr v55
                    if (c < 0)                    // < pivot -> stop high scan
                        break;
                    if (c == 0) {                 // == pivot -> stash at high band
                        swap(hi, hiEq);
                        hi -= width;              // v54 -= a4
                    }
                    hiEq -= width;                // v55 -= a4
                    --remaining;                  // --v56
                }
                if (remaining == 0)               // 0x5ea485
                    break;
                // i holds an element > pivot, hiEq holds one < pivot -> swap.
                swap(hiEq, i);                    // 0x5ea49c: swap(v55, v13)
                i += width;                       // 0x5ea4c0: v13 += a4
                --remaining;                      // 0x5ea4c2: --v56
                if (remaining == 0)               // 0x5ea4ca
                    break;
                --remaining;                      // 0x5ea4d8: v56 = v20 - 1
                hiEq -= width;                    // 0x5ea4df: v55 -= a4
            }

            // ---- fold the equal bands into the middle (0x5ea515..0x5ea5b8) ---
            char* end = lo + width * n;          // v43
            // Move low-equal band [lo, loEq) up against the partition point i.
            {
                std::size_t lenLowEq = static_cast<std::size_t>(loEq - lo);
                std::size_t lenLt    = static_cast<std::size_t>(i - loEq);
                std::size_t mv = lenLowEq < lenLt ? lenLowEq : lenLt;
                if (mv) {
                    char* src = lo;
                    char* dst = i - mv;
                    for (std::size_t k = 0; k < mv; ++k) {
                        char t = src[k]; src[k] = dst[k]; dst[k] = t;
                    }
                }
            }
            // Move high-equal band (hi, hiEq] down against the partition point.
            {
                std::size_t lenHiEq = static_cast<std::size_t>(hi - hiEq);
                std::size_t lenGt   = static_cast<std::size_t>(end - hi - width);
                std::size_t mv = lenHiEq < lenGt ? lenHiEq : lenGt;
                if (mv) {
                    char* src = i;
                    char* dst = end - mv;
                    for (std::size_t k = 0; k < mv; ++k) {
                        char t = src[k]; src[k] = dst[k]; dst[k] = t;
                    }
                }
            }

            // ---- recurse: push the larger side, loop on the smaller ----------
            std::size_t leftBytes  = static_cast<std::size_t>(i - loEq);          // v33
            std::size_t rightBytes = static_cast<std::size_t>(hi - hiEq);         // v34
            char* rightBase = end - rightBytes;                                   // v35
            if (rightBytes < leftBytes) {        // 0x5ea5e1
                if (leftBytes <= width)          // 0x5ea5fd
                    goto pop;
                stackLo[sp]  = lo;               // push left side
                stackCnt[sp] = leftBytes / width;
                n  = rightBytes / width;         // loop on right side
                lo = rightBase;
            } else {
                stackLo[sp]  = rightBase;        // push right side
                stackCnt[sp] = rightBytes / width;
                n  = leftBytes / width;          // loop on left side
                // lo unchanged
            }
            ++sp;
        }

        // ---- insertion sort for runs < 16 (0x5ea0e9..0x5ea1d0) -------------
        // Shrinking-gap pass: gap starts at 3*width, then 2*width.
        {
            char* end = lo + width * n;          // v46
            std::ptrdiff_t gap = static_cast<std::ptrdiff_t>(gap3); // v50
            while (gap > 0) {
                for (char* k = lo + gap; k < end; k += gap) {
                    for (char* m = k; m > lo; m -= gap) {
                        char* prev = m - gap;
                        if (cmp(prev, m) <= 0)   // 0x5ea193: stop when ordered
                            break;
                        swap(prev, m);
                    }
                }
                gap -= static_cast<std::ptrdiff_t>(gap2); // 0x5ea1c7
            }
        }

    pop:
        if (sp == 0)                             // 0x5ea1df
            return;
        --sp;                                    // 0x5ea1e5
        n  = stackCnt[sp];                       // 0x5ea1f2
        lo = stackLo[sp];                        // 0x5ea1f9
    }
}

} // namespace guild::util
