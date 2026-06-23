#include "sim/cutscene_rand.h"

#include "crt/printf.h"   // crt::Sprintf == VIBE_Crt_Sprintf_0 @0x5cba00

namespace guild::sim {

// gilde.exe dword_11B4E38 @0x11B4E38 — the single shared cutscene-LCG state word.
i32 g_cutsceneRandSeed = 0;

namespace {

// Default trace: reproduce VIBE_Crt_Sprintf_0 into a scratch buffer (the original
// uses a 64/68-byte ebp-relative stack buffer it never reads back) and return the
// char count, matching the call's contract exactly without emitting output.
int DefaultLog(const char* fmt, i32 value) {
    char scratch[68];                         // [esp+0h] var_44 (Get) / var_40 (Set)
    return guild::crt::Sprintf(scratch, fmt, value);  // call VIBE_Crt_Sprintf_0
}

CutsceneRandLogFn g_log = &DefaultLog;

} // namespace

void SetCutsceneRandLogHook(CutsceneRandLogFn fn) {
    g_log = fn ? fn : &DefaultLog;
}

// gilde.exe 0x4ac9a0 — VIBE_Cutscene_SetRandSeed.
//   dword_11B4E38 = a1; return Crt_Sprintf_0(buf, "cut_randseed: %i", a1);
i32 CutsceneSetRandSeed(i32 seed) {
    g_cutsceneRandSeed = seed;                              // mov ds:dword_11B4E38, eax
    return g_log("cut_randseed: %i", seed);                 // call VIBE_Crt_Sprintf_0
}

// gilde.exe 0x4ac9c0 — VIBE_Cutscene_GetRandSeed.
//   Crt_Sprintf_0(buf, "cut_getrandseed: %i", dword_11B4E38);
//   return dword_11B4E38;   (reloaded after the sprintf)
i32 CutsceneGetRandSeed() {
    g_log("cut_getrandseed: %i", g_cutsceneRandSeed);       // call VIBE_Crt_Sprintf_0
    return g_cutsceneRandSeed;                              // mov eax, ds:dword_11B4E38
}

} // namespace guild::sim
