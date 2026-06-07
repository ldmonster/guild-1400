#pragma once
#include "guild/common/types.h"
#include <cstddef>

// guild::util — a batch of small, pure, OS-free CRT/util leaves recovered 1:1
// from gilde.exe (the long tail of the VIBE_Util_* prefix). Every function here
// is deterministic: it touches only its arguments and (for the shuffles) the
// shared LCG via guild::crt::RandNext. No file/heap/OS state.
//
// Provenance (each definition carries its own `// gilde.exe 0xADDR` comment):
//   RetZero                  0x4029c0   VIBE_Util_RetZero
//   RetOne                   0x527d9c   VIBE_Util_RetOne
//   MemFindPattern           0x140b000  VIBE_Util_MemFindPattern
//   RandomMod                0x140b530  VIBE_Util_RandomMod
//   BuildCrc16Table          0x1412290  VIBE_Util_BuildCrc16Table
//   RotateByte               0x14155b0  VIBE_Util_RotateByte
//   AlignTo8 (RotByteComp)   0x14155f0  VIBE_Util_AlignTo8
//   InitAndShuffleByteArray  0x58b9c4   VIBE_Util_InitAndShuffleByteArray
//   InitAndShuffleDwordArray 0x58ba98   VIBE_Util_InitAndShuffleDwordArray
//   ShuffleDwordArray        0x58bb74   VIBE_Util_ShuffleDwordArray
//   StrCopyChecked           0x43fc58   VIBE_Util_StrCopyChecked
namespace guild::util {

// VIBE_Util_RetZero / VIBE_Util_RetOne — constant returners used as default
// vtable/callback slots all over the binary.
int RetZero();
int RetOne();

// VIBE_Util_MemFindPattern @0x140b000 — a (Knuth-Morris-Pratt-shaped but actually
// naive) substring search over a raw byte buffer. Scans `haystack[0..hayLen)` for
// the first occurrence of `needle[0..needleLen)`. The original's matcher resets the
// needle cursor to 0 on any mismatch (so it is the classic O(n*m) scan, NOT real
// KMP). Returns a pointer to the match start (haystack + i - needleLen) or nullptr.
// Faithful quirk: with needleLen==0 the loop never advances the needle cursor, so
// (v5 == a4) is true immediately at i==0 -> returns haystack (matches the binary).
const void* MemFindPattern(const void* haystack, const void* needle,
                           int hayLen, int needleLen);

// VIBE_Util_RandomMod @0x140b530 — builds a 31-bit value from two VIBE_Rand_Next()
// draws ((next<<16) | next) and returns it modulo `mod`. NOTE this uses the OTHER
// generator (VIBE_Rand_Next, the MSVC 214013 LCG), wired here through a hook so the
// unit test can drive it deterministically; the default hook calls crt::RandNext.
u32 RandomMod(u32 mod);

// VIBE_Util_BuildCrc16Table @0x1412290 — fills a 256-entry CCITT CRC-16 lookup
// table (polynomial 0x1021, MSB-first) into `table16`. table16[i] is the value of
// (i<<8) shifted left 8 times, XOR 0x1021 on each high-bit-set step.
void BuildCrc16Table(u16* table16);

// VIBE_Util_RotateByte @0x14155b0 — left-rotates the low byte of `value` by
// (shift % 8) bits within an 8-bit lane: ((v << s) & 0xFF) | (((v << s) & 0xFF00) >> 8).
int RotateByte(int value, int shift);

// VIBE_Util_AlignTo8 @0x14155f0 — RotateByte(value, 8 - shift % 8) (the inverse
// rotation; "align to the 8-bit boundary"). Pure composition of RotateByte.
int AlignTo8(int value, int shift);

// VIBE_Util_InitAndShuffleByteArray @0x58b9c4 — fill arr[i]=i for i in [0,count),
// then perform (count>>2 clamped to >=1) + (count>>1) successful random swaps
// (a swap counts only when the two drawn indices differ). count is a byte (0..255).
// Returns nothing meaningful (the original leaves a swap-temp in eax).
void InitAndShuffleByteArray(u8 count, u8* arr);

// VIBE_Util_InitAndShuffleDwordArray @0x58ba98 — same as above for a dword array.
void InitAndShuffleDwordArray(u8 count, u32* arr);

// VIBE_Util_ShuffleDwordArray @0x58bb74 — shuffle ONLY (no init fill) of a dword
// array of `count` elements, same swap count as the init variants.
void ShuffleDwordArray(u32 count, u32* arr);

// VIBE_Util_StrCopyChecked @0x43fc58 — if dst is null, return 0; else copy the
// NUL-terminated `src` into `dst` (the original unrolls two bytes per step but the
// observable result is strcpy). Returns 1 on copy, 0 if dst was null.
int StrCopyChecked(char* dst, const char* src);

// ---- test seam for the RandomMod generator (the MSVC 214013 LCG) -------------
// Inert default = crt::RandNext-equivalent MSVC generator seeded to 0.
struct UtilMiscHooks {
    int (*randNext)();   // returns 0..0x7FFF
};
UtilMiscHooks& GetUtilMiscHooks();
// Reset the internal MSVC LCG state (dword_1452BD0) used by the default randNext.
void SetMsvcRandSeed(i32 seed);
int  MsvcRandNext();     // VIBE_Rand_Next @0x142214e (214013 LCG)

} // namespace guild::util
