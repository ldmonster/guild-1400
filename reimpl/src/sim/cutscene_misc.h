#pragma once
// ===========================================================================
// cutscene_misc.{h,cpp} — the remaining self-contained cutscene leaf bodies
// (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// This module collects the deterministic cutscene helpers that sit beneath the
// per-type mains and the active-processing driver: the duel-outcome RNG tier,
// the per-frame tick counter / frame-count latch, the pending-script table
// scans, the actor-animation pause/resume sweeps, the tick-proc registration,
// the music pause/resume gates and the progress-bar fill math.
//
// Each function is translated 1:1 from its Hex-Rays pseudocode. The cross-module
// leaves these bodies touch (Script_*, Character_*, Music_*, TimeBase_*,
// frame-loop, Object_* / Coord_*) are routed through CutsceneMiscHooks so the
// deterministic control flow / arithmetic is faithfully reproduced and testable.
//
// RECOVERED GEOMETRY / GLOBALS
//   * Script record table  (base dword_62E8A4 @0x62E8A4): stride 2584 bytes,
//     128 records (loop bound 330752 == 128*2584). Fields the scans read:
//       +128 (dword) handle      ; -1 == empty record
//       +132 (dword) kind/state  ; -2 == "pending cutscene script" sentinel
//       +164 (byte)  flags       ; bit0 == active/running
//   * Character pointer table (base dword_66F0D0 @0x66F0D0): 512 dword slots of
//     Character*; entry 0 == empty. Each Character's +140 byte holds the
//     animation flags (bit2 == "ani currently paused by cutscene").
//   * dword_6315A4 — duel "outcome/report mode" flag (set by the combat path);
//     selects the 100-roll outcome-tier branch over the 10-roll choice branch.
//   * dword_6315A8 (tick counter), dword_6315AC (tick limit), dword_6315B0
//     (latched "expired" bool) — the TickCompareCounter triple.
//   * dword_631630 (float) <- dword_631634 (int frame count, then zeroed) — the
//     per-frame frame-count latch driven by the registered tick proc.
//   * dword_62EB38 — the global millisecond clock (g_gameTick, owned elsewhere).
//   * dword_6315F8/FC (progress-bar start tick / span), flt_61D94C (bar width).
//   * dword_6315BC — the global "cutscene disabled / remote replay" gate: when
//     nonzero the audio/script gates short-circuit (we mirror that exactly).
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "sim/cutscene.h"   // CutsceneSlot / CutsceneRng

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered table geometry.
// ---------------------------------------------------------------------------
constexpr int kScriptRecordStride = 2584;    // bytes per script record
constexpr int kScriptRecordCount  = 128;     // 330752 / 2584
constexpr int kScriptOffHandle    = 128;     // +0x80  (-1 == empty)
constexpr int kScriptOffKind      = 132;     // +0x84  (-2 == pending sentinel)
constexpr int kScriptOffFlags     = 164;     // +0xA4  (bit0 == active)
constexpr i32 kScriptPendingKind  = -2;      // the -2 sentinel

constexpr int kCharacterTableCount = 512;    // dword_66F0D0[512]
constexpr int kCharacterOffAniFlags = 140;   // +0x8C byte; bit2 == ani paused
constexpr u8  kCharAniPausedBit     = 0x04;

// Progress-bar fill: flt_61D94C == bar full width (recovered bytes 00 00 C8 42
// == IEEE-754 0x42C80000 == 100.0).  The fill is (1 - elapsed/span) * width,
// truncated to int and clamped to [0, ...].
constexpr float kProgressBarWidth = 100.0f;  // flt_61D94C

// ---------------------------------------------------------------------------
// Leaf hooks — the cross-cluster side effects these bodies make. Tests install
// a recording mock; the inert default (all null) makes every leaf a no-op so
// the deterministic scan/arithmetic logic can be exercised in isolation.
// ---------------------------------------------------------------------------
struct CutsceneMiscHooks {
    // dword_6315BC — the global "cutscene disabled / remote replay" gate. When
    // nonzero, PauseGame/ResumeGame/the script runners short-circuit. Default 0.
    i32 disabledGate = 0;

    // --- the script-record table (Script_*) -----------------------------------
    // Script_FindByHandle(handle) -> opaque script* (0 if gone). Used to decide
    // whether a still-running record should be finished.
    void* (*scriptFindByHandle)(i32 handle) = nullptr;
    // Script_Finish(script*) — tear down a finished pending script.
    void  (*scriptFinish)(void* script) = nullptr;

