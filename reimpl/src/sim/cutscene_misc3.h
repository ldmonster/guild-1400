#pragma once
// ===========================================================================
// cutscene_misc3.{h,cpp} — the per-type cutscene "main" bodies and their
// presentation siblings (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// Wave 14 slice of VIBE_Cutscene_*: the life-event cutscene mains
// (Execution / Death / Birth / Bankruptcy), the salon flow
// (Salon / SalonFadeTransition / LoadScene), the lease window pair
// (LeaseWindow / LeaseAutoResolve), the duel window pair
// (DuelChoiceWindow / DuelOutcomeWindow), the participant run driver
// (RunParticipants / ShowParticipantDialog), and the small leaves
// (SetupCallbacks / FormatLodDebug / PlayTobyScene).
//
// Each body is translated 1:1 from its Hex-Rays pseudocode. The cutscene mains
// are long sequences of cross-module side effects (scene load, voice, sky,
// script, frame-pump, command-staging) wrapped around a small DETERMINISTIC
// decision kernel: the seasonal-window pick, the gender/child voice-line index
// selection, the lease accept/counter arithmetic, the duel outcome tier and the
// duel window button -> choice mapping. We route every cross-module leaf through
// CutsceneMisc3Hooks (inert defaults defined in cutscene_misc3.cpp) and expose
// the deterministic kernels as standalone testable free functions, then drive
// the full flows over the hook surface so the control flow is byte-faithful.
//
// The cutscene-local RNG, the slot record (CutsceneSlot) and the slot table are
// owned by sim/cutscene.{h,cpp} (CutsceneRng); we REUSE them — the per-type
// mains snapshot/restore the RNG seed (GetRandSeed/SetRandSeed) and consume
// RandInt exactly as the original.
//
// RECOVERED TABLES / CONSTANTS
//   * dword_49D8B8 (Death) / dword_49D8D0 (Bankruptcy): the seasonal day-window
//     table {8,10,12,15,18,20} — 6 ascending day thresholds. The body walks the
//     table and the season index is the first slot whose threshold is >= the
//     current day-of-month (WORD2(qword_13CE852)); 6 if none (default v1=6 then
//     overwritten by the matched index). Used as the SkyColor band-lighting arg.
//   * dword_49D8A4 (Execution): the 5-entry RunTimedScript duration table
//     {14000, 12000, 12000, 15000, 10000} ms; the intro duration is element
//     RandInt(3) (so one of the first three: 14000 / 12000 / 12000).
//   * flt_61D718 == 1/6 (0.16666667), flt_61D71C == 0.3, flt_61D720 == 0.1 —
//     the lease auto-resolve "willingness" coefficients (LeaseAutoResolve).
//   * Birth voice-line base indices: both parents "ill" byte(+9)!=0 -> base 6;
//     father-only -> base 2; mother-only -> base 4; neither -> base 0; the line
//     index is base + RandInt(2). The "geschrei" burst plays RandInt(4)+2 lines.
//   * dword_6315A4 — the duel "outcome/report mode" flag; RunParticipants
//     increments it on exit, ShowDuelWindow selects outcome vs choice on it.
//   * Duel window button ids (dword_75BF38): 1210 == "accept/fight" (choice 1 /
//     outcome action), 1155 == "decline" (choice 0). DuelOutcomeWindow maps the
//     three child-object ids to slot+148 == 2 / 3 / 4.
// ---------------------------------------------------------------------------
#include "guild/common/types.h"
#include "sim/cutscene.h"   // CutsceneSlot, CutsceneRng

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered tables / constants.
// ---------------------------------------------------------------------------
// dword_49D8B8 / dword_49D8D0 — the 6-entry seasonal day-window thresholds.
constexpr int kSeasonWindowCount = 6;
extern const i32 kSeasonDayWindows[kSeasonWindowCount];  // {8,10,12,15,18,20}

// dword_49D8A4 — Execution intro RunTimedScript durations (ms).
constexpr int kExecutionDurCount = 5;
extern const i32 kExecutionDurations[kExecutionDurCount]; // {14000,12000,12000,15000,10000}

// LeaseAutoResolve willingness coefficients.
constexpr float kLeaseWillBase  = 0.16666667f;  // flt_61D718  (1/6)
constexpr float kLeaseWillScale = 0.30000001f;  // flt_61D71C
constexpr float kLeaseCounterCoef = 0.10000000f;// flt_61D720

// Person record +9 "ill / sick" byte (read by Birth) and +2 "kind" byte.
constexpr int kPersonOffKind = 2;   // kind 6/7 == bride/groom-class
constexpr int kPersonOffIll  = 9;   // nonzero == ill

// Duel window button ids (dword_75BF38).
constexpr i32 kDuelBtnAccept  = 1210;  // fight / accept
constexpr i32 kDuelBtnDecline = 1155;  // decline

