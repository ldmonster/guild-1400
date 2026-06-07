#include "util/sort.h"

namespace guild::util {

// gilde.exe 0x142190c — VIBE_Mem_SwapElements  (__cdecl(char*, char*, int))
void* SwapElements(void* a, void* b, std::size_t width) {
    char* p = static_cast<char*>(a);
    char* q = static_cast<char*>(b);
    if (p != q && width) {            // a1 != a2 && a3
        std::size_t n = width;
        do {
            char t = *p;
            *p++ = *q;
            *q++ = t;
            --n;
        } while (n);
    }
    return p;
}

// gilde.exe 0x14218be — VIBE_Sort_InsertionSort
//   a1 = lo, a2 = hi (last element), a3 = width, a4 = compare.
// For each position i from hi down to lo+width, find the max of [lo, i] and swap
// it into i. (max = element for which cmp(candidate, current_max) > 0.)
void InsertionSort(void* lo, void* hi, std::size_t width, SortCompare cmp) {
    char* base = static_cast<char*>(lo);
    char* last = static_cast<char*>(hi);
    for (char* i = last; i > base; i -= width) {
        char* max = base;                                  // v6 (ebx)
        for (char* j = base + width; j <= i; j += width) { // scan up to i
            if (cmp(j, max) > 0)                            // cmp(j, current max)
                max = j;
        }
        SwapElements(max, i, width);
    }
}

// gilde.exe 0x142176a — VIBE_Sort_QuickSort
//   a1 = base, a2 = num (element count), a3 = width, a4 = compare.
// MSVC CRT qsort: median-of-pivot partition with an explicit lo/hi stack and a
// cutoff to InsertionSort for runs of <= 8 elements. The original's two on-stack
// buffers hold up to 30 pending (lo,hi) pairs; std::vector here is an equivalent
// unbounded stack (behaviour-identical for any input the original could handle).
void QuickSort(void* base, std::size_t num, std::size_t width, SortCompare cmp) {
    if (num < 2 || width == 0) // a2 >= 2 && a3
        return;

    // Pending-range stack (lo pointers in one array, hi pointers in the other),
    // mirroring var_80 / var_F8 in the original frame.
    char* loStack[64];
    char* hiStack[64];
    int sp = 0; // var_4 (depth - 1)

    char* lo = static_cast<char*>(base);
    char* hi = lo + width * (num - 1); // &a1[a3*(a2-1)] — last element

    for (;;) {
        // Element count of [lo, hi] inclusive.
        std::size_t count = (std::size_t)(hi - lo) / width + 1;
        if (count <= 8) {
            InsertionSort(lo, hi, width, cmp);
        } else {
            // Move the median (mid element) to the front to use as pivot.
            char* mid = lo + width * (count >> 1);
            SwapElements(mid, lo, width);

            char* loScan = lo;     // var_8 / v16
            char* hiScan = hi + width;
            for (;;) {
                // Advance loScan while elements compare <= pivot (pivot at lo).
                do {
                    loScan += width;
                } while (loScan <= hi && cmp(loScan, lo) <= 0);
                // Retreat hiScan while elements compare >= pivot.
                do {
                    hiScan -= width;
                } while (hiScan > lo && cmp(hiScan, lo) >= 0);
                if (hiScan < loScan)
                    break;
                SwapElements(loScan, hiScan, width);
            }
            // Place pivot at its final position.
            SwapElements(lo, hiScan, width);

            // Compare the two partition halves by byte span (exactly as the
            // original, which works in raw byte offsets):
            //   left span  = (hiScan - lo) - 1   [bytes below the pivot]
            //   right span = (hi - loScan)       [bytes at/above loScan]
            // Process the SMALLER half immediately and push the larger to revisit.
            std::ptrdiff_t leftSpan = (hiScan - lo) - 1;
            std::ptrdiff_t rightSpan = hi - loScan;
            if (leftSpan >= rightSpan) {
                // Left half is the larger (or equal) -> push [lo, hiScan-width],
                // then continue on the right half [loScan, hi].
                if (lo + width < hiScan) {
                    loStack[sp] = lo;
                    hiStack[sp] = hiScan - width;
                    ++sp;
                }
                if (loScan >= hi)
                    goto pop;
                lo = loScan; // process right half
                continue;
            } else {
                // Right half is the larger -> push [loScan, hi], then continue
                // on the left half [lo, hiScan-width].
                if (loScan < hi) {
                    loStack[sp] = loScan;
                    hiStack[sp] = hi;
                    ++sp;
                }
                if (lo + width >= hiScan)
                    goto pop;
                hi = hiScan - width; // process left half
                continue;
            }
        }
    pop:
        --sp;
        if (sp < 0)
            return;
        lo = loStack[sp];
        hi = hiStack[sp];
    }
}

// gilde.exe 0x5e9f70 — VIBE_Util_BinarySearch
//   key@eax(ebp), base@edx(a1->ecx as lo), width@ecx(edi), num@ebx, cmp@[esp].
// Element pointers stay in raw bytes exactly like the original. `hi` (esi) is the
// LAST element, not one-past-the-end. The comparator is called as cmp(key, mid).
void* BinarySearch(const void* key, const void* base, std::size_t width,
                   std::size_t num, SearchCompare cmp) {
    if (!num)                                   // test ebx,ebx / jz -> return 0
        return nullptr;

    const char* lo = static_cast<const char*>(base);            // ecx
    const char* hi = lo + width * (num - 1);                    // esi = last elem

    if (lo < hi) {                              // cmp edx,esi / jnb skips the loop
        for (;;) {
            // mid = lo + width * (((hi - lo) / width) >> 1)
            std::size_t span = static_cast<std::size_t>(hi - lo) / width;
            const char* mid = lo + width * (span >> 1);         // ebx
            int v = cmp(key, mid);              // call cmp (key@eax, mid@edx)
            if (v == 0)
                return const_cast<char*>(mid);  // exact match
            if (v >= 0)
                lo = mid + width;               // search upper half
            else
                hi = mid;                       // search lower half
            if (lo >= hi)                       // cmp ecx,esi / jb continues
                break;
        }
    }

    // LABEL_9: final boundary probe — only when lo == hi exactly.
    if (lo == hi && cmp(key, hi) == 0)
        return const_cast<char*>(hi);
    return nullptr;
}

} // namespace guild::util
