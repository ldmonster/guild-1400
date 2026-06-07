#pragma once
#include "guild/common/types.h"

// RSA Data Security MD5 message-digest (RFC 1321), as embedded in gilde.exe.
//
//   VIBE_Md5_Init      @0x1418c20
//   VIBE_Md5_Update    @0x1418c70
//   VIBE_Md5_Final     @0x1418da0
//   VIBE_Md5_Transform @0x1418e80
//
// The original works on a 0x58-byte context struct laid out (as 32-bit words) as:
//   [0..3]  state a,b,c,d
//   [4..5]  bit count (low, high)
//   [6..21] 64-byte input block buffer (a2 == a1 + 6)
// We model that layout exactly so byte-for-byte behavior is preserved, including
// the deliberate 32-bit wraparound in the transform.
namespace guild::compress {

struct Md5Context {
    u32 state[4];   // +0x00  a,b,c,d
    u32 count[2];   // +0x10  bit count: count[0] low, count[1] high
    u8  buffer[64]; // +0x18  partial input block (a1 + 6 words)
};

// VIBE_Md5_Init @0x1418c20 — set initial state/count.
void Md5Init(Md5Context* ctx);

// VIBE_Md5_Update @0x1418c70 — absorb `len` bytes of `data`.
void Md5Update(Md5Context* ctx, const u8* data, u32 len);

// VIBE_Md5_Final @0x1418da0 — finish and write the 16-byte digest to `digest`.
void Md5Final(u8 digest[16], Md5Context* ctx);

} // namespace guild::compress