// Lease window accept button id (dword_75BF38).
constexpr i32 kLeaseBtnAccept = 1210;

// ---------------------------------------------------------------------------
// Mutable globals these bodies read/write (the original's scattered dword_*).
// Bundled so tests can seed/observe. The frame loop / driver owns the real
// lifetime; we keep them faithful in value/semantics. (replayGate mirrors
// dword_6315BC; duelMode mirrors dword_6315A4 — both shared conceptually with
// the misc/misc2 state but kept here to keep this module self-contained.)
// ---------------------------------------------------------------------------
struct Cutscene3State {
    i32 replayGate   = 0;   // dword_6315BC — disabled / remote-replay gate
    i32 frameFlags   = 0;   // dword_631598 — frame-loop flags dword
    i32 forceQuit    = 0;   // dword_631614 — force-advance / quit latch
    i32 duelMode     = 0;   // dword_6315A4 — duel outcome/report mode (++ on RunParticipants exit)
    i32 dayOfMonth   = 0;   // WORD2(qword_13CE852) — current day-of-month
    i32 buttonId     = -1;  // dword_75BF38 — last-clicked widget id (-1 == none)
};
Cutscene3State& Cutscene3();

// ---------------------------------------------------------------------------
// Leaf hooks — the cross-module side effects these bodies make. Tests install a
// recording mock; the inert default (all null / inert) makes every leaf a
// deterministic no-op so the kernels can be exercised in isolation. The frame
// pump returns 0 by default ("stop") so every frame loop terminates.
// ---------------------------------------------------------------------------
struct CutsceneMisc3Hooks {
    // --- the frame pump (GameLogic_RunFrameLoop) -------------------------------
    // Returns nonzero to keep looping, 0 to stop. The mains pass dword_631598
    // (with byte1|0x80 for the script-pump variants).
    int (*pumpFrame)(i32 flags, i32 a, void* payload) = nullptr;

    // --- audio / music ---------------------------------------------------------
    void  (*voiceFlushAll)() = nullptr;            // VoiceQueue_FlushAll
    void  (*musicPlayCutscene)(const char* trk) = nullptr; // Music_PlayCutsceneTrack
    void  (*musicRestore)() = nullptr;             // Music_RestoreAfterCutscene
    void* (*voiceLoadBank)(const char* name) = nullptr;    // Voice_LoadLanguageBank
    void  (*voicePlaySample)(int ch, void* bank, int idx, const char* tag, int delay) = nullptr;
    void  (*voiceUnloadBank)(void* bank) = nullptr; // Audio_UnloadSampleBank
    void  (*audioStartSample)(int a, int b, int c, int vol) = nullptr; // Audio_StartVoiceSample

    // --- scene / sky / fade ----------------------------------------------------
    void  (*loadScene)(const char* file) = nullptr;     // Cutscene_LoadScene
    void  (*setupSky)(const char* sky) = nullptr;       // Cutscene_SetupSky
    void  (*skyColorBand)(int idx) = nullptr;           // SkyColor_BlendBandLighting
    void  (*skyColorAmbient)(float blend) = nullptr;    // SkyColor_ApplyAmbientBlend
    void  (*destroySky)() = nullptr;                    // Sky_RemoveLayer + Sky_Destroy
    void  (*fadeIn)(int a) = nullptr;                   // Cutscene_FadeIn
    void  (*teardown)() = nullptr;                      // Cutscene_Teardown
    void* (*rainCreate)(int n) = nullptr;               // Rain_Create
    void  (*rainDestroy)() = nullptr;                   // Rain_Destroy

    // --- script ----------------------------------------------------------------
    // Returns the started script handle (0 if none / gated).
    i32   (*scriptLoadRun)(const char* file) = nullptr; // Script_LoadFromScriptDir + RunMain
    // FindByHandle(handle) -> nonzero while the script is still alive.
    int   (*scriptAlive)(i32 handle) = nullptr;         // Script_FindByHandle
    void  (*scriptFinish)(i32 handle) = nullptr;        // Script_Finish

    // --- the timed-script pump (Cutscene_RunTimedScript) -----------------------
    // Returns nonzero when the user skipped (cutscene aborted early). The mains
    // chain on the result: a nonzero return short-circuits the rest.
    int   (*runTimedScript)(i32 durationMs) = nullptr;

    // --- person / inheritance --------------------------------------------------
    void* (*personFind)(i32 id) = nullptr;              // Person_FindRecordById
    // person field reads (the bodies index the 536-byte record):
    u8    (*personKind)(void* person) = nullptr;        // +2
    u8    (*personIll)(void* person) = nullptr;         // +9
    int   (*personCountAdultChildren)(void* person) = nullptr; // Person_CountAdultChildren
    void  (*distributeInheritance)(void* person, int withChildren) = nullptr;

