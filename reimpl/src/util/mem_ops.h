#pragma once
#include <cstddef>

// guild::util — the engine's overlap-safe block-copy primitives recovered from
// gilde.exe. Both are observable-equivalent to C `memmove`; the originals differ
// only in their (perf-oriented) word/cache unrolling, which produces no
// observable difference. They are OS-free leaves used by the buffered-IO reader
// (VIBE_Crt_FileRead calls MemMove) and bulk record moves across the game.
namespace guild::util {

// VIBE_Util_MemMove @0x5d9310 — memmove(dst, src, n). Returns `dst`.
//   The original copies forward via qmemcpy when the regions don't overlap (or
//   src >= dst), and copies BACKWARD word-by-word when they overlap with
//   src < dst < src+n. Behaviour is exactly std::memmove.
void* MemMove(void* dst, const void* src, std::size_t n);

// VIBE_Util_MemMove_27370 @0x1427370 — a second memmove with heavy Duff's-device
// unrolling (4-byte stores, dword-aligned fast paths, 8-way unroll). Observable
// behaviour is identical to std::memmove; reproduced as such. Returns dst (as the
// original returns a1 in eax).
void* MemMove2(void* dst, const void* src, std::size_t n);

} // namespace guild::util
