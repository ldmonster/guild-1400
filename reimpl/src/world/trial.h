#pragma once
// Court trial — the VERDICT / EVIDENCE-SCORING rules core of the cutscene-driven
// VIBE_Office_RunCourtTrial (gilde.exe 0x4a0eb8, ~12 KB). The original is a giant
// cutscene state machine (loads Gericht.ed3, runs cutscenes\prozess\*.esc scenes,
// plays voice banks, drives the GUI). That cutscene/.esc/GUI shell is DEFERRED
// (see the module report). What is recovered here, byte-for-byte, is the
// deterministic decision substrate the trial is built on:
//
//   1. The evidence-score accumulation: dedup the defendant's collected crimes by
//      their law-type key, look each unique crime's law up via Gesetz_GetRecord,
//      and sum (penalty * wantedWeight).
//   2. The wanted-level -> weight table dword_49D644[5] (selected by the clamped
//      "max wanted level" law field).
//   3. The guilty-verdict fine adjustment (favorability-weighted), the torture
//      confession/penalty selection, and the JURY VOTE TALLY + verdict threshold.
//
// The original reads the live Person/Crime/Gesetz tables and the cutscene jury
// panel for the per-juror vote. Here the caller supplies the collected crimes,
// the per-juror votes, and the favorability inputs; the rules compute the same
// score / verdict / fine the original does. Mutations (fines, relation deltas)
// route through a settable command hook (mock), matching the amt.cpp pattern.
//
// Recovered constants (get_bytes):
//   dword_49D644[5] = {1.4, 1.2, 1.0, 0.8, 0.6}  wanted-level -> score weight
//   flt_61CCF4 = 0.2   flt_61CCF8 = 0.01          guilty-fine multipliers
//   flt_61CCFC = 1.1   flt_61CD00 = 0.8           torture confess/deny fine scale
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"   // LawRecord, CrimeRecord, kCrimeStride