    // --- ui / dialog -----------------------------------------------------------
    void  (*showMessageBox)(int kind) = nullptr;        // Dialog_ShowMessageBoxBig
    void  (*runFamilyTreeWindow)() = nullptr;           // Stammbaum_RunFamilyTreeWindow
    void  (*reloadSession)() = nullptr;                 // Hud_FindModeIndex(GameLogic_InitOrLoadSession)
    void  (*showParticipantDialog)(i32 partCount) = nullptr; // body of ShowParticipantDialog
    void  (*setupCallbacks)() = nullptr;                // SetupCallbacks scene-graph traversal

    // --- math leaf -------------------------------------------------------------
    // Math_RandomModulo(range) — the CRT (NOT cutscene) RNG used by the Birth
    // "geschrei" burst for the random delay seed. Default 0 -> deterministic.
    u32   (*mathRandomModulo)(u32 range) = nullptr;

    // --- lease / salon helpers -------------------------------------------------
    // SumCurrencyHeld(person) — the lessee's liquid funds (LeaseWindow / Resolve).
    int   (*personSumCurrency)(void* person) = nullptr;
    // RandFloat()-equivalent willingness draw [0,1); routed so the lease bodies
    // stay deterministic in tests (default uses the cutscene RNG directly).
};

void SetCutsceneMisc3Hooks(const CutsceneMisc3Hooks* hooks);
const CutsceneMisc3Hooks& GetCutsceneMisc3Hooks();

// ===========================================================================
// DETERMINISTIC KERNELS (the testable cores of the per-type mains).
// ===========================================================================

// gilde.exe 0x4a7fdc / 0x4a851c — the seasonal day-window pick shared by Death
// and Bankruptcy:
//   v1 = 6; for (i=0; i<6; ++i) { if (day < table[i]) { v1 = i; break; } ... }
//   (Death uses `<`; the loop breaks on the FIRST threshold strictly greater
//   than `day`, leaving v1 = that index; if none, v1 stays 6.)
// Returns the season-band index in [0, 6]. `day` is WORD2(qword_13CE852).
int CutsceneSeasonWindowIndex(int day);

// gilde.exe 0x4a7b5c (Birth) — the voice-line BASE index from the two parents'
// "ill" byte (+9): both ill -> 6; father(a) ill only -> 2; mother(b) ill only
// -> 4; neither -> 0. (a == record at slot.partIds[0], b == slot.partIds[1].)
// The actual line played is base + RandInt(2).
int CutsceneBirthVoiceBase(bool fatherIll, bool motherIll);

// gilde.exe 0x4a851c (Bankruptcy) — the closing message id: word_63C740 & 4
// (player is the bankrupt) -> 7343; otherwise 5822.
int CutsceneBankruptcyMessageId(unsigned int worldFlags);

// gilde.exe 0x4a6b90 (Execution) — the intro RunTimedScript duration: table
// element RandInt(3) of {14000,12000,12000,15000,10000}.
int CutsceneExecutionIntroDuration(int rollMod3);

// gilde.exe 0x4a65e4 (DuelChoiceWindow) — map a clicked button id to the slot's
// +148 choice byte AND whether the loop latches the quit flag:
//   1210 -> choice 1 (accept), quit; 1155 -> choice 0 (decline), quit;
//   else  -> no change (choice stays its prior value), no quit.
// Returns the resolved choice byte for a fresh window (prior value 0). `out_quit`
// receives whether dword_631614 is latched. Unknown button -> returns prior 0.
int CutsceneDuelChoiceFromButton(i32 buttonId, bool* outQuit);

// gilde.exe 0x4a673c (DuelOutcomeWindow) — map a clicked child-object id (one of
// three action buttons, indices 0/1/2 here) to the slot +148 outcome byte:
//   index 0 -> 2, index 1 -> 3, index 2 -> 4 (default initial value 4).
// `clickedIndex` in {0,1,2} or -1 for "none clicked" (keeps the default 4).
int CutsceneDuelOutcomeFromButton(int clickedIndex);

