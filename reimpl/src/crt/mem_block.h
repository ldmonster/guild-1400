#pragma once
#include "guild/common/types.h"
#include <cstddef>

// guild::crt — the MSVC CRT block primitives compiled into gilde.exe's higher
// imagebase region (0x142xxxx). These are the optimised libc clones (4-byte-at-a-
// time, alignment-aware) used by the formatted-IO and locale layers. All are pure
// and OS-free; observable behaviour matches the C library functions noted below.
//
// Provenance (each definition carries `// gilde.exe 0xADDR`):
//   MemSet            0x1421a20  VIBE_Mem_Set            (== memset)
//   MemCompare        0x1422010  VIBE_Mem_Compare        (== memcmp)
//   MemCompareBounded 0x1421fd0  VIBE_Mem_CompareBounded (memcmp up to first NUL)
//   MemMoveOverlapping 0x1421340 VIBE_Mem_MoveOverlapping(== memmove)
namespace guild::crt {

// VIBE_Mem_Set @0x1421a20 — memset(dst, byte, n). Returns dst. The original aligns
// the head to a 4-byte boundary, fills 4 bytes at a time with the broadcast value
// (0x01010101 * byte), then the tail; result is exactly std::memset.
void* MemSet(void* dst, u8 value, std::size_t n);

// VIBE_Mem_Compare @0x1422010 — memcmp(a, b, n). Returns 0 if equal, +1 if the
// first differing byte in `a` is greater, -1 if less (the original's word-at-a-time
// fast path resolves the differing byte and yields -v6-(v6-1) ∈ {-1,+1}).
int MemCompare(const void* a, const void* b, std::size_t n);

// VIBE_Mem_CompareBounded @0x1421fd0 — compares `a` and `b` over at most `n` bytes
// but ALSO stops at the first NUL byte found in `a` (it first measures the run
// length up to the first NUL within n, then memcmp's that many bytes). Returns 0 if
// the bounded runs match, +1 / -1 on the first difference. Used as a length-capped
// string compare in the locale layer.
int MemCompareBounded(const void* a, const void* b, int n);

// VIBE_Mem_MoveOverlapping @0x1421340 — memmove(dst, src, n). Returns dst. Copies
// backward (from the high end) when the regions overlap with src < dst < src+n,
// forward otherwise; both via 4-byte unrolled stores. Exactly std::memmove.
void* MemMoveOverlapping(void* dst, const void* src, std::size_t n);

} // namespace guild::crt
