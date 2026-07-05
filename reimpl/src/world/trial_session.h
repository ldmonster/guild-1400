#pragma once
// Court-trial CUTSCENE STATE MACHINE — the phase-sequencing shell of
// VIBE_Office_RunCourtTrial (gilde.exe 0x4a0eb8, ~12 KB, 2753 instructions).
//
// The original is one long straight-line cutscene driver: it builds a trial
// context (defendant / judge / two assessors / plaintiff + the charged law),
// loads the courtroom scene "Gericht.ed3", then plays a fixed sequence of
// cutscene scenes / voice banks (the PROZESS_* .sbf banks), calling the
// deterministic VERDICT / EVIDENCE-SCORING rules core (trial.{h,cpp}, the
// already-recovered substrate) at the appropriate phases.
//
// This header recovers the FSM STRUCTURE 1:1: the phase enum, the phase
// sequence and per-phase transitions, the per-phase rules-core call, and the
// cutscene/voice/GUI invocation points (forward-declared/stubbed leaves). The
// actual GUI form build (VIBE_Office_BuildElectionForm, _BuildTortureChoiceForm),
// cutscene playback (VIBE_Cutscene_LoadScene / _BuildSpeechPacket) and voice
// (the .sbf banks) are DEFERRED leaves — see TrialSessionLeaves below.
//
// Phase map (semantics binary-verified in the hardening pass):
//   kIntro          load "Gericht.ed3", build the cutscene slot   (0x4a0f81)
//   kAccusation     PROZESS_2_VORWURF_KOMMENTARE.sbf  -> evidence score core
//                   (the v318 float accumulation; v280 = v318 @0x4a15f6)
//   kPlea           the DEFENDANT's plea: his panel-row value == 1
//                   (dword_11AB094 load @0x4a1adb, `cmp ecx,1` @0x4a1b27) ->
//                   PROZESS_3_SCHULDIG (+ favorability fine-adjust, fmul
//                   flt_61CCF4 @0x4a1b3e) / PROZESS_3_NICHT_SCHULDIG (torture
//                   gate: torturer(ctx+72) resolves && evidenceCount > 2)
//   kTorture        torture-cost fine (wealth-score-B/3 x3, QueueRequest16
//                   triple) BEFORE the scene; PROZESS_4_FOLTER_1,
//                   PROZESS_5_FOLTER2_<instrument>, PROZESS_6_FOLTERERGEBNIS;
//                   running score * flt_61CCFC (confess) / flt_61CD00 (deny)
//   kVoteAnnounce   PROZESS_6_ANKUENDIGUNG, PROZESS_6_ABSTIMMUNG_ERGEBNISSE;
//                   jury tally v313 = judge + present assessors' rows;
//                   v313 >= 2 -> acquitted
//   kSentence       PROZESS_7_SCHULDIG + wealth-score-A/3 fine (v291/3 x3) +
//                   PROZESS_7_STRAFEN, OR PROZESS_7_NICHT_SCHULDIG (acquit);
//                   then PROZESS_8 close
//   kDone
//
// The original runs each phase to completion inline; we expose a tick-able FSM
// (Step) so the phase sequence + per-phase rules-core calls + emitted (mock)
// commands can be exercised and verified, mocking the cutscene/GUI leaves.
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"   // LawRecord, CrimeRecord
#include "world/trial.h"       // TrialScore, TrialVerdict, the rules core

namespace guild::world {

// ===========================================================================
// Recovered torture-instrument .esc scene table (gilde.exe @0x49D658, stride
// 16 bytes, 7 entries). Played at PROZESS_5_FOLTER2 by the torture phase; the
// chosen instrument indexes this table. Strings recovered via get_string.
// ===========================================================================
constexpr int kTrialTortureInstrumentCount = 7;
extern const char* const kTrialTortureEsc[kTrialTortureInstrumentCount];
// dschraube / stiefel / peitsche / brandeisen / eistropfer / kaefig / streckbank

// ===========================================================================
// Trial session-state record.
// ===========================================================================
// The original passes a context pointer (eax, the `var_98` arg) into the
// function; the fields it touches (recovered from the prologue disasm):
//   +0x34  (dword) a participant person id (read into var_1DC at 0x4a0fff)
//   +0x3c  (dword) the charged law id (passed to VIBE_Gesetz_GetRecord at entry)
// plus the locally-built cutscene slot (type byte 0x0A at var_1E0, ready-time
// 447A0000h == 1000.0f at var_1E4). We model the trial setup as a small struct
// holding the seats + charge + the live inputs the rules core consumes; the byte
// offsets that matter for the original's reads are commented.
struct TrialSetup {
    i32 chargedLawId = 0;     // ctx+0x3c — law passed to Gesetz_GetRecord
    i32 defendantObj = -1;    // ctx+0x34 — the delinquent / payer of fines
    i32 plaintiffObj = -1;    // Klaeger
    i32 judgeObj     = -1;    // Richter (juror 0)
    i32 assessorAObj = -1;    // Beisitzer A (juror 1)
    i32 assessorBObj = -1;    // Beisitzer B (juror 2)
    u8  currency     = 0;     // currency object id for the fine commit

    // Charge law-record max-wanted level field (Gesetz(0) threshold), clamped
    // 0..4 to select the score weight (the var_D4 clamp at 0x4a0f3e..).
    int maxWantedLevel = 0;

