#include "sim/cutscene_misc.h"

namespace guild::sim {

// ===========================================================================
// Leaf-hook plumbing + bundled mutable state.
// ===========================================================================
namespace {
const CutsceneMiscHooks* g_hooks = nullptr;
CutsceneMiscHooks        g_inert{};   // all-null + disabledGate 0 -> no-op
CutsceneMiscState        g_state{};
u32                      g_gameTick = 0;   // dword_62EB38
}

void SetCutsceneMiscHooks(const CutsceneMiscHooks* hooks) { g_hooks = hooks; }
const CutsceneMiscHooks& GetCutsceneMiscHooks() {
    return g_hooks ? *g_hooks : g_inert;
}

CutsceneMiscState& CutsceneMisc() { return g_state; }

u32  CutsceneMiscGameTick() { return g_gameTick; }
void SetCutsceneMiscGameTick(u32 tick) { g_gameTick = tick; }

// The LatchFrameCountToFloat fn identity passed to the tick-proc registry. Used
// only as an opaque key by RegisterTickProc / UnregisterTickProc (mirrors the
// original passing &VIBE_Cutscene_LatchFrameCountToFloat).
namespace { void LatchProcThunk() { CutsceneLatchFrameCountToFloat(); } }

// ===========================================================================
// gilde.exe 0x4a6908 — VIBE_Cutscene_RollDuelOutcomeTier
//   ecx = rec (record pointer); eax = the RandInt range arg & the returned roll.
//   Branch on dword_6315A4 (duel outcome/report mode). Writes the chosen tier
//   to rec[+0x94] (+148) and returns the raw roll.
// ===========================================================================
i32 CutsceneRollDuelOutcomeTier(CutsceneRng& rng, u8* rec) {
    if (CutsceneMisc().duelMode) {                  // dword_6315A4 != 0
        i32 r = static_cast<i32>(rng.RandInt(0x64u));   // RandInt(100)
        if (r > 50)                                 // cmp eax,32h ; jg
            rec[148] = 4;
        else if (r <= 15)                           // cmp eax,0Fh ; jle
            rec[148] = 3;
        else
            rec[148] = 2;
        return r;
    } else {
        i32 r = static_cast<i32>(rng.RandInt(0x0Au));   // RandInt(10)
        rec[148] = static_cast<u8>(static_cast<u32>(r) > 7u ? 1 : 0);  // (unsigned)>7
        return r;
    }
}

// ===========================================================================
// gilde.exe 0x4a7fbc — VIBE_Cutscene_CheckDeathTimer
//   The original calls Person_FindRecordById(a1,a2) (side-effect lookup whose
//   result is discarded) then returns Building_ComputeCurrentOutput() <= 0.0.
//   We take the computed output value directly (the deterministic predicate).
// ===========================================================================
bool CutsceneCheckDeathTimer(double currentOutput) {
    return currentOutput <= 0.0;
}

// ===========================================================================
// gilde.exe 0x4aa5e4 — VIBE_Cutscene_TickCompareCounter
//   result = (counter > limit);   // SIGNED compare (setg)
//   latch = result;  ++counter;  return result;
// ===========================================================================
bool CutsceneTickCompareCounter() {
    CutsceneMiscState& s = CutsceneMisc();
    bool result = s.tickCounter > s.tickLimit;   // dword_6315A8 > dword_6315AC
    s.tickExpired = result ? 1 : 0;              // dword_6315B0 = result
    ++s.tickCounter;                             // ++dword_6315A8
    return result;
}

// ===========================================================================
// gilde.exe 0x4ad4ac — VIBE_Cutscene_LatchFrameCountToFloat
//   fild dword_631634 ; fstp dword_631630 ; dword_631634 = 0
// dword_631634 is a SIGNED int frame count (fild treats it as int32).
// ===========================================================================
void CutsceneLatchFrameCountToFloat() {
    CutsceneMiscState& s = CutsceneMisc();
    s.frameRate  = static_cast<float>(s.frameCount);   // (float)dword_631634
    s.frameCount = 0;                                  // dword_631634 = 0
}

// ===========================================================================
// gilde.exe 0x4ad4c4 — VIBE_Cutscene_RegisterTickProc
//   TimeBase_RegisterProc(LatchFrameCountToFloat, 71); + two render pokes;
//   returns dword_B5F810 (a render handle, not modelled here).
// ===========================================================================
void CutsceneRegisterTickProc() {
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    if (h.timeBaseRegister)
        h.timeBaseRegister(reinterpret_cast<void*>(&LatchProcThunk), 71);
}

// ===========================================================================
// gilde.exe 0x4ad4fc — VIBE_Cutscene_UnregisterTickProc
//   TimeBase_UnregisterProc(LatchFrameCountToFloat);
// ===========================================================================
void CutsceneUnregisterTickProc() {
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    if (h.timeBaseUnregister)
        h.timeBaseUnregister(reinterpret_cast<void*>(&LatchProcThunk));
}

// ===========================================================================
// gilde.exe 0x4aa944 — VIBE_Cutscene_PauseGame
//   if (!dword_6315BC) { VoiceQueue_FlushAll(); Music_PlayCutsceneTrack(); }
// ===========================================================================
void CutscenePauseGame() {
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    if (!h.disabledGate) {                  // !dword_6315BC
        if (h.voiceFlushAll)     h.voiceFlushAll();
        if (h.musicPlayCutscene) h.musicPlayCutscene();
    }
}

// ===========================================================================
// gilde.exe 0x4aa960 — VIBE_Cutscene_ResumeGame
//   if (!dword_6315BC) return Music_RestoreAfterCutscene();
// ===========================================================================
void CutsceneResumeGame() {
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    if (!h.disabledGate) {                  // !dword_6315BC
        if (h.musicRestore) h.musicRestore();
    }
}

// ===========================================================================
// gilde.exe 0x4aa14c — VIBE_Cutscene_FinishPendingScripts
//   for (i = 0; i != 330752; i += 2584):
//     r = table + i;
//     if (*(dword*)(r+128) != -1 && (*(byte*)(r+164) & 1) && *(dword*)(r+132)==-2)
//       Script_Finish(r);
//   return 0;
// ===========================================================================
i32 CutsceneFinishPendingScripts(u8* table) {
    if (!table) return 0;
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    for (int i = 0; i != kScriptRecordCount * kScriptRecordStride;
         i += kScriptRecordStride) {
        u8* r = table + i;
        i32 handle = *reinterpret_cast<i32*>(r + kScriptOffHandle);   // +128
        u8  flags  = r[kScriptOffFlags];                             // +164
        i32 kind   = *reinterpret_cast<i32*>(r + kScriptOffKind);    // +132
        if (handle != -1 && (flags & 1) != 0 && kind == kScriptPendingKind) {
            if (h.scriptFinish) h.scriptFinish(r);
        }
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x4aa188 — VIBE_Cutscene_WaitForPendingScripts
//   while (RunFrameLoop(...)) {
//     i = 0;
//     while (table[i+128]==-1 || table[i+132]!=-2) {   // skip empty / non-pending
//       i += 2584; if (i >= 330752) return i;          // none pending -> done
//     }
//     // (fall through to pump another frame while a pending record exists)
//   }
//   return <frame-loop result>;
// We model RunFrameLoop as `pumpFrame()` returning nonzero to keep pumping.
// ===========================================================================
i32 CutsceneWaitForPendingScripts(u8* table, int (*pumpFrame)()) {
    if (!table) return 0;
    int result;
    while ((result = pumpFrame ? pumpFrame() : 0) != 0) {
        int i = 0;
        while (true) {
            i32 handle = *reinterpret_cast<i32*>(table + i + kScriptOffHandle);
            i32 kind   = *reinterpret_cast<i32*>(table + i + kScriptOffKind);
            if (handle != -1 && kind == kScriptPendingKind)
                break;                       // a pending record exists -> pump
            i += kScriptRecordStride;
            if (i >= kScriptRecordCount * kScriptRecordStride)
                return i;                    // none pending -> done
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4ab4e0 — VIBE_Cutscene_PauseAllActorAni
//   for (i=0; i!=512; ++i): c = chars[i];
//     if (c && !(c[+140] & 4)) Character_ToggleAniPlayback(c, 1);
// ===========================================================================
void CutscenePauseAllActorAni(void** chars) {
    if (!chars) return;
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    for (int i = 0; i != kCharacterTableCount; ++i) {
        void* c = chars[i];
        if (c) {
            u8 aniFlags = reinterpret_cast<u8*>(c)[kCharacterOffAniFlags];  // +140
            if ((aniFlags & kCharAniPausedBit) == 0) {
                if (h.characterToggleAni) h.characterToggleAni(c, 1);
            }
        }
    }
}

// ===========================================================================
// gilde.exe 0x4ab520 — VIBE_Cutscene_ResumeAllActorAni
//   for (i=0; i!=512; ++i): c = chars[i];
//     if (c && (c[+140] & 4)) Character_ToggleAniPlayback(c, 0);
// ===========================================================================
void CutsceneResumeAllActorAni(void** chars) {
    if (!chars) return;
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    for (int i = 0; i != kCharacterTableCount; ++i) {
        void* c = chars[i];
        if (c) {
            u8 aniFlags = reinterpret_cast<u8*>(c)[kCharacterOffAniFlags];  // +140
            if ((aniFlags & kCharAniPausedBit) != 0) {
                if (h.characterToggleAni) h.characterToggleAni(c, 0);
            }
        }
    }
}

// ===========================================================================
// gilde.exe 0x4acb4c — VIBE_Cutscene_UpdateProgressBar
//   eax = widget, edx = span, ebx = reset.
//   reset path:  dword_6315FC = span; dword_6315F8 = gameTick;
//                Object_SetEnabled(widget, 0);            // value stays 0
//   tick path:   elapsed = gameTick - dword_6315F8;       // unsigned
//                v = (1.0 - (double)elapsed/(double)span) * 100.0;  // flt_61D94C
//                value = (v <= 0) ? 0 : (int)v;            // truncate, clamp lo
//   Object_SetValueOrText(widget, 0, 100, value);
// ===========================================================================
i32 CutsceneUpdateProgressBar(i32 widget, i32 span, bool reset) {
    const CutsceneMiscHooks& h = GetCutsceneMiscHooks();
    CutsceneMiscState& s = CutsceneMisc();
    i32 value = 0;
    if (reset) {                                    // ebx != 0
        s.barSpan  = span;                          // dword_6315FC = span
        s.barStart = static_cast<i32>(CutsceneMiscGameTick());  // dword_6315F8
        if (h.objectSetEnabled) h.objectSetEnabled(widget, 0);
    } else {
        u32 elapsed = CutsceneMiscGameTick() - static_cast<u32>(s.barStart);
        double v = (1.0 - static_cast<double>(elapsed)
                          / static_cast<double>(s.barSpan))
                   * static_cast<double>(kProgressBarWidth);
        if (static_cast<int>(v) <= 0)
            value = 0;
        else
            value = static_cast<int>(v);
    }
    if (h.objectSetValue) h.objectSetValue(widget, 0, 100, value);
    return value;
}

} // namespace guild::sim
