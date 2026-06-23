#pragma once
// ===========================================================================
// cutscene_misc2.{h,cpp} — the second batch of cutscene leaf/driver bodies
// (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// Wave 14 slice of VIBE_Cutscene_*: the script-runner family (the frame-pump
// loops that drive a loaded scene script to completion / a deadline / a skip),
// the fade-in / sky-create / sky-destroy presentation helpers, the duel-window
// dispatch, the birth-participant eligibility check, the cutscene teardown
// sequence and the per-participant state-restore broadcast.
//
// Each body is translated 1:1 from its Hex-Rays pseudocode. Every cross-module
// leaf (GameLogic_RunFrameLoop / Script_* / Fade_* / Sky_* / Person_* / the
// duel windows / the command broadcast) is routed through Cutscene2Hooks with
// inert defaults defined in cutscene_misc2.cpp, so the deterministic control
// flow / arithmetic is faithfully reproduced and unit-testable in isolation.
//
// RECOVERED GLOBALS / CONSTANTS
//   * dword_6315BC — the global "cutscene disabled / remote replay" gate. When
//     nonzero every script runner / fade / sky / salon body short-circuits.
//     (Shared with cutscene_misc; we reference the same Cutscene2State.replayGate.)
//   * dword_631598 — the frame-loop "flags" dword; the runners OR byte1 with
//     0x80 (LOBYTE / BYTE1 |= 0x80) before each RunFrameLoop call. We reproduce
//     the bit-set exactly and pass the value to the pump hook.
//   * dword_631614 — the frame-loop "force-quit / advance" latch the runners set
//     when their deadline (timed/delayed) or a script skip fires.
//   * dword_672230 — the "skip pressed" gate read by RunScriptUntilSkip /
//     RunTimedScript: when nonzero, the skip callback (or 1) is returned.
//   * dword_62EB38 — the global millisecond clock (g_gameTick); the timed/delayed
//     runners snapshot it and compare against (frames * scale + start).
//   * dword_6315A8 — the cutscene tick counter (RunCombatScript deadline source).
//   * dword_6315B4 — the per-frame "frame budget" dword (frames-per-tick); used
//     by the duel windows for the progress-bar span (75 * dword_6315B4).
//   * dbl_61D7A4 == dbl_61D7AC == 1/14 (0.0714285714...) — the frame->ms scale
//     used by RunTimedScript / RunDelayedScript (recovered IEEE-754 0x3FB2492492492493).
//   * dword_6315CC — the cutscene nesting depth (Teardown decrements it; only
//     runs when >0). dword_6315D0 / dword_62E8DC — the two live script handles.
//   * dword_6315A4 — the duel outcome/report-mode flag (ShowDuelWindow dispatch).
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "sim/cutscene.h"   // CutsceneSlot

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// dbl_61D7A4 / dbl_61D7AC == 1/14 — frames-per-millisecond budget scale.
constexpr double kFrameMsScale = 0.07142857142857144;  // 1/14

constexpr u8 kFrameLoopHiBit = 0x80;   // byte1 of dword_631598 OR'd before pump

// Person record +2 "kind" byte values the cutscene bodies branch on.
constexpr u8 kPersonKindBride   = 6;   // (kind == 6 || kind == 7) -> vow line
constexpr u8 kPersonKindGroom   = 7;
constexpr u8 kPersonKindDeceased = 15; // parent kind 15 -> birth aborts

// ---------------------------------------------------------------------------
// Mutable globals these bodies read/write. The frame loop / cutscene driver
// owns the real lifetime; bundled here so tests can seed/observe. (The replay
// gate mirrors cutscene_misc's dword_6315BC value; both are seeded by tests.)
// ---------------------------------------------------------------------------
struct Cutscene2State {
    i32 replayGate   = 0;   // dword_6315BC — disabled / remote replay
    i32 frameFlags   = 0;   // dword_631598 — frame-loop flags dword
    i32 forceQuit    = 0;   // dword_631614 — force-advance / quit latch
    i32 skipGate     = 0;   // dword_672230 — skip-pressed gate
    u32 gameTick     = 0;   // dword_62EB38 — ms clock
    i32 tickCounter  = 0;   // dword_6315A8 — cutscene tick counter
    i32 frameBudget  = 0;   // dword_6315B4 — frames-per-tick budget
    i32 nestDepth    = 0;   // dword_6315CC — cutscene nesting depth
    i32 scriptHandleA = -1; // dword_6315D0 — primary live script handle
    i32 scriptHandleB = -1; // dword_62E8DC — secondary live script handle
    i32 duelMode     = 0;   // dword_6315A4 — duel outcome/report mode
};
Cutscene2State& Cutscene2();

