#pragma once
// =============================================================================
// guild::drm — DiscProtect XOR/LFSR authentication codecs.
//
// Pure, byte-exact reconstructions of the DiscProtect copy-protection codec
// cluster of gilde.exe (32-bit x86, imagebase 0x400000). These are the leaf
// data-transform primitives the disc-authentication orchestrator
// (0x1419eb0 VIBE_DiscProtect_RunAuthentication, kept as an OS-coupled stub in
// drm_stub.cpp) builds on:
//
//   * a 15-bit LFSR keystream-table generator,
//   * a family of "rolling XOR" byte obfuscators/de-obfuscators (the same
//     `buf[i] ^= buf[i+1]` chain RunAuthentication open-codes on its registry
//     path / device strings), and
//   * the signature comparison + closed-form scoring used to decide "genuine".
//
// Every function reproduces the original control flow, loop bounds, integer
// wraparound and edge cases exactly. The original codecs mutate fixed global
// buffers in place; here each takes a caller-supplied buffer pointer + length
// so the math is testable headless with no OS / disc coupling (the original's
// buffers are themselves just RAM the orchestrator fills from disc/registry).
// =============================================================================
#include "guild/common/types.h"
#include <cstddef>

namespace guild::drm {

using guild::u8;
using guild::u16;
using guild::i32;
using guild::u32;

// gilde.exe 0x1419db0 — VIBE_DiscProtect_InitLfsrTable
// 15-bit Fibonacci LFSR (taps on bits 0 and 1; feedback into bit 14 = 0x4000).
// Seed = 1. Runs 72 "warm-up" full bytes (8 shifts each) then emits the next 32
// LFSR low-bytes into out[0..31]. `out` must hold >= 32 bytes. The original
// writes the fixed global byte_145AD40[32]; pass that buffer here.
//   Warm-up count and emit count are the original literals (var_4 = 0x48 = 72,
//   inner span = 0x20 = 32).
void InitLfsrTable(u8* out /* >=32 bytes */);

// Convenience: the byte-exact 32-byte table InitLfsrTable produces from seed 1.
// (Computed, not stored — the static global image is stale runtime garbage.)
// Exposed so tests can assert the keystream without re-deriving the LFSR.
extern const u8 kLfsrTableSeed1[32];

// gilde.exe 0x141b060 — VIBE_DiscProtect_XorEncodeForward  (__stdcall(buf,len))
// Forward rolling XOR: for i in [0, len-1): buf[i] ^= buf[i+1].
void XorEncodeForward(u8* buf, i32 len);

// gilde.exe 0x141b0b0 — VIBE_DiscProtect_XorEncodeReverse  (__stdcall(buf,len))
// Reverse rolling XOR (inverse of XorEncodeForward): for i in [len-2 .. 0]:
//   buf[i] ^= buf[i+1].  No-op when len <= 1.
void XorEncodeReverse(u8* buf, i32 len);

// gilde.exe 0x141b100 — VIBE_DiscProtect_XorDecode123  (__stdcall(buf))
// Reverse rolling XOR over a fixed 123-byte block: i from 122 down to 0.
// (Decodes a 123-byte field encoded by a forward pass; buf must hold >=123.)
void XorDecode123(u8* buf /* >=123 bytes */);

// gilde.exe 0x141b140 — VIBE_DiscProtect_XorEncode127  (__stdcall(buf))
// Forward rolling XOR over a fixed 127-step block: i from 0 to 126.
// (buf must hold >=128 bytes; touches buf[0..127].)
void XorEncode127(u8* buf /* >=128 bytes */);

// gilde.exe 0x141b180 — VIBE_DiscProtect_XorDecode127  (__stdcall(buf))
// Reverse rolling XOR over a fixed 127-step block: i from 126 down to 0.
// Exact inverse of XorEncode127. (buf must hold >=128 bytes.)
void XorDecode127(u8* buf /* >=128 bytes */);

// gilde.exe 0x1416730 — VIBE_DiscProtect_CompareBytes  (__stdcall(a1,a2,len))
// Counts positions i in [0,len) where a[i] == b[i]. Returns the match count.
// Signed comparison of bytes is irrelevant to equality; loop bound is signed.
i32 CompareBytes(const u8* a, const u8* b, i32 len);

// gilde.exe 0x1417780 — VIBE_DiscProtect_EvaluateSignature
// Closed-form confidence score from a per-frame match count against the
// reference signature string.
//
// In the original this function first runs the disc-timing analysis pipeline
// (0x1416830 AnalyzeFrameTimings, 9 passes) to *produce* the decoded frame
// buffer, then scores it via:
//     cnt = CompareBytes(signature, decodedFrames, len)        // 0x1416730
//     T   = 27                                                  // dword_142C460
//     scale = 1.0                                               // dbl_1455818
//     if (cnt >= T)  score = scale*(cnt - T) / (72 - T)
//     else           score = scale*(cnt - T) / T
//     return (double)(float)score                               // narrows to f32
//
// The timing pipeline depends on float tunables that are *runtime-populated*
// (all zero in the static image) and on raw disc-read timing buffers, so it is
// NOT a pure recoverable codec and is intentionally not reconstructed here
// (see discprotect_codec.cpp header note + the agent final report). The exact
// scoring closed-form IS pure and is reproduced below: callers pass the match
// count (and may obtain it from CompareBytes on real buffers).
double EvaluateSignatureScore(i32 matchCount);

// Threshold / window constants recovered byte-exact from the static image.
inline constexpr i32   kEvalThreshold      = 27;   // dword_142C460
inline constexpr i32   kEvalWindow         = 72;   // literal 0x48
inline constexpr double kEvalScale         = 1.0;  // dbl_1455818

} // namespace guild::drm
