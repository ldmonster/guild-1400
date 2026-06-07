#pragma once
// gilde.exe — Combat batch-2 LEAVES: threat/strength accumulation, weighted
// target selection, and the morale/retreat decision (namespace guild::sim).
// MODULE: combat (prefix VIBE_Combat_*).
//
// SCOPE. The deterministic combat MATH the battle-loop / order modules left
// untranslated, extracted as testable rules over caller-provided data:
//   * AccumulateThreatStats (0x57eb64) — squad strength/threat accumulation into
//     8 cash-tier buckets + a normalization pass (the "battle balance" report).
//   * FindNearestWareObject (0x48b4d4) — nearest gathered ware by 3D distance.
//   * FindNearestConquerTarget (0x48b5d8) — nearest gathered conquer target by a
//     RELATION-WEIGHTED 3D distance (enemy 10x / friend-owned 15x / self 25x /
//     unowned 1x), with a self-owned reject at the end.
//   * AutoIssueRetreatOrders morale gate (0x49080c) — the friend/foe alive-count
//     ratio + retreat-threshold predicate that decides "press the attack" vs.
//     "fall back" for a side.
//
// The originals address static globals (the 768-slot person/character table
// word_12CE910, the scene-graph gathered-object list dword_B59F10, the squad
// rosters dword_631208) and issue cross-module Person/Building/Command/SceneGraph
// calls. This module exposes the SAME arithmetic over caller-supplied views and
// a small hooks struct (with inert defaults), so the leaves stay testable with no
// OS / render / animation / network coupling.
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_slots.h"
#include "sim/combat_types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Recovered .rdata constants
// ===========================================================================
//   flt_625994 = 0.1     (cash -> "tier" scale; cash * 0.1)
//   flt_62599C = 7.0     (top cash tier / clamp ceiling)
//   flt_625998 = 100.0   (output-ratio percentage scale)
//   dbl_61B8B4 = 10.0    (conquer score: enemy-owned multiplier)
//   dbl_61B8A4 = 15.0    (conquer score: friend-owned-by-other multiplier)
//   dbl_61B8AC = 25.0    (conquer score: self-owned multiplier)
//   dbl_61BAEC = 0.5     (retreat: friend/foe ratio "press attack" threshold)
constexpr float  kThreatCashScale   = 0.1f;   // flt_625994
constexpr float  kThreatTierCeiling = 7.0f;   // flt_62599C
constexpr float  kOutputPercentage  = 100.0f; // flt_625998
constexpr double kConquerEnemyMul   = 10.0;   // dbl_61B8B4
constexpr double kConquerFriendMul  = 15.0;   // dbl_61B8A4
constexpr double kConquerSelfMul    = 25.0;   // dbl_61B8AC
constexpr double kRetreatRatioThreshold = 0.5; // dbl_61BAEC

// Number of cash tiers (0..7 inclusive -> 8 buckets); the final loop runs
// `result != 8`.
constexpr int kThreatTiers = 8;

// ===========================================================================
// Threat / strength accumulation  (gilde.exe 0x57eb64)
// ===========================================================================
// The original walks all 768 person/character slots (word_12CE910, stride 0x218),
// and for each ELIGIBLE slot:
//   * gate:  marker != -1  &&  active byte (+0x008) != 0  &&  busyRank (+0x002) < 10
//   * cash  = GetCashAmount(slot) * 0.1
//   * tier  = round( (cash <= 0.0) ? 0.0 : min(cash, 7.0) )   // -> 0..7
//   * ++totalActive;  ++tierCount[tier];
//   * if fillCount (+0x00A word) < requiredCap (+0x020 float): ++underfullCount
//   * curOutSum[tier] += ComputeCurrentOutput(slot)
//   * maxOutSum[tier] += ComputeMaxOutput(slot)
// then a normalization pass over the 8 tiers:
//   * outputPct[tier] = curOutSum[tier] / maxOutSum[tier] * 100.0
//   * avgMaxOut[tier] = maxOutSum[tier] / tierCount[tier]
//   * underfullPct    = underfullCount / totalActive * 100.0
//
// NOTE on rounding: the original rounds `cash` to an int tier via the x87 FPU in
// round-to-nearest-even mode (fld; <ConvertX>; fistp). RoundTier() reproduces
// round-half-to-even.
struct ThreatSlot {
    bool   eligible;       // marker != -1 && active && busyRank < 10 (pre-evaluated)
    double cash;           // VIBE_Person_GetCashAmount(slot) — raw, pre-scale
    int    fillCount;      // +0x00A word (current squad size)
    float  requiredCap;    // +0x020 float (underfull threshold)
    double currentOutput;  // VIBE_Building_ComputeCurrentOutput(slot)
    double maxOutput;      // VIBE_Building_ComputeMaxOutput(slot)
};