namespace guild::world {

// ===========================================================================
// Recovered tuning constants (single-precision; see header note for raw bytes).
// ===========================================================================
// dword_49D644[5] @0x49D644 — score weight indexed by the clamped max-wanted
// level (Gesetz(0) threshold clamped to 0..4). Higher wanted level (lower index)
// => heavier evidence weight.
constexpr float kTrialWantedWeight[5] = {1.4f, 1.2f, 1.0f, 0.8f, 0.6f};
constexpr int   kTrialWantedWeightCount = 5;

// Guilty-verdict fine adjustment (the SCHULDIG branch):
//   fineBasis = score * flt_61CCF4
//   verdictScore = score - favorability * fineBasis * flt_61CCF8
constexpr float kTrialFineBasisScale = 0.2f;   // flt_61CCF4 (0x3E4CCCCD)
constexpr float kTrialFavorScale     = 0.01f;  // flt_61CCF8 (0x3C23D70A)
// Torture outcome fine scale (applied to the running score):
//   confessed  -> score * flt_61CCFC
//   maintained -> score * flt_61CD00
constexpr float kTrialTortureConfessScale = 1.1f; // flt_61CCFC (0x3F8CCCCD)
constexpr float kTrialTortureDenyScale    = 0.8f; // flt_61CD00 (0x3F4CCCCD)

// Jury verdict threshold: the trial tallies juror votes (judge + assessors).
// A juror vote >= 2 counts as "not guilty". The verdict is NICHT_SCHULDIG
// (acquitted) when the summed vote total reaches kTrialAcquitThreshold.
//   if (voteTotal >= 2) -> acquitted, else -> convicted.
constexpr int kTrialAcquitThreshold = 2;

// ===========================================================================
// Crime law-type accessor. In the trial the crime record's byte at offset 28
// (CrimeRecord::location) is used as the law-table key: Gesetz_GetRecord(crime[28]).
// The evidence dedup compares this same byte across crimes (v349[28]==v347[28]).
// ===========================================================================
inline u8 TrialCrimeLawType(const CrimeRecord& c) { return c.location; }

// ===========================================================================
// Evidence-score accumulation (the v365 loop of the original).
// ===========================================================================
// Given the defendant's collected crimes (as gathered by Beweis_CollectByOwner)
// and a Gesetz lookup, compute:
//   * uniqueCount: crimes after dedup by law-type byte (duplicates merged),
//   * score: sum over UNIQUE crimes of  penalty(law) * wantedWeight.
// `lawLookup` resolves a law-type byte to its LawRecord (penalty at +16); it
// returns false if no such law. `wantedWeight` is kTrialWantedWeight[clamp].
struct TrialScore {
    float score      = 0.0f;  // v365 / v327
    int   uniqueCount = 0;    // v361 collapsed to distinct law types (v364 path)
    int   totalCount  = 0;    // raw evidence count
};

using TrialLawLookup = bool (*)(u8 lawType, LawRecord* out, void* ctx);

// gilde.exe 0x4a0eb8 (evidence-score portion) — accumulate the trial score.
// `crimes`/`count` are the defendant's collected crime records (count == v361).
// `wantedLevel` is Gesetz(0)'s clamped threshold (0..4) selecting the weight.
TrialScore TrialComputeEvidenceScore(const CrimeRecord* crimes, int count,
                                     int wantedLevel,
                                     TrialLawLookup lawLookup, void* ctx);

// Selects the wanted-level weight: kTrialWantedWeight[clamp(wantedLevel,0,4)].
float TrialWantedWeight(int wantedLevel);

// ===========================================================================
// Verdict computation.
// ===========================================================================
enum class TrialVerdict : int {
    kAcquitted = 0,  // NICHT_SCHULDIG (vote total >= threshold)
    kConvicted = 1,  // SCHULDIG
};

// gilde.exe 0x4a0eb8 (jury-tally portion). Sums the juror votes (judge first,
// then each present assessor). Returns kAcquitted iff the total reaches
// kTrialAcquitThreshold (the v360 >= 2 test), else kConvicted.
//   `votes` is the per-juror vote values (the dword_11AB094 panel results);
//   only the first `juryCount` are summed.
TrialVerdict TrialTallyVerdict(const int* votes, int juryCount, int* outTotal);

// gilde.exe 0x4a0eb8 (SCHULDIG branch fine). When the jury convicts, the running
// evidence score is reduced by the favorability-weighted fine basis:
//   fineBasis    = score * kTrialFineBasisScale
//   verdictScore = score - favorability * fineBasis * kTrialFavorScale
// (favorability is VIBE_Ai_ComputePersonFavorability(judge, defendant); the AI
// scorer itself is sim-owned and supplied as a value here.)
float TrialApplyGuiltyFine(float score, double favorability);

// gilde.exe 0x4a0eb8 (torture branch fine). After torture, the score is scaled:
//   confessed -> score * kTrialTortureConfessScale
//   else      -> score * kTrialTortureDenyScale
float TrialApplyTortureFine(float score, bool confessed);

// ===========================================================================
// Mutation (command) hook — mock. The original commits fines via
// VIBE_Command_QueueRequest16(payerObj, recipientObj, amount, currency) (one per
// trial seat: Klaeger/judge/assessor, each amount = wealthScore / 3). We surface
// the same call as a settable hook so the rules are standalone-testable.
// ===========================================================================
using TrialFineHook = void (*)(i32 payerObj, i32 recipientObj, i32 amount,
                               u8 currency, void* ctx);
void TrialSetFineHook(TrialFineHook hook, void* ctx);

// gilde.exe 0x4a0eb8 (fine-commit portion). Splits a wealth score three ways
// (amount = wealthScore / 3) and enqueues the three transfers the original does
// (judge, assessor-A, assessor-B all paid by the defendant/recipient). Returns
// the per-seat amount. No-op commit when no hook is set.
i32 TrialCommitFine(i32 defendantObj, i32 judgeObj, i32 assessorAObj,
                    i32 assessorBObj, i32 wealthScore, u8 currency);

} // namespace guild::world
