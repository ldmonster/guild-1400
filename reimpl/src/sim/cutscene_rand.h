#pragma once
// sim/cutscene_rand.{h,cpp} — the cutscene RNG SEED accessors that operate on the
// SHARED global seed word dword_11B4E38 (@0x11B4E38).
//
//   gilde.exe 0x4ac9a0 — VIBE_Cutscene_SetRandSeed(seed@eax)
//   gilde.exe 0x4ac9c0 — VIBE_Cutscene_GetRandSeed()
//
// These are the get/set of the cutscene LCG's CURRENT state word, used to
// SNAPSHOT and RESTORE the deterministic cutscene/combat/duel RNG across a step so
// that, e.g., a re-played event draws the identical sequence (see cutscene.h /
// combat.h: both reference dword_11B4E38 as the shared cutscene-LCG state). The
// advance/draw of the LCG itself lives in CutsceneRng (cutscene.h) and the
// combat-side CutsceneRng (combat.h); this module owns ONLY the global state word
// they snapshot through, so there is exactly ONE dword_11B4E38 in the build.
//
// Disassembly — Set (0x4ac9a0):
//   sub  esp, 40h
//   push eax                          ; the seed
//   push offset "cut_randseed: %i"
//   mov  ds:dword_11B4E38, eax        ; STORE the seed
//   lea  eax, [esp+...var_40]
//   push eax
//   call VIBE_Crt_Sprintf_0           ; debug log into a scratch buffer
//   add  esp, 0Ch / add esp, 40h
//   retn                              ; returns sprintf's result (chars written)
//
// Disassembly — Get (0x4ac9c0):
//   push edx / sub esp, 40h
//   mov  edx, ds:dword_11B4E38
//   push edx
//   push offset "cut_getrandseed: %i"
//   lea  eax, [esp+...var_44]
//   push eax
//   call VIBE_Crt_Sprintf_0           ; debug log
//   mov  eax, ds:dword_11B4E38        ; RELOAD and RETURN the seed
//   ...
//   retn
//
// Both write a "cut_(get)randseed: %i" line into a 64/68-byte stack scratch buffer
// via VIBE_Crt_Sprintf_0 — a pure debug-trace side effect with no further use of the
// buffer. We reproduce the side effect through an optional log hook (default inert)
// so the observable behavior (the format + the value) is faithful and testable, and
// reproduce Set's return value (the sprintf char count) exactly.

#include "guild/common/types.h"

namespace guild::sim {

using guild::i32;

// The shared cutscene-LCG state word (gilde.exe dword_11B4E38 @0x11B4E38).
// Exposed for the snapshot/restore callers and tests; it is the SINGLE definition
// of this global in the build.
extern i32 g_cutsceneRandSeed;

// Optional debug-trace hook reproducing the original's VIBE_Crt_Sprintf_0 call.
// `fmt` is the original format string ("cut_randseed: %i" / "cut_getrandseed: %i"),
// `value` the seed. Returns the number of chars the original sprintf would have
// written (so Set's return value is byte-faithful). Default: a built-in that formats
// into a scratch buffer and returns the length, matching VIBE_Crt_Sprintf_0's
// contract (chars written, excluding the NUL) without emitting anything.
using CutsceneRandLogFn = int (*)(const char* fmt, i32 value);
void SetCutsceneRandLogHook(CutsceneRandLogFn fn);

// gilde.exe 0x4ac9a0 — VIBE_Cutscene_SetRandSeed(seed).
// Stores `seed` into g_cutsceneRandSeed, emits the "cut_randseed: %i" trace, and
// returns the trace's char count (the original returns VIBE_Crt_Sprintf_0's result).
i32 CutsceneSetRandSeed(i32 seed);

// gilde.exe 0x4ac9c0 — VIBE_Cutscene_GetRandSeed().
// Emits the "cut_getrandseed: %i" trace, then returns g_cutsceneRandSeed (reloaded
// after the trace, exactly as the original does).
i32 CutsceneGetRandSeed();

} // namespace guild::sim