    // --- the character animation table (Character_*) ---------------------------
    // ToggleAniPlayback(character*, pause) — pause(1)/resume(0) an actor's ani.
    void  (*characterToggleAni)(void* character, int pause) = nullptr;

    // --- the audio gate (Music_* / VoiceQueue_*) -------------------------------
    void  (*voiceFlushAll)() = nullptr;        // VoiceQueue_FlushAll
    void  (*musicPlayCutscene)() = nullptr;    // Music_PlayCutsceneTrack
    void  (*musicRestore)() = nullptr;         // Music_RestoreAfterCutscene

    // --- the tick proc registry (TimeBase_*) -----------------------------------
    // RegisterProc(proc, interval) / UnregisterProc(proc): the engine drives
    // VIBE_Cutscene_LatchFrameCountToFloat at a fixed interval. We pass the
    // latch fn pointer through so the registry sees the same identity.
    void  (*timeBaseRegister)(void* proc, int interval) = nullptr;
    void  (*timeBaseUnregister)(void* proc) = nullptr;

    // --- the progress-bar widget (Object_* / Coord_*) --------------------------
    void  (*objectSetEnabled)(i32 widget, int enabled) = nullptr;
    void  (*objectSetValue)(i32 widget, int min, int max, int value) = nullptr;
};

void SetCutsceneMiscHooks(const CutsceneMiscHooks* hooks);
const CutsceneMiscHooks& GetCutsceneMiscHooks();

// ---------------------------------------------------------------------------
// Mutable globals these bodies read/write (the original's scattered dword_*).
// Bundled here so tests can seed and observe them. The active-processing /
// frame loop owns the real lifetime; we keep them faithful in value/semantics.
// ---------------------------------------------------------------------------
struct CutsceneMiscState {
    i32   tickCounter = 0;     // dword_6315A8 — incremented each TickCompareCounter
    i32   tickLimit   = 0;     // dword_6315AC — the comparison ceiling
    i32   tickExpired = 0;     // dword_6315B0 — latched (counter > limit)
    i32   frameCount  = 0;     // dword_631634 — frames since last latch (int)
    float frameRate   = 0.0f;  // dword_631630 — latched frame count as float
    i32   duelMode    = 0;     // dword_6315A4 — duel outcome/report mode flag
    i32   barStart    = 0;     // dword_6315F8 — progress-bar start tick
    i32   barSpan     = 0;     // dword_6315FC — progress-bar span (ms)
};
CutsceneMiscState& CutsceneMisc();

// The global millisecond clock (dword_62EB38 / g_gameTick). Owned elsewhere; the
// progress-bar math reads it. Tests set it explicitly. Default 0.
u32  CutsceneMiscGameTick();
void SetCutsceneMiscGameTick(u32 tick);

// ---------------------------------------------------------------------------
// gilde.exe 0x4a69b0 — VIBE_Cutscene_NullSub. The type-0 secondary fn — empty.
// ---------------------------------------------------------------------------
inline void CutsceneNullSub() {}

// gilde.exe 0x4a6908 — VIBE_Cutscene_RollDuelOutcomeTier
//   (__usercall, eax = range arg / result; ecx = record pointer `rec`).
// Rolls the duel AI choice / outcome tier and stores it at rec[+0x94] (+148):
//   if (duelMode)                     // dword_6315A4 != 0  -> 100-sided outcome
//       r = RandInt(100);
//       rec[+148] = (r > 50) ? 4 : (r <= 15 ? 3 : 2);
//   else                              // 10-sided AI choice
//       r = RandInt(10);
//       rec[+148] = (r > 7) ? 1 : 0;
//   returns the raw roll `r`. `rng` is the cutscene-local LCG.
i32 CutsceneRollDuelOutcomeTier(CutsceneRng& rng, u8* rec);

// gilde.exe 0x4a7fbc — VIBE_Cutscene_CheckDeathTimer  (__fastcall(a1, a2)).
//   Person_FindRecordById(a1, a2);  // side-effect lookup, result ignored
//   return Building_ComputeCurrentOutput() <= 0.0;   // st(0) <= 0
// The two leaves are deterministic-from-the-callbacks: we take the computed
// "current output" value directly and return whether it has hit/passed zero.
bool CutsceneCheckDeathTimer(double currentOutput);