// ---------------------------------------------------------------------------
// Leaf hooks — the cross-module side effects. Tests install a recording mock;
// the inert default (all-null) makes every leaf a deterministic no-op.
// ---------------------------------------------------------------------------
struct Cutscene2Hooks {
    // --- the frame pump (GameLogic_RunFrameLoop) -------------------------------
    // pumpFrame(flags, a, payload) — runs ONE frame; returns nonzero to keep
    // looping, 0 to stop. The runners pass dword_631598 (with byte1|0x80) as flags.
    int (*pumpFrame)(i32 flags, i32 a, void* payload) = nullptr;

    // --- the script table (Script_*) -------------------------------------------
    void* (*scriptLoadFromDir)(const char* name) = nullptr; // Script_LoadFromScriptDir
    void  (*scriptRunMain)(void* script) = nullptr;          // Script_RunMain
    void* (*scriptFindByHandle)(i32 handle) = nullptr;       // Script_FindByHandle
    void  (*scriptFinish)(void* script) = nullptr;           // Script_Finish
    // Script_FindByHandle returns an opaque record; for the Teardown busy-wait we
    // need its +164 flags byte (bit0 == running). Returns the byte (0 if gone).
    u8    (*scriptHandleFlags)(i32 handle) = nullptr;

    // --- presentation: fade / sky ----------------------------------------------
    // Fade_Register(...) -> opaque fade* (its [0] byte bit2 == "done"). The
    // FadeIn body spins until done. We model "frames until done" as a counter.
    void* (*fadeRegister)() = nullptr;
    // fadeDoneBit(fade*) -> the fade's [0] byte (bit2 set == finished).
    u8    (*fadeDoneByte)(void* fade) = nullptr;
    void* (*skyCreate)() = nullptr;            // Sky_Create
    void* (*skyCreateLayer)(void* sky) = nullptr; // Sky_CreateLayer
    void  (*skyConfigLayer)(void* sky, void* layer) = nullptr; // scroll/fade setup
    void  (*skyRemoveLayer)(void* sky, void* layer) = nullptr;
    void  (*skyDestroy)(void* sky) = nullptr;

    // --- frame-side combat updates (RunCombat/Timed/Delayed presentation) ------
    void  (*combatUpdateDamageNumbers)() = nullptr;
    void  (*showParticipantDialog)(i32 slot) = nullptr;  // current dialog slot

    // --- duel windows (ShowDuelWindow dispatch) --------------------------------
    int (*duelOutcomeWindow)(i32 a1, i32 a2, i32 a3) = nullptr;
    int (*duelChoiceWindow)(i32 a1, i32 a3) = nullptr;

    // --- birth-participant check leaves ----------------------------------------
    // resolvePerson(id) -> opaque person record (0 == not found). +2 is the kind
    // byte (kindOf), +92 is the parent person id (parentOf).
    void* (*resolvePerson)(i32 personId) = nullptr;
    u8    (*personKind)(void* person) = nullptr;
    i32   (*personParentId)(void* person) = nullptr;
    // QueueRequestPair33(personId, 1) — the "child stillborn / parent dead" cmd.
    void  (*queueBirthFailure)(i32 personId) = nullptr;
    // BuildSpeechPacket(speaker, slot, font, text) — emit the vow/birth line.
    void  (*buildSpeechPacket)(void* speaker, const CutsceneSlot* slot) = nullptr;

    // --- teardown leaves --------------------------------------------------------
    void  (*sceneTeardown)() = nullptr;          // heightmap/anim free + slot reset

    // --- per-participant state restore (RestoreParticipantState) ----------------
    // The original stages a cmd28 block then RequestSendCutInfo + busy-waits on
    // the packet status. We model it as one "restore broadcast" per matched part.
    void  (*restoreBroadcast)(i32 person, i32 slotId) = nullptr;
};

void SetCutscene2Hooks(const Cutscene2Hooks* hooks);
const Cutscene2Hooks& GetCutscene2Hooks();

// ===========================================================================
// gilde.exe 0x4aa01c — VIBE_Cutscene_LoadAndRunScript(name@<eax>).
//   if (dword_6315BC) return 0;
//   s = Script_LoadFromScriptDir(name); if (!s) return s;
//   Script_RunMain(s); return s[+128];   // the started script handle
// Returns the running script handle (nullptr when gated or load failed). We
// surface the handle via the load hook returning the record and a +128 read
// modelled as `scriptHandle` out-param.
void* CutsceneLoadAndRunScript(const char* name, i32* scriptHandleOut);

