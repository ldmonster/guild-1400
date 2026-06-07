#pragma once
#include "guild/common/types.h"

// Fixed-width bit-vector primitives recovered from gilde.exe. The original is a
// 96-bit magnitude (3x u32 words) used by the float-formatting big-integer
// rounding path (VIBE_Math_BigIntRoundBit @0x1426e88). Bits are numbered
// MSB-first within each word: bit 0 is the most-significant bit of word[0],
// bit 31 the LSB of word[0], bit 32 the MSB of word[1], etc.
//
//   guild::util::BitSetTestTail        — gilde.exe 0x1426de9 — VIBE_BitSet_TestRange
//   guild::util::BitSetAddRoundCarry   — gilde.exe 0x1426e32 — VIBE_BitSet_AtomicClearRange
//
// (The "AtomicClearRange" symbol name is a misnomer from the auto-namer: the
// function performs an add-with-carry round-up, not a clear.)
namespace guild::util {

// Number of u32 words in the fixed bit-vector (the loops stop at word index 3).
inline constexpr int kBitSetWords = 3;

// 0x1426de9 — return 1 iff NO bits at position `bit` or higher index (i.e. at or
// below the rounding bit) are set: tests the in-word tail mask, then any
// fully-following words. Returns 0 if a set bit is found.
//   words: pointer to kBitSetWords u32s.  bit: bit index in [0, 96).
int BitSetTestTail(const u32* words, int bit);

// 0x1426e32 — add 1 at the bit position `bit` (MSB-first numbering) into the
// magnitude and propagate the carry toward lower word indices (more-significant
// words). Returns the final carry-out (1 if the whole magnitude overflowed).
int BitSetAddRoundCarry(u32* words, int bit);

} // namespace guild::util
