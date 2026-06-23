#pragma once
#include "guild/common/types.h"
#include <cstddef>

// guild::util — additional hand-written utility leaves recovered from gilde.exe,
// reconstructed 1:1 from the Hex-Rays decompile (the reference of record).
//
// Scope of THIS translation unit (cluster VIBE_Util):
//   * StrCmp                @0x5d3f10  — word-at-a-time strcmp returning -1/0/+1
//   * StrCopyToNormalBuf    @0x43de48  — copy a C-string into the module "normal_s" buffer
//   * ZeroStruct12          @0x58f138  — zero a 12-byte record (1 byte + two dwords)
//   * BubbleSortRecords     @0x59211c  — stable-ish bubble sort of i32 ids by formatted label
//   * SortChoosePivot       @0x5e9ff0  — median-of-3/medians pivot selector (MSVC qsort core)
//   * SortSwapElements      @0x5ea040  — width-byte block swap (dword + byte tail)
//   * QuickSort             @0x5ea068  — the full MSVC qsort (median pivot, insertion cutoff)
//
// All entries are OS-free leaves. Integer wraparound, edge cases, and the exact
// return-value normalisation are preserved. See util_recon.cpp for the per-line
// mapping to the original pseudocode/disassembly.
//
// NOTE on ODR: this file deliberately does NOT redefine helpers already present
// elsewhere in src/ (StrCmpNoCase @0x5cb8f0 lives in util/string_ops.cpp; the CRT
// rand state lives in crt/rand.cpp; the other qsort variant @0x142176a lives in
// util/sort.cpp). The names here are unique to avoid collisions.
namespace guild::util {

// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp (__usercall, eax = (a@eax, b@edx)).
// MSVC word-at-a-time strcmp. Returns 0 when equal, otherwise the sign of the
// first differing byte normalised to exactly -1 / +1 (NOT the raw byte
// difference). Comparing a pointer with itself returns 0 immediately.
int ReconStrCmp(const char* a, const char* b);

// gilde.exe 0x43de48 — VIBE_Util_StrCopyToNormalBuf (__usercall, eax = &src).
// Copies the NUL-terminated string *src into the module-level "normal_s" buffer
// (the original writes to the writable global at 0x62d012, pre-seeded with the
// literal "normal_s"). The copy is unrolled two bytes at a time but is
// byte-for-byte equivalent to strcpy(dst, *src). Always returns 1.
// `dst` lets callers/tests supply the destination explicitly; the live engine
// uses ReconNormalBuf().
int ReconStrCopyToNormalBuf(char* dst, char* const* src);

// The module-level "normal_s" buffer (original global @0x62d012). Pre-seeded with
// "normal_s" exactly like the binary's static initialiser.
char* ReconNormalBuf();

// gilde.exe 0x58f138 — VIBE_Util_ZeroStruct12 (__usercall, eax = result).
// Zeroes a 12-byte record: byte at +0, dword at +4, dword at +8. The byte at +0
// is cleared independently of +4..+7 (the original writes a single byte there,
// leaving +1..+3 untouched). Returns the same pointer.
void* ReconZeroStruct12(void* p);

// Comparator used by ReconBubbleSortRecords. The original formats both record
// ids with VIBE_Text_FormatItemLabel @0x59c2e4 and compares the two labels with
// VIBE_Util_StrCmp; a result of exactly 1 (b's label sorts after a's) triggers a
// swap. That label formatter is outside this cluster, so callers inject the
// comparison: return ReconStrCmp(label(b), label(a)) semantics — i.e. the value
// the original passes to its `== 1` test. The default hook treats ids as already
// ordered (returns <=0 always) so the sort is inert until wired to the real
// formatter.
using ReconLabelCompare = int (*)(i32 id_a, i32 id_b, void* user);

// gilde.exe 0x59211c — VIBE_Util_BubbleSortRecords
//   (__usercall, eax=count, edx=array, bl=skipFlag).
// `array` points at `count` 32-bit ids. When skipFlag is zero, performs an
// ascending bubble sort: for each i in [0,count-1) and each j in (i,count), if
// cmp(array[i], array[j]) == 1 the two ids are swapped. When skipFlag is
// non-zero the function does nothing (matches the original's early-out). `cmp`
// defaults to an inert hook (no swaps) when null.
void ReconBubbleSortRecords(i32* array, i32 count, u8 skipFlag,
                            ReconLabelCompare cmp = nullptr, void* user = nullptr);

// ---- MSVC qsort core (median-of-medians, insertion cutoff at 8) ----------

// Comparator: __cdecl, two element pointers, returns <0 / 0 / >0. The original
// is a register comparator invoked with both element pointers pushed; here it is
// the equivalent C signature.
using ReconSortCompare = int (*)(const void* a, const void* b);

// gilde.exe 0x5ea040 — VIBE_Util_SortSwapElements (__usercall, ecx=width,
// edi=a, esi=b). Swaps `width` bytes between blocks `a` and `b`, dword-at-a-time
// with a byte tail (the original uses an atomic exchange for the dword run; an
// in-place swap of two non-overlapping blocks is observationally identical).
void ReconSortSwapElements(std::size_t width, void* a, void* b);

// gilde.exe 0x5e9ff0 — VIBE_Util_SortChoosePivot (__usercall, eax=a, edx=b,
// ecx=cmp, ebx=c). Returns whichever of a/b/c is the median under `cmp`. `cmp`
// is invoked as cmp(a,b), cmp(b,c), cmp(a,c) lazily exactly as the original
// branches dictate.
void* ReconSortChoosePivot(void* a, void* b, void* c, ReconSortCompare cmp);

// gilde.exe 0x5ea068 — VIBE_Util_QuickSort (__usercall, eax=base, edx=num,
// ecx=cmp, ebx=width). In-place sort of `num` elements of `width` bytes at
// `base`. Median pivot (median-of-medians for large runs), three-way partition,
// explicit stack (depth 32 entries), insertion sort for runs < 16 via a
// shrinking-gap pass. Bit-for-bit the MSVC qsort shipped at 0x5ea068.
void ReconQuickSort(void* base, std::size_t num, ReconSortCompare cmp, std::size_t width);

} // namespace guild::util
