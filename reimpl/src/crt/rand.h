#pragma once
#include "guild/common/types.h"

// CRT pseudo-random number generator (ANSI/POSIX 32-bit LCG).
//
// The original stores the 32-bit state in the per-thread CRT data block:
//   VIBE_Util_GetRandStatePtr @0x5cb8b0  == off_64A90C() + 12
// We model a single global state. The only original threads (net/audio) did not
// consume the generator, so single-state behavior is observationally identical.
// See PLAN §8 (TLS) if a per-thread state is ever needed.
namespace guild::crt {

// VIBE_Util_GetRandStatePtr @0x5cb8b0 — address of the 32-bit LCG state.
u32* RandStatePtr();

// VIBE_Util_RandNext @0x5cb8bc — advance the LCG and return bits 16..30:
//   state = state * 1103515245 + 12345;  return (state >> 16) & 0x7FFF;
// Range is [0, 0x7FFF]. Returns 0 if the state pointer is null.
int RandNext();

// CRT srand — set the generator state (matches std::srand semantics).
void Srand(u32 seed);

} // namespace guild::crt
