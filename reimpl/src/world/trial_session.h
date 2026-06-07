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
// Phase map (recovered from the in-function string-ref ordering, see report):
//   kIntro          load "Gericht.ed3", build the cutscene slot   (0x4a0f81)
//   kAccusation     PROZESS_2_VORWURF_KOMMENTARE.sbf  -> evidence score core
//                                                       (0x4a1853)
//   kJuryVerdict    tally juror votes -> PROZESS_3_SCHULDIG / _NICHT_SCHULDIG
//                                                       (0x4a1d3b / 0x4a1de4)
//   kTorture        (convicted-but-uncertain path) PROZESS_4_FOLTER_1,
//                   PROZESS_5_FOLTER2_<instrument>, PROZESS_6_FOLTERERGEBNIS
//                                       (0x4a21ff / 0x4a2392 / 0x4a2e2b)
//   kVoteAnnounce   PROZESS_6_ANKUENDIGUNG, PROZESS_6_ABSTIMMUNG_ERGEBNISSE
//                                       (0x4a2ec4 / 0x4a3346)
//   kSentence       PROZESS_7_SCHULDIG + PROZESS_7_STRAFEN (fine commit) OR
//                   PROZESS_7_NICHT_SCHULDIG (acquit), then PROZESS_8 close
//                                       (0x4a34cd / 0x4a34de / 0x4a3c82 / 0x4a34bc)
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
    // as a value; drives the guilty-fine adjustment.
    double judgeFavorability = 0.0;

    // Torture sub-decision (only consulted on the torture path): whether the
    // defendant confessed under torture, and which instrument was used.
    bool torture          = false;  // does this trial enter the torture phase?
    bool tortureConfessed = false;  // confession outcome (BuildTortureChoiceForm)
    int  tortureInstrument = 0;     // index into kTrialTortureEsc (0..6)
};

// ===========================================================================
// Phases (the FSM states).
// ===========================================================================
enum class TrialPhase : int {
    kIntro       = 0,  // LoadScene Gericht.ed3 + alloc cutscene slot
    kAccusation  = 1,  // evidence presentation -> TrialComputeEvidenceScore
    kJuryVerdict = 2,  // TrialTallyVerdict -> guilty/acquit branch
    kTorture     = 3,  // torture scenes -> TrialApplyTortureFine
    kVoteAnnounce= 4,  // announce + ABSTIMMUNG result voice
    kSentence    = 5,  // TrialApplyGuiltyFine + TrialCommitFine, or acquit
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
    TrialVerdict  verdict = TrialVerdict::kConvicted;  // kJuryVerdict
    int           voteTotal = 0;            // jury vote sum
    float         runningScore = 0.0f;      // score after torture/guilty fine
    i32           perSeatFine = 0;          // kSentence (TrialCommitFine result)
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