    // Evidence: the defendant's collected crimes (Beweis_CollectByOwner result).
    const CrimeRecord* evidence = nullptr;
    int                evidenceCount = 0;

    // Jury votes (the dword_11AB094 panel results): judge, assessorA, assessorB.
    // A vote >= 2 (the original's per-juror not-guilty value) sums toward acquit.
    const int* juryVotes = nullptr;
    int        juryCount  = 0;

    // Judge->defendant favorability (VIBE_Ai_ComputePersonFavorability), supplied
    // as a value; drives the guilty-fine adjustment (applied at kPlea on the
    // guilty-plea branch, fmul flt_61CCF4 @0x4a1b3e).
    double judgeFavorability = 0.0;

    // The defendant's plea: his panel-row value == 1 (dword_11AB094[215*row]
    // @0x4a1adb, `cmp ecx,1` @0x4a1b27) -> the PROZESS_3 SCHULDIG branch.
    bool defendantPleadsGuilty = false;

    // Torture gate + sub-decision. The original enters torture from the
    // not-guilty-plea branch only when the torturer person (ctx+72) resolves
    // AND the evidence count exceeds 2 (`if (v327 && v314 > 2) v279[0] = 1`).
    bool torturerPresent  = false;  // VIBE_Person_FindRecordById(ctx+72) != 0
    bool tortureConfessed = false;  // confession outcome (v298/v315 panel read)
    int  tortureInstrument = 0;     // (panel & 0xF) clamped 0..6 (the v330 clamp)

    // Wealth scores (VIBE_AiMethod_ComputeWealthScoreA/B — deferred AI leaves,
    // supplied as inputs). A drives the PROZESS_7 sentence fine (v291/3 per
    // seat); B drives the torture-cost fine committed when torture proceeds
    // (v292/3 per seat).
    i32 wealthScoreA = 0;   // v291 = ComputeWealthScoreA() (prologue)
    i32 wealthScoreB = 0;   // v292 = ComputeWealthScoreB() (torture entry)
};

// ===========================================================================
// Phases (the FSM states).
// ===========================================================================
enum class TrialPhase : int {
    kIntro       = 0,  // LoadScene Gericht.ed3 + alloc cutscene slot
    kAccusation  = 1,  // evidence presentation -> TrialComputeEvidenceScore
    kPlea        = 2,  // defendant plea (panel==1) -> guilty-adjust / torture gate
    kTorture     = 3,  // torture-cost fine + scenes -> TrialApplyTortureFine
    kVoteAnnounce= 4,  // announce + ABSTIMMUNG tally -> TrialTallyVerdict
    kSentence    = 5,  // convicted: wealth-score-A fine commit; else acquit
    kDone        = 6,
};

// ===========================================================================
// Deferred cutscene/GUI/voice leaves — forward-declared/stubbed.
// ===========================================================================
// Each leaf records one playback/build invocation point of the original. The
// default backend is a no-op; a test installs a recording backend to verify the
// invocation SEQUENCE (which scene/voice each phase plays, in order).
struct TrialSessionLeaves {
    // VIBE_Cutscene_LoadScene(scene, scale) — the .ed3 scene load (kIntro).
    void (*loadScene)(const char* scene, float scale, void* ctx) = nullptr;
    // The .sbf voice bank for a phase (e.g. "PROZESS_2_VORWURF_KOMMENTARE.sbf").
    void (*playVoice)(const char* sbf, void* ctx) = nullptr;
    // A torture-instrument .esc scene (kTrialTortureEsc[i]).
    void (*playTortureEsc)(const char* esc, void* ctx) = nullptr;
    // VIBE_Office_BuildElectionForm / _BuildTortureChoiceForm — GUI form builds.
    void (*buildForm)(const char* which, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// ===========================================================================
// The session driver.
// ===========================================================================
// Holds the FSM cursor + the computed rules-core outputs as the trial proceeds.
struct TrialSession {
    TrialSetup    setup;
    TrialPhase    phase = TrialPhase::kIntro;

    // Rules-core outputs, filled as phases advance:
    TrialScore    score;                    // kAccusation
    TrialVerdict  verdict = TrialVerdict::kConvicted;  // kVoteAnnounce tally
    int           voteTotal = 0;            // jury vote sum (v313)
    float         runningScore = 0.0f;      // v280: score after plea/torture adj
    i32           tortureFine = 0;          // kTorture (wealth-score-B / 3)
    i32           perSeatFine = 0;          // kSentence (wealth-score-A / 3)
    bool          finished = false;

    // Gesetz_GetRecord dependency for the evidence-score phase (set by Init).
    TrialLawLookup lawLookup = nullptr;
    void*          lawCtx    = nullptr;
};

// Initialize a session from a setup. Resets the FSM to kIntro.
void TrialSessionInit(TrialSession& s, const TrialSetup& setup,
                      TrialLawLookup lawLookup, void* lawCtx);

// Advance the FSM by exactly one phase (the original runs them inline; this
// exposes the same transitions tick-by-tick). Returns the phase just executed.
// Calls the appropriate rules core for the phase and fires the (mock) leaves.
TrialPhase TrialSessionStep(TrialSession& s, const TrialSessionLeaves& leaves);

// Run the whole session to kDone (loops Step). Returns the final verdict.
TrialVerdict TrialSessionRun(TrialSession& s, const TrialSessionLeaves& leaves);

// Install the law lookup used by the evidence-score phase (the Gesetz_GetRecord
// dependency). Stored on the session at Init.

} // namespace guild::world