// gilde.exe 0x4a9a68 (LeaseAutoResolve) — the AI lessee accept/counter decision.
//   willingness = randFloat * (1/6) + 0.3                 (in [0.3, 0.4667))
//   ask = requestedRent  (a1[37] default == base rent v3[8])
//   if (funds * willingness >= 32000 * leaseYears) -> ACCEPT at the asking rate
//   else { counterBudget = funds * (randFloat2 * (1/6) + 0.3);
//          if (counterBudget <= ask) {
//            ratio = counterBudget / ask;
//            if (randFloat3 > ratio) {        // willing to counter
//              cap = min(ask, 2*counterBudget);
//              accept = 1;
//              step = leaseYears * 0.1;
//              counter = RandInt(cap - counterBudget) * step + counterBudget;
//              counter += counter % 32;       // round-up to the 32 grid
//            } } }
// We expose it as a pure function over the recovered inputs (randFloat draws
// supplied by the caller / cutscene RNG) returning the decision. `out_rent`
// receives the negotiated rent (the asking rate on accept, the counter on
// counter, unchanged on reject). Returns: 0 reject, 1 accept-at-ask, 2 counter.
struct LeaseDecision { int kind = 0; i32 rent = 0; };
LeaseDecision CutsceneLeaseAutoResolve(int funds, i32 askingRent, int leaseYears,
                                       double will1, double will2, double will3,
                                       u32 counterRoll);

// ===========================================================================
// FULL FLOW DRIVERS (1:1 control flow over the hook surface).
// ===========================================================================

// gilde.exe 0x4aabdc — VIBE_Cutscene_ShowParticipantDialog(slot@<eax>).
//   for i in [0, slot[+48]): if (rendered[i] || !textPresent[i]) skip;
//     else { Form_SelectWindow; Text_RenderRichString(line); rendered[i]=1; }
// We model the two parallel arrays via the hook (the host owns the 11AB0xx
// participant text table); the inert default is a no-op. Returns nothing.
void CutsceneShowParticipantDialog(const CutsceneSlot* slot);

// gilde.exe 0x4aa970 — VIBE_Cutscene_SetupCallbacks().
//   GameTime_GetSeasonFromDay(); SceneGraph_TraverseTree(... ,192) x2;
//   Weather_ApplySeasonalMeshes();
// All four leaves are routed through setupCallbacks (single inert no-op).
void CutsceneSetupCallbacks();

// gilde.exe 0x4aa1f4 — VIBE_Cutscene_FormatLodDebug(obj@<eax>).
//   sprintf(buf, "Name: %s  active_lod: %x  fl.type: %i  fl.oldtype: %i",
//           obj, obj[+460], obj[+530]>>24, obj[+531]>>24); return 1;
// A pure debug formatter; we reproduce the format into the caller's buffer and
// return 1. `name` is obj (a C string), the other fields are the raw reads.
char CutsceneFormatLodDebug(char* buf, int bufSize, const char* name,
                            unsigned int activeLod, int flType, int flOldType);

// gilde.exe 0x4a697c — VIBE_Cutscene_PlayTobyScene(...).
//   LoadScene("cutscene_toby.ed3"); RunParticipants x2; RunCombatScript;
//   Teardown; NullSub();
// The combat-script pump and participant runs are routed through the hooks; the
// body is the fixed call sequence. `slot` is the cutscene slot driven.
void CutscenePlayTobyScene(const CutsceneSlot* slot);

// gilde.exe 0x4a6b90 — VIBE_Cutscene_Execution(slot@<eax>).
//   Snapshot/restore RNG; pick intro duration; LoadScene("Hinrichtung");
//   run the timed-script chain; FadeIn; Teardown. Returns nothing.
void CutsceneExecution(CutsceneRng& rng, const CutsceneSlot* slot);

// gilde.exe 0x4a7fdc — VIBE_Cutscene_Death(slot@<eax>).
//   season pick; voice/music/sky; script load; timed-script chain; inheritance
//   branch (adult children -> family tree; player -> reload session; else
//   distribute). FadeIn; sky destroy; teardown.
void CutsceneDeath(const CutsceneSlot* slot);

// gilde.exe 0x4a7b5c — VIBE_Cutscene_Birth(slot@<eax>).
//   resolve both parents; if either missing return; voice base from ill bytes;
//   timed-script chain with the gendered birth line + the geschrei burst; name
//   window; command delta; fade/teardown.
void CutsceneBirth(CutsceneRng& rng, const CutsceneSlot* slot);

// gilde.exe 0x4a851c — VIBE_Cutscene_Bankruptcy(slot@<eax>).
//   season pick; voice/music/sky + rain; timed-script chain; closing message;
//   fade; sky+rain destroy; teardown; inheritance/reload branch.
void CutsceneBankruptcy(const CutsceneSlot* slot);

// gilde.exe 0x4a9c24 — VIBE_Cutscene_Salon(slot@<eax>).
//   voice/music; resolve the salon entity; if present SalonFadeTransition else
//   LoadScene("ob_SALON.ed3"); RunCombatScript; swap each participant pair via
//   Command_QueueRequestCoord27; if loaded then FadeIn + Teardown; music restore.
// `entityPresent` is whether GameObject_ResolveEntityById found the salon (the
// branch selector). Returns nothing.
void CutsceneSalon(const CutsceneSlot* slot, bool entityPresent);

} // namespace guild::sim
