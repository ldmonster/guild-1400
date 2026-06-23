#pragma once
// ===========================================================================
// AiMethod scoring / selection kernels — pure decision reconstruction.
//
// gilde.exe addresses (all __usercall):
//   0x4671c8 VIBE_AiMethod_EvalAttackTarget    (a1 = person record, a2 = ai-player)
//   0x4680e0 VIBE_AiMethod_ScanCandidatePersons ()
//   0x469248 VIBE_AiMethod_SelectBestForPerson  (a1 = person index)
//   0x469a18 VIBE_AiMethod_ExecuteSelected      (a1 = person index, a2 = ...)
//   0x46ac24 VIBE_AiMethod_EvalMoveToBuilding   (relation-weighted move scorer)
//
// These pick the best of up to 61 candidate "AI methods" for a person, score
// attack/move targets, and assemble the chosen method's command packet. The bulk
// of each is engine plumbing over the global person table word_12CE910[] (stride
// 268 words) and the method table byte_B57210[] (stride 148). What is pure and
// faithfully reconstructed here:
//   * EvalAttackTarget: the favorability acceptance gate
//       roll = RandomModulo(30); accept iff (float)roll + 40.0 > favorability.
//     (flt_61A1F8 = 40.0). Plus the top-level reject conditions.
//   * ScanCandidatePersons: the wealth score scale + floor
//       score = trunc(totalWealth * 0.015); floor at 800 (if score<=800 → 800).
//     (flt_61A2A4 = 0.015.) Plus the candidate-eligibility predicate.
//   * SelectBestForPerson: the dual-score best-keeping selection over candidates
//       (track best-by-score-A and best-by-score-B, choose A's if |A|!=0 && A>=B).
//   * ExecuteSelected: the per-entry weight emit transform
//       emitWeight = -rawWeight * 0.5  (flt_61A3AC = 0.5), gated on slotByte>=0.
//   * EvalMoveToBuilding: the per-relation-entry score accumulation
//       contrib = entryWeight * personFloat36 * (sameFaction ? 0.02 : 0.01)
//     (flt_61A3C4 = 0.02 same-faction, flt_61A3C0 = 0.01 other), summed.
//     Plus the "pick the most-disliked of 3 relations < 100" min-search.
//
// COUPLED LEAVES (inert hooks — engine reads passed as plain inputs):
//   VIBE_Ai_ComputePersonFavorability, VIBE_Person_ComputeTotalWealth,
//   VIBE_He_FindFirstHandlerByFilter, the command-queue emit, the global tables.
// ===========================================================================
#include <array>
#include <cstdint>
#include <vector>

#include "guild/common/types.h"

namespace guild::sim {

constexpr float kAttackFavBias    = 40.0f;        // flt_61A1F8
constexpr float kWealthScoreScale = 0.015f;       // flt_61A2A4
constexpr int   kWealthScoreFloor = 800;          // ScanCandidatePersons floor
constexpr float kMethodEmitScale  = 0.5f;         // flt_61A3AC (negated)
constexpr float kMoveSameFaction  = 0.02f;        // flt_61A3C4
constexpr float kMoveOtherFaction = 0.01f;        // flt_61A3C0
constexpr float kMoveRelationCap  = 100.0f;       // v42 = 100.0 initial "best"

inline int CoordTrunc2(double v) { return static_cast<int>(v); }

// ---------------------------------------------------------------------------
// 0x4671c8 — attack-target favorability acceptance gate.
//   roll = RandomModulo(30) ; accept iff (float)roll + 40.0 > favorability.
// `favorability` = VIBE_Ai_ComputePersonFavorability(...) (engine read).
// Returns true if the candidate is accepted.
// ---------------------------------------------------------------------------
bool AttackFavorabilityAccept(int roll0to29, float favorability);

// ---------------------------------------------------------------------------
// 0x4680e0 — wealth score with floor.
//   score = trunc(totalWealth * 0.015) ; if (score <= 800) score = 800.
// `totalWealth` = VIBE_Person_ComputeTotalWealth(...) (engine read).
// ---------------------------------------------------------------------------
int WealthScoreFloored(int totalWealth);

// ---------------------------------------------------------------------------
// 0x469a18 — the per-entry method weight emit transform.
//   emit = -rawWeight * 0.5   (only when the entry's slot byte >= 0).
// Returns the value the original appends via AppendAiMethodEntry. `slotByte` < 0
// means the entry is skipped (returns false, *out untouched).
// ---------------------------------------------------------------------------
bool MethodEmitWeight(int slotByte, float rawWeight, float* out);

// ---------------------------------------------------------------------------
// 0x46ac24 — per-relation-entry move-score contribution.
//   contrib = entryWeight * personFloat36 * (sameFaction ? 0.02 : 0.01)
// only counted when slotByte >= 0 && entryWeight > 0.0. Returns the contribution
// (0 when the entry is skipped). Mirrors both the +13 and +29 entry loops.
// ---------------------------------------------------------------------------
float MoveScoreContribution(int slotByte, float entryWeight, float personFloat36,
                            bool sameFaction);

// The "pick most-disliked relation" min-search (0x46ac24 head):
//   v42 = 100.0 ; v27 = -1 ; for i in 0..2: if (fav[i] < v42) { v27=i; v42=fav[i]; }
// Returns the index of the minimum (or -1 if none below 100.0). `fav` holds the
// three favorability reads (default 100.0 when the relation slot is empty).
int PickMostDislikedRelation(const std::array<float, 3>& fav);

// ---------------------------------------------------------------------------
// 0x469248 — dual-score best-method selection.
//
// The original keeps two running bests over the 61-entry method table: best-by
// "score A" (v24) and best-by "score B" (v23), each initialised to -1e30 and
// updated when a candidate's scoreA >= bestA (resp. scoreB >= bestB). At the end:
//   if ((bits(bestA) & 0x7FFFFFFF) != 0 && bestA >= bestB) → choose the A winner
//   else                                                   → choose the B winner.
// (`(bits & 0x7FFFFFFF) != 0` is "bestA != 0.0 and != -0.0", i.e. a candidate set
//  scoreA at all.)
//
// We model a candidate as (scoreA, scoreB) plus an opaque id; the selector returns
// the chosen candidate's id and which score won. A candidate updates a best with
// `>=` so the LAST candidate at the max wins (matches the forward scan + >=).
// ---------------------------------------------------------------------------
struct MethodCandidate {
    int   id     = 0;
    float scoreA = 0.0f;   // v18
    float scoreB = 0.0f;   // v19
};
struct MethodChoice {
    int  id      = -1;     // chosen candidate id (-1 if none)
    bool choseA  = false;  // true → A winner, false → B winner
    float scoreA = 0.0f;
    float scoreB = 0.0f;
};
MethodChoice SelectBestAiMethod(const std::vector<MethodCandidate>& candidates);

} // namespace guild::sim
