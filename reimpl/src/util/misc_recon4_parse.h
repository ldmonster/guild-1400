#pragma once
// misc_recon4_parse.{h,cpp} — CRT float-string scanner + tick-seed helpers (gilde.exe)
//
// Provenance:
//   gilde.exe 0x5d3fe0  VIBE_Util_ParseDoubleString  (__usercall eax=ptr, edx=out, ebx=endptr)
//   gilde.exe 0x5d4174  VIBE_Util_StrToDouble        (__usercall st0; eax=str, edx=endptr)
//   gilde.exe 0x1414ae0 VIBE_Util_GetTickSeed
//   gilde.exe 0x1422144 VIBE_Rand_SetSeed
//
// These are the MSVC CRT `strtod` / `_fltin` front-ends. The actual decimal
// string -> IEEE-754 conversion is done by VIBE_Math_ParseDecimal (0x5fa622) and
// VIBE_Math_ScaleExponentClamped (0x5fa7a3) — a big-integer BCD/float decoder that
// is part of the C runtime and is NOT faithfully reconstructable here (it relies on
// 80-bit x87 long-double accumulation + a CRT-internal scale table). Per project
// rule 8 we do NOT fake it: the GENUINE part we CAN translate 1:1 is the lexical
// scanner (whitespace skip, sign, digit collection, '.' handling, exponent FSM,
// leading/trailing-zero exponent bookkeeping, the 19-significant-digit cap, and the
// final overflow/underflow classification). That scanner is reproduced exactly; the
// decoder itself is routed through an inert hook (see ParseScanResult / decodeHook).
//
// The scanner result captures everything the original computed before calling the
// decoder, so the decode can be supplied later (or in tests) without altering the
// reconstructed control flow.

#include "guild/common/types.h"

namespace guild::util {

using f32 = float;
using f64 = double;

// ---- ParseDoubleString scanner result -------------------------------------
// Mirrors the state the original 0x5d3fe0 had assembled at LABEL_42 just before
// invoking VIBE_Math_ParseDecimal / VIBE_Math_ScaleExponentClamped.
struct ParseScanResult {
    // Significant decimal digits (ASCII '0'..'9'), at most 19 stored (v17[19]).
    // Leading zeros are dropped exactly as the original does (v7 |= digit gate).
    u8   mantissa[20];   // NUL-terminated like v17 after v17[v6]=0
    int  digitCount;     // v6 (clamped to <=19 for the decoder), pre-clamp value too
    int  rawDigitCount;  // v6 before the >19 clamp (used to adjust the exponent)
    int  decExponent;    // v13: net base-10 exponent after the scanner's bookkeeping
    bool negative;       // v5 bit0
    int  classification; // return code: 0 zero, 1 ok, 2 underflow, 3 overflow
    const u8* endPtr;    // v20: first char not consumed
};

// gilde.exe 0x5d3fe0 — scanner-only reconstruction. Fills `out` with the exact
// digit/exponent/sign state the original assembled. Returns the same status code
// the original returns (0/1/2/3). `endPtr` (if non-null) receives the consume point,
// matching the original's `*a3 = v20`.
int ParseDoubleStringScan(const u8* str, ParseScanResult* out, const u8** endPtr);

// gilde.exe 0x1422144 — VIBE_Rand_SetSeed. Stores the seed into the global PRNG
// state (dword_1452BD0) and returns it. Reconstructed 1:1.
i32 RandSetSeed(i32 seed);

// Returns the current stored seed (== dword_1452BD0). Test/inspection helper.
i32 RandCurrentSeed();

// gilde.exe 0x1414ae0 — VIBE_Util_GetTickSeed: seeds the PRNG from the local wall
// clock. The time source (VIBE_Time_GetLocalTime @0x142216c, a CRT/Win32 localtime)
// is the platform boundary; it is supplied via the hook so the headless build needs
// no OS clock. Returns the value RandSetSeed returns (== the time value).
//   int GetTickSeed() { return RandSetSeed(GetLocalTime(0)); }
struct TickSeedHooks {
    // Default returns 0 (inert / deterministic). Real backend wires this to the
    // SDL/CRT local-time source.
    i32 (*getLocalTime)() = nullptr;
};
i32 GetTickSeed(const TickSeedHooks& hooks = {});

} // namespace guild::util
