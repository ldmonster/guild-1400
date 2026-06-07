#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — 96-bit big-integer + float-bit math leaves (gilde.exe crt/math).
//
// These are the low-level fixed-precision integer primitives the MSVC dtoa/strtod
// support layer (and a couple of float-format helpers) build on. Each operates on
// a little-endian 3x uint32 magnitude ("96-bit", element [0] = least significant)
// or on raw IEEE-754 bit patterns — pure arithmetic, no allocation, no globals,
// no FPU control-word juggling — so each is exactly golden-testable in isolation
// against hand-computed values cross-checked with the Hex-Rays reference.
//
// Translated functions (each verified UNTRANSLATED at time of writing, by both
// address AND bare name across all of src/, case-insensitively):
//   0x1426f14  VIBE_Math_BigIntCopy96          (copy 3 dwords src -> dst)
//   0x1426f2f  VIBE_Math_BigIntZero96          (zero 3 dwords)
//   0x1426f3b  VIBE_Math_BigIntIsZero96        (all-3-dwords-zero predicate)
//   0x1428e2d  VIBE_Math_BigIntShiftLeft96     (<<1 across 96 bits)
//   0x1428e5b  VIBE_Math_BigIntShiftRightOne96 (>>1 across 96 bits, low word arith)
//   0x1426f56  VIBE_Math_BigIntShiftRight96    (>>n, n in bits, with word skip)
//   0x14166f0  VIBE_Math_CompareFloat          (float* cmp via double promotion)
//   0x606890   VIBE_Math_DoubleToFloatBits     (IEEE double bits -> float bits, RNE)
// =============================================================================

namespace guild::render {

// --- 96-bit little-endian magnitude primitives ----------------------------

// 0x1426f14 — VIBE_Math_BigIntCopy96. Copies 3 dwords from src to dst; returns
// the past-the-end src pointer (the original returns the advanced src cursor).
u32* BigIntCopy96(const u32* src, u32* dst);

// 0x1426f2f — VIBE_Math_BigIntZero96. Writes three zero dwords. Returns 0.
i32  BigIntZero96(u32* a);

// 0x1426f3b — VIBE_Math_BigIntIsZero96. Returns 1 iff all 3 dwords are zero,
// short-circuiting on the first nonzero word.
i32  BigIntIsZero96(const u32* a);

// 0x1428e2d — VIBE_Math_BigIntShiftLeft96. In-place <<1 across the 96-bit value
// (carry propagates [0]->[1]->[2]; the top bit of [2] is dropped). Returns a.
u32* BigIntShiftLeft96(u32* a);

// 0x1428e5b — VIBE_Math_BigIntShiftRightOne96. In-place >>1 across 96 bits. The
// low 64 bits ([0],[1]) are treated as a signed 64-bit arithmetic >>1 (matching
// the original's `*(__int64*)(a+1) >> 1`); [2] is logically >>1. Returns a.
u32* BigIntShiftRightOne96(u32* a);

// 0x1426f56 — VIBE_Math_BigIntShiftRight96. In-place >>n bits (n decomposed into
// word-skip n/32 and intra-word n%32). Returns a pointer to one of the dwords
// (the original returns whatever landed in eax on the last loop iteration).
i32* BigIntShiftRight96(i32* a, i32 nbits);

// --- IEEE-754 bit helpers --------------------------------------------------

// 0x14166f0 — VIBE_Math_CompareFloat. Compares *a and *b. Returns 1 if *a>*b,
// 0 if equal (>=), -1 if *a<*b. The original promotes both to double before
// comparing, so this preserves any float->double widening behavior.
i32  CompareFloat(const float* a, const float* b);

// 0x606890 — VIBE_Math_DoubleToFloatBits. Converts a raw IEEE-754 double bit
// pattern (passed as the 64-bit integer) into the raw IEEE-754 single-precision
// bit pattern, with round-to-nearest-even and overflow/underflow to inf/zero,
// exactly mirroring the original integer-domain conversion. Returns float bits.
u32  DoubleToFloatBits(u64 doubleBits);

} // namespace guild::render