// ===========================================================================
// gilde.exe 0x4aa06c — VIBE_Cutscene_RunScriptLoop(payload@<edi>).
//   if (dword_6315BC) return <undef>;   (gated: returns 0 here)
//   do { v = pump(631598|byte1<<8 0x80, 0, payload);
//        if (!v) break; v = Script_FindByHandle(h); } while (v);
//   return v;
// Pumps frames while the tracked script handle is still alive. Returns the last
// pump/find result. `handle` is the script handle the loop re-queries each frame.
i32 CutsceneRunScriptLoop(void* payload, i32 handle);

// ===========================================================================
// gilde.exe 0x4aa0a4 — VIBE_Cutscene_RunScriptUntilSkip(skipCb@<edx>, a2@<ebx>).
//   if (dword_6315BC) return 0;
//   while (1) { r = pump(631598|0x80<<8, a2, 0); if (!r) break;
//               r = Script_FindByHandle(h); if (!r) break;
//               if (dword_672230) return skipCb ? skipCb() : 1; }
//   return r;
// Pumps frames until the script ends or a skip is pressed. `handle` is the live
// script handle; on skip, calls skipCb (or returns 1 if null).
i32 CutsceneRunScriptUntilSkip(int (*skipCb)(), i32 a2, i32 handle);

// ===========================================================================
// gilde.exe 0x4aa0fc — VIBE_Cutscene_RunScriptWait(payload@<edi>).
//   if (dword_6315BC) return <undef> (0);
//   do { v = pump(497414, 0, payload);   // literal flags 0x79706 == 497414
//        if (!v) break; v = Script_FindByHandle(h); } while (v);
//   return v;
// Like RunScriptLoop but with the fixed literal flags 497414. `handle` re-queried.
i32 CutsceneRunScriptWait(void* payload, i32 handle);

// ===========================================================================
// gilde.exe 0x4aa808 — VIBE_Cutscene_RunTimedScript(frames@<eax>, a2@<ebx>).
//   start = dword_62EB38; v5 = 0; if (dword_6315BC) return 0;
//   while (1) {
//     if (!pump(631598|0x80<<8, a2, frames)) return v5;
//     if (dword_672230) { v5 = skipCb ? skipCb() : 1; dword_631614 = 1; }
//     else if ((double)dword_62EB38 >= frames*scale + start) dword_631614 = 1;
//     Combat_UpdateDamageNumbers(); ShowParticipantDialog(dword_6315C0);
//   }
// Runs frames until the (frames * 1/14)-ms deadline elapses or skip. Returns the
// skip-callback result (0 if it timed out / no skip). `skipCb` may be null.
i32 CutsceneRunTimedScript(i32 frames, i32 a2, i32 (*skipCb)());

// ===========================================================================
// gilde.exe 0x4aa8bc — VIBE_Cutscene_RunDelayedScript().
//   start = dword_62EB38; if (dword_6315BC) return <undef>(0);
//   while (1) { v = pump(631598|0x80<<8, start, (char*)1); if (!v) break;
//     if ((double)dword_62EB38 >= frames*scale + start) dword_631614 = 1;
//     Combat_UpdateDamageNumbers(); ShowParticipantDialog(dword_6315C0); }
//   return v;
// Pumps a fixed delay (frames * 1/14 ms) then latches the quit flag. The frame
// count comes from an inbound register (v4) — here `frames`. Returns last pump.
i32 CutsceneRunDelayedScript(i32 frames, i32 dialogSlot);

// ===========================================================================
// gilde.exe 0x4aa7b0 — VIBE_Cutscene_RunCombatScript(deadline@<ebx>, payload@<edi>).
//   if (dword_6315BC) goto join;
//   while (1) { if (!pump(631598|0x80<<8, deadline, payload)) break;
//     if (deadline <= dword_6315A8) dword_631614 = 1;
//     Combat_UpdateDamageNumbers(); ShowParticipantDialog(dword_6315C0); }
//   (tail JUMPOUT into the duel main — modelled as plain return here)
// Pumps frames until the cutscene tick counter reaches `deadline`. `dialogSlot`
// is the dword_6315C0 dialog target.
void CutsceneRunCombatScript(i32 deadline, void* payload, i32 dialogSlot);