struct ThreatStats {
    int    totalActive = 0;                 // dword_1234938
    int    tierCount[kThreatTiers] = {0};   // dword_123493C[8]
    double curOutSum[kThreatTiers] = {0};   // flt_1234960[8]
    double maxOutSum[kThreatTiers] = {0};   // flt_1234980[8]
    double outputPct[kThreatTiers] = {0};   // flt_1234960[8] after normalize
    double avgMaxOut[kThreatTiers] = {0};   // flt_123497C[1..8] after normalize
    double underfullCount = 0.0;            // flt_123495C before normalize
    double underfullPct   = 0.0;            // flt_123495C after normalize
};

// gilde.exe 0x57eb64 — VIBE_Combat_AccumulateThreatStats.
// Accumulates the per-tier strength report from a slot list. (The original reads
// the global slot table + issues Person/Building calls per slot; here each slot's
// already-resolved values are supplied via ThreatSlot.)
ThreatStats AccumulateThreatStats(const std::vector<ThreatSlot>& slots);

// The cash -> tier rounding (round-half-to-even of clamp(cash, 0..7)).
int RoundTier(double cash);

// ===========================================================================
// Nearest gathered object  (gilde.exe 0x48b4d4 / 0x48b5d8)
// ===========================================================================
// Both originals first WALK the scene graph collecting candidate objects into
// dword_B59F10[dword_631268] (via the Ware/Conquer callbacks), then scan that
// gathered list. Here the caller supplies the gathered list directly.

// A gathered candidate object for the nearest-search scorers.
//   pos      — the object's mesh origin (obj+0x4C/0x50/0x54 for conquer; the
//              ware list uses obj+19/20/21 floats == the same +0x4C..+0x54).
//   ownerId  — the object's owner-unit team id source (obj+0x200 owner ptr's team
//              +0x16C). We collapse the two indirections into ownerTeam/hasOwner
//              + an opaque identity for the self / self-owned tests.
struct GatheredObject {
    float pos[3] = {0, 0, 0};   // object mesh origin
    bool  hasOwner = false;     // obj+0x200 (owner unit ptr) != 0
    bool  ownerIsSelf = false;  // owner unit ptr == searcher
    int   ownerTeam = 0;        // owner unit +0x16C team id (valid iff hasOwner)
    const void* identity = nullptr; // stable handle returned as the pick result
};

// gilde.exe 0x48b4d4 — VIBE_Combat_FindNearestWareObject.
// Returns the identity of the gathered object minimising the raw 3D distance from
// `searcherPos` to the object origin, or nullptr if the list is empty.
const void* FindNearestWareObject(const std::vector<GatheredObject>& gathered,
                                  const float searcherPos[3]);

// gilde.exe 0x48b5d8 — VIBE_Combat_FindNearestConquerTarget.
// Scores each gathered object by a relation-weighted 3D distance:
//   * owner present, DIFFERENT team than searcher: dist * 10.0   (enemy-owned)
//   * owner present, SAME team, owner != searcher: dist * 15.0   (ally-owned)
//   * owner present, owner == searcher:            dist * 25.0   (self-owned)
//   * no owner:                                    dist          (unowned, x1)
// Returns the identity of the min-score object, except: if that object is
// self-owned (ownerIsSelf) the original returns 0 — reproduced here as nullptr.
const void* FindNearestConquerTarget(const std::vector<GatheredObject>& gathered,
                                     const float searcherPos[3], int searcherTeam);

// ===========================================================================
// Retreat / morale decision  (gilde.exe 0x49080c)
// ===========================================================================
// The order driver counts, on each side, the units that are ALIVE and not already
// retreating (state byte +0x008 != 0 && != 4). For the side being evaluated it
// forms the ratio  foe / friend  (v30 = (double)foeCount / (double)friendCount)
// and decides:
//   * "press the attack"  iff  ratio > 0.5  ||  RandomModulo(100) <= 20
//   * otherwise            "fall back".
// (i.e. it presses the attack while it is not heavily outnumbering the enemy; a
//  very small foe/friend ratio is the only thing that triggers the fall-back, and
//  even then a 21% random roll overrides it.)
// (The actual per-unit order issuing — attack-move / escape-tile / op80 commands —
// is command/scene coupled and DEFERRED; this leaf is the decision predicate.)

// Count of units that are alive-and-fighting in one roster (state in {1,2,3,5,6,...},
// i.e. != 0 (dead/empty) and != 4 (already retreating)). `states` holds the per-slot
// state byte (+0x008 of the combat unit) for the -1-filtered roster entries.
int CountFightingUnits(const std::vector<u8>& states);

// gilde.exe 0x49080c — the retreat morale gate.
// `friendCount` / `foeCount` are CountFightingUnits for the evaluated side and the
// opposing side. `rng100` is the RandomModulo(100) roll the original makes when the
// ratio test fails (0..99). Returns true == "press the attack" (do NOT retreat):
//   (double)foeCount / (double)friendCount > 0.5  ||  rng100 <= 20.
bool ShouldPressAttack(int friendCount, int foeCount, int rng100);

} // namespace guild::sim
