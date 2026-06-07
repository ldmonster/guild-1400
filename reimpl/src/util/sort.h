#pragma once
#include <cstddef>

// CRT-style generic sort core (the MSVC qsort implementation, as compiled into
// gilde.exe). The original operates on raw byte arrays with an element width and
// a __cdecl comparator returning <0 / 0 / >0. We expose the same C signature.
//
// Functions:
//   guild::util::QuickSort      — gilde.exe 0x142176a — VIBE_Sort_QuickSort
//   guild::util::InsertionSort  — gilde.exe 0x14218be — VIBE_Sort_InsertionSort
//   guild::util::SwapElements   — gilde.exe 0x142190c — VIBE_Mem_SwapElements
//
// The comparator receives two element pointers (the original pushes both before
// each `call`, despite the Hex-Rays single-arg prototype; see disassembly at
// 0x142180d-0x142180f). It must implement a total order for the std::sort
// equivalence to hold; for a partial order the result is still a valid
// permutation but the order is implementation-defined (the original is unstable).
namespace guild::util {

// Comparator: returns negative if *a sorts before *b, positive if after, 0 if equal.
using SortCompare = int (*)(const void* a, const void* b);

// Swap two `width`-byte elements in place, byte by byte (no-op if a==b or width==0).
// Returns `a` (mirrors the original, which leaves the post-increment pointer in eax
// only when a copy happened; callers ignore the result).
void* SwapElements(void* a, void* b, std::size_t width);

// Selection-style "insertion" sort of the inclusive element range [lo, hi]
// (hi points at the LAST element, not one past). Used by QuickSort for runs <= 8.
void InsertionSort(void* lo, void* hi, std::size_t width, SortCompare cmp);

// In-place quicksort of `num` elements of `width` bytes starting at `base`,
// ordered by `cmp`. Median pivot + insertion-sort cutoff at 8, explicit stack
// (no recursion). Bit-for-bit the algorithm shipped in gilde.exe.
void QuickSort(void* base, std::size_t num, std::size_t width, SortCompare cmp);

// Comparator for BinarySearch: the original passes the SEARCH KEY (fixed) and the
// current mid ELEMENT, returning negative if the key sorts before the element,
// positive if after, 0 on a match. (In the binary it is a register __usercall
// with key@eax, elem@edx; here it is a normal __cdecl two-arg comparator.)
using SearchCompare = int (*)(const void* key, const void* elem);

// gilde.exe 0x5e9f70 — VIBE_Util_BinarySearch (__userpurge: key@eax, base@edx,
// width@ecx, num@ebx, cmp on stack). MSVC `bsearch`: returns a pointer to a
// matching element in the sorted `num`-element array at `base`, or nullptr. `cmp`
// is invoked as cmp(key, elem). When `cmp(key, mid) >= 0` the lower bound moves
// past mid, else the upper bound drops to mid; on collapse it does one final
// equality probe of the boundary element (mirrors the original tail check).
void* BinarySearch(const void* key, const void* base, std::size_t width,
                   std::size_t num, SearchCompare cmp);

} // namespace guild::util