// gilde.exe 0x4aa5e4 — VIBE_Cutscene_TickCompareCounter
//   result = (dword_6315A8 > dword_6315AC);   // counter > limit  (signed)
//   dword_6315B0 = result;                     // latch
//   ++dword_6315A8;                            // advance counter
//   return result;
bool CutsceneTickCompareCounter();

// gilde.exe 0x4ad4ac — VIBE_Cutscene_LatchFrameCountToFloat
//   *(float*)&dword_631630 = (float)dword_631634;   // fild/fstp
//   dword_631634 = 0;
void CutsceneLatchFrameCountToFloat();

// gilde.exe 0x4ad4c4 — VIBE_Cutscene_RegisterTickProc
//   TimeBase_RegisterProc(LatchFrameCountToFloat, 71);
//   Light_SetGrayColorThunk(0, 768); Light_SetGrayColorThunk(0, 256);
//   return dword_B5F810;
// The two SetGrayColor thunks are pure render-state pokes; routed through the
// hook is unnecessary (they take literal args), so we model only the registry
// call (the load-bearing identity) and leave the render pokes to the host. The
// registration interval (71) is preserved.
void CutsceneRegisterTickProc();

// gilde.exe 0x4ad4fc — VIBE_Cutscene_UnregisterTickProc
//   return TimeBase_UnregisterProc(LatchFrameCountToFloat);
void CutsceneUnregisterTickProc();

// gilde.exe 0x4aa944 — VIBE_Cutscene_PauseGame
//   if (!dword_6315BC) { VoiceQueue_FlushAll(); Music_PlayCutsceneTrack(); }
void CutscenePauseGame();

// gilde.exe 0x4aa960 — VIBE_Cutscene_ResumeGame
//   if (!dword_6315BC) return Music_RestoreAfterCutscene();
void CutsceneResumeGame();

// gilde.exe 0x4aa14c — VIBE_Cutscene_FinishPendingScripts
//   for each script record r (stride 2584, 128 records):
//     if (r.handle(+128) != -1 && (r.flags(+164) & 1) && r.kind(+132) == -2)
//       Script_Finish(&r);
//   return 0;
// `table` points at the 128-record array (or null to scan the host table). We
// expose a record-array form so the scan is testable byte-for-byte.
i32 CutsceneFinishPendingScripts(u8* table);

// gilde.exe 0x4aa188 — VIBE_Cutscene_WaitForPendingScripts
//   while (RunFrameLoop(...)) {            // pump frames
//     for each record r: if any record has handle != -1 && kind == -2, keep
//       waiting; otherwise return.
//   }
// The frame pump is a leaf (returns 0 when the loop should stop); we model it as
// `pumpFrame()` returning nonzero to continue. Returns when no pending (-2,
// non-empty) record remains, or the pump signals stop. `table` as above.
i32 CutsceneWaitForPendingScripts(u8* table, int (*pumpFrame)());

// gilde.exe 0x4ab4e0 — VIBE_Cutscene_PauseAllActorAni
//   for i in [0,512): c = dword_66F0D0[i];
//     if (c && !(c[+140] & 4)) Character_ToggleAniPlayback(c, 1);
// `chars` is the 512-entry Character* table (entry 0 == empty/null). The
// TimeBase_SetProcInterval(ComputeGameTimeOfDay, 1) prologue is a host poke; we
// keep the sweep faithful.
void CutscenePauseAllActorAni(void** chars);

// gilde.exe 0x4ab520 — VIBE_Cutscene_ResumeAllActorAni
//   for i in [0,512): c = dword_66F0D0[i];
//     if (c && (c[+140] & 4)) Character_ToggleAniPlayback(c, 0);
void CutsceneResumeAllActorAni(void** chars);

// gilde.exe 0x4acb4c — VIBE_Cutscene_UpdateProgressBar
//   (__usercall, eax = widget, edx = span, ebx = reset).
//   if (reset)  { dword_6315FC = span; dword_6315F8 = gameTick;
//                 Object_SetEnabled(widget, 0); value = (uninit -> see note); }
//   else        { elapsed = gameTick - dword_6315F8;
//                 v = (1 - elapsed/span) * width;  Coord_ConvertX();
//                 value = (v <= 0) ? 0 : (int)v; }
//   Object_SetValueOrText(widget, 0, 100, value);
// Returns the clamped fill value (0 on the reset path the original leaves the
// register undefined; we report 0 to make the reset path well-defined).
i32 CutsceneUpdateProgressBar(i32 widget, i32 span, bool reset);

} // namespace guild::sim