// ===========================================================================
// gilde.exe 0x4aa450 — VIBE_Cutscene_FadeIn(a1@<ebx>, payload@<edi>).
//   if (dword_6315BC) return <undef>(0);
//   dword_6315D4 = Fade_Register(...);
//   while (!(*(byte*)dword_6315D4 & 4)) pump(631598|0x80, a1, payload);
//   return last;
// Registers a fade-to-black then pumps frames until the fade's [0]&4 done bit
// is set. We model the fade via fadeRegister()/fadeDoneByte(); the inert default
// reports done immediately so the loop runs zero frames.
i32 CutsceneFadeIn(i32 a1, void* payload);

// ===========================================================================
// gilde.exe 0x4aa6a8 — VIBE_Cutscene_SetupSky(a1@<eax>).
//   if (dword_6315BC) return;
//   sky = Sky_Create(...); g_skyHandle = sky;
//   layer = Sky_CreateLayer(sky,...); Sky_SetLayerScrollSpeed; Sky_SetLayerFade;
// Builds the sky + one scroll/fade layer (stored in the sky-handle pair).
void CutsceneSetupSky(unsigned int* a1);

// ===========================================================================
// gilde.exe 0x4aa740 — VIBE_Cutscene_DestroySky(a1@<edi>, a2@<esi>).
//   if (dword_6315BC) return <undef>(0);
//   if (layer) Sky_RemoveLayer(sky, layer); Sky_Destroy(sky); g_skyMirror = 0;
i32 CutsceneDestroySky();

// The sky handle pair (dword_6315F0 sky / dword_6315EC layer) Setup/Destroy use,
// plus the mirror global dword_64A7C8 (SetupSky sets it to the sky handle;
// DestroySky zeroes ONLY this mirror and leaves sky/layer untouched, exactly as
// the binary does — see 0x4aa6a8 / 0x4aa740).
struct CutsceneSky { void* sky = nullptr; void* layer = nullptr; void* mirror = nullptr; };
CutsceneSky& CutsceneSkyState();

// ===========================================================================
// gilde.exe 0x4a6964 — VIBE_Cutscene_ShowDuelWindow(a1@<eax>, a2@<edx>, a3@<ecx>).
//   return dword_6315A4 ? DuelOutcomeWindow(a1, a2, a3) : DuelChoiceWindow(a1, a3);
i32 CutsceneShowDuelWindow(i32 a1, i32 a2, i32 a3);

// ===========================================================================
// gilde.exe 0x4a7a64 — VIBE_Cutscene_CheckBirthParticipants(slot@<eax>).
//   a = resolve(slot[13]/+52); b = resolve(slot[14]/+56);
//   if (!a || !b) return 0;
//   parent = resolve(a[+92]);   // a's parent
//   if (!parent || parent.kind == 15) { QueueRequestPair33(b.id, 1); return 0; }
//   if (a.kind == 6 || a.kind == 7) { BuildSpeechPacket(...); return 1; }
//   return 1;
// `slot` is the cutscene slot; the two combatants are partIds[0]/[1] (+52/+56).
i32 CutsceneCheckBirthParticipants(const CutsceneSlot* slot);

// ===========================================================================
// gilde.exe 0x4aa4a8 — VIBE_Cutscene_Teardown(a1@<edi>).
//   if (dword_6315BC || dword_6315CC <= 0) return;
//   if (dword_6315D0 != -1) { s = FindByHandle(dword_6315D0); if (s) Finish(s); }
//   ... busy-wait dword_62E8DC running ...
//   FinishPendingScripts(); WaitForPendingScripts();
//   ... busy-wait dword_6315D0 running ...
//   <scene/anim/heightmap free + Universe_ResetCurrentSlot>;
//   --dword_6315CC;
// The two busy-waits pump frames while the handle's +164 flags bit0 is set. We
// model finish-pending / wait-pending and the scene teardown through hooks.
void CutsceneTeardown();

// ===========================================================================
// gilde.exe 0x4aab0c — VIBE_Cutscene_RestoreParticipantState(slot@<eax>, person@<edx>).
//   for i in [0, slot[+48]):
//     if (person == dword_11AB010[i*215]) {
//       stage cmd28 block (flags 1/1/1, byte5=0, [2]=0); RequestSendCutInfo;
//       busy-wait packet status; }
//   return slot[+48];
// The original keys on a per-participant id array (dword_11AB010, stride 215
// dwords). We expose the participant-id list directly so the match is testable.
// Returns the participant count. `ids` is `count` entries; on a match the
// restoreBroadcast hook fires for `person` against the slot id.
i32 CutsceneRestoreParticipantState(const CutsceneSlot* slot, i32 person,
                                    const i32* ids, int count);

} // namespace guild::sim
