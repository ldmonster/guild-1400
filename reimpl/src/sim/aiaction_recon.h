#pragma once
// gilde.exe — AI action-selection / needs-evaluation / target-picking cluster.
//
// This file reconstructs the *portable, pure* arithmetic cores of the
// VIBE_AiAction_* / VIBE_AiNeeds_* / VIBE_AiTarget_* / VIBE_AiPlayer_* family
// (addresses 0x475700..0x47d4cc). The original bodies are deeply coupled to the
// live entity / person-record / office / handler-entry (He) / command-queue
// clusters: they read game records through raw pointer-offset accesses, emit
// command packets, and walk global world arrays. Those coupled leaves are NOT
// reproduced here (they belong to the entity/world layer and are LISTed as
// deferred in the module report). What *is* reproduced, byte-for-byte against
// the Hex-Rays decompile, is the math that decides scores, thresholds,
// random gates, cost computations, and the final action-split selection — the
// genuinely 1:1 numeric heart of the AI.
//
// Functions whose numeric cores are reconstructed here (provenance per fn):
//   VIBE_AiAction_EvalGuildhallTarget     0x475700  — relation-weight gate + scale
//   VIBE_AiAction_PlanGuildhallUpgrade    0x4757e8  — upgrade-cost tier + cost math
//   VIBE_AiAction_EvalNeedFulfillTarget   0x4761a0  — need-ratio scoring + pick
//   VIBE_AiAction_PlanPersonInteraction   0x475f48  — interaction-cost gate
//   VIBE_AiAction_EvalConversationTarget  0x47b468  — favorability conversation gate
//   VIBE_AiAction_EvalSleepSpot           0x47b7d4  — law-record time-window check
//   VIBE_AiAction_DispatchTargetSearch    0x47c430  — primary/secondary split RNG
//   VIBE_AiNeeds_EvaluateActions          0x47852c  — final action-split selection
//   VIBE_AiTarget_FindNearestPerson       0x479dd8  — randomized distance metric
//   VIBE_AiPlayer_TryRangedAttack         0x47d364  — ranged-accept probability
//
// All functions below are register-pure (no globals, no I/O). The RNG-dependent
// ones take the random draws as explicit parameters so they are deterministic
// and golden-vector testable; the original draws them inline from
// VIBE_Math_RandomModulo / VIBE_Math_RandomFloatScaled (documented per fn).
//
// Coupled leaves intentionally omitted (rule 8 — no fake stand-ins):
//   VIBE_AiNeeds_BuildScoreTable 0x4764e8 (7961 bytes) — a giant INI-driven
//     registrar (hundreds of VIBE_AiMethod_RegisterFromIni calls); pure
//     table-construction with no extractable closed-form math. Deferred.
//   The *_Find*/*_Handle*/*_Plan* command emitters (Command_Enqueue*,
//     QueueRequest*, GameObject_ResolveEntityById) — world mutation, deferred.

#include "guild/common/types.h"

namespace guild::sim {

using namespace guild;

// ---------------------------------------------------------------------------
// Shared numeric constants recovered via get_bytes (.rdata float pool).
// ---------------------------------------------------------------------------
constexpr float kGuildhallOwnRelationScale = 3.0f;    // flt_61A690 (0x40400000)
constexpr float kUpgradeCostBuildScale     = 0.4f;    // flt_61A694 (0x3ecccccd)
constexpr float kUpgradeCostCashScaleA     = 0.5f;    // flt_61A698 (0x3f000000)
constexpr float kUpgradeCostCashScaleB     = 0.25f;   // flt_61A69C (0x3e800000)
constexpr float kGuildhallTriggerWorthScale= 0.3f;    // flt_61A6B4 (0x3e99999a)
constexpr float kInteractionWealthScale    = 0.015f;  // flt_61A6B8 (0x3c75c28f)
constexpr float kConversationFavorGateA    = 33.0f;   // flt_61AC28 (0x42040000)
constexpr float kConversationFavorGateB    = 40.0f;   // flt_61AC2C (0x42200000)
constexpr float kNearestNoiseScale         = 0.5f;    // flt_61AB88 (0x3f000000)
constexpr float kNearestNoiseBase          = 0.75f;   // flt_61AB8C (0x3f400000)
constexpr double kRangedAttackAccuracyBase = 33.0;    // dbl_61AE48 (0x4040800...)

// ===========================================================================
// VIBE_AiAction_EvalGuildhallTarget @0x475700
// ===========================================================================
// The target's guild-relation byte (record+89, an i32 read then >>24, i.e. the
// top signed byte) is compared against a randomized loyalty threshold:
//     rand(0..15) + 50
// If relation >= threshold the "weighted" relation score path is taken and the
// returned scores are passed through unchanged; otherwise the "own" relation
// path is taken and BOTH output scores are multiplied by 3.0 (flt_61A690).
// Returns whether the high (loyal) branch was taken.
struct GuildhallGate {
    bool loyalBranch;       // true => weighted path (no extra scale)
    float ownScale;         // 1.0 on loyal branch, 3.0 otherwise
};
// relationTop = (record+89 dword) >> 24   (arithmetic, top signed byte)
// rand16 = VIBE_Math_RandomModulo(16)  in [0,15]
GuildhallGate EvalGuildhallGate(i32 relationTop, u16 rand16);

// ===========================================================================
// VIBE_AiAction_PlanGuildhallUpgrade @0x4757e8
// ===========================================================================
// The upgrade-level byte (record+92) maps to a per-tier upgrade weight:
//     level <  33 -> 50
//     level <  66 -> 30
//     level >= 66 -> 10
// (0x4758d4: cmp 33 / 0x4759ad: cmp 66). NOTE: the level byte is read SIGNED
// (mov ah,[esi+5Ch]) and both compares are signed (jge), so a byte with the high
// bit set (>=128) is negative -> takes the `<33` path -> 50. Param is i8 to match.
u8 GuildhallUpgradeTierWeight(i8 upgradeLevel);

// The loyalty gate that *enables* the upgrade-emit branch (negated form of the
// guildhall gate above): proceeds only when relationTop < rand(16)+50 AND the
// flag bit (record+90 & 4) is clear.
bool GuildhallUpgradeEnabled(i32 relationTop, u16 rand16, u8 flagByte90);

// The three cash-derived upgrade cost gates. The original computes, from a cash
// surplus (currency - 80000), a scaled cost and compares it against a budget.
// Each variant uses a different scale (paths at 0x47596a / 0x475c10 / —):
//   costFromBuildWorth(worth)  = (i32)((double)worth        * 0.4)
//   costFromCashScaleA(cash)   = (i32)((double)(cash-80000) * 0.5)
//   costFromCashScaleB(cash)   = (i32)((double)(cash-80000) * 0.25)
i32 UpgradeCostFromBuildWorth(i32 buildWorth);
i32 UpgradeCostCashScaleA(i32 currency);   // requires currency >= 80000
i32 UpgradeCostCashScaleB(i32 currency);   // requires currency >= 80000

// ===========================================================================
// VIBE_AiAction_EvalNeedFulfillTarget @0x4761a0
// ===========================================================================
// For each of three candidate need-slots the original forms a fulfilment ratio
//     ratio = (double)stat / max(1.0, (double)cap)
// where `cap` comes from a building-type record byte and `stat` from the same
// record + 128 (two contributions per slot are summed; see below). `cap` is
// floored at 1.0 (0x47624b/0x4762a0: `if (capf <= 1.0f) cap = 1.0`). The pair
// of contributions per output score is summed.
//
// NeedRatio reproduces one contribution: floor the cap at 1.0 then divide.
double NeedRatio(i16 stat, i16 cap);

// The min/max tie-break that picks one of three accumulated scores. When
// rand(2) is non-zero the original picks the *minimum* of {s0,s1,s2}; when zero
// it picks the *maximum* (0x4762e2). Encodes the exact index-resolution:
//   maxmode (rand2==0):  i = (s0 >= s1); if (s[i] >= s2) i = 2
//   minmode (rand2!=0):  i = (s0 <= s1); if (s[i] <= s2) i = 2
// Returns the chosen slot index in [0,2].
int NeedPickSlot(float s0, float s1, float s2, u16 rand2);

// The affordability gate: the chosen slot's "amount" dword is multiplied by 10
// and compared against the actor's currency (0x47632e: v18 = 10 * amount).
i32 NeedFulfillCost(i32 slotAmount);   // = 10 * slotAmount

// ===========================================================================
// VIBE_AiAction_PlanPersonInteraction @0x475f48
// ===========================================================================
// The interaction affordability gate. wealth is the target's total wealth; the
// original computes
//     cost = (i32)((double)wealth * 0.015)   (flt_61A6B8)
// then requires currency >= 6 * cost (0x476077).
i32 PersonInteractionUnitCost(i32 targetWealth);   // (i32)(wealth*0.015)
i32 PersonInteractionTotalCost(i32 unitCost);      // 6 * unitCost

// The family-need scan: starting from a random index in [0,12], advance
// (idx+1)%13 up to 13 times until a slot whose family-record byte (+idx+112) is
// zero is found, skipping idx==0. Returns the chosen need index, or -1 if none
// (the original's `goto LABEL_29` fall-through keeps the default v22).
// `occupied[i]` mirrors *((u8*)FamilyRecord + i + 112) for i in [0,12].
int PersonInteractionFamilyNeed(int startIdx, const u8 occupied[13]);

// ===========================================================================
// VIBE_AiAction_EvalConversationTarget @0x47b468
// ===========================================================================
// The two favorability gates used when scanning the 6 office co-workers.
// PrimaryGate (0x47b650): a candidate is accepted when its favorability toward
//   the actor is *below* 33.0 (flt_61AC28) and its slot-state byte == 1.
// SecondaryGate (0x47b757..0x47b7b8): used for the "higher office" pass; accept
//   when (rand(32) + 40.0) > favorability  (flt_61AC2C) and slot-state == 1.
bool ConversationPrimaryAccept(float favorability, u8 slotState);
bool ConversationSecondaryAccept(float favorability, u16 rand32, u8 slotState);

// ===========================================================================
// VIBE_AiAction_EvalSleepSpot @0x47b7d4
// ===========================================================================
// Given a law/curfew record's three i32 fields (a "current" value, a low bound
// and a high bound, at record +27/+7/+11) and a predicate result (the indirect
// call at +35), the spot is valid iff:
//     pred != 0  AND  current != value  AND  value >= low  AND  value <= high
// Reproduces 0x47b81f exactly (note: `value` is the v5 local at +0, and the
// `current` compared is the +27 field). Returns true => emit sleep action.
bool SleepSpotValid(bool predicate, i32 value, i32 fieldCurrent,
                    i32 lowBound, i32 highBound);

// ===========================================================================
// VIBE_AiAction_DispatchTargetSearch @0x47c430 (+ DispatchSecondarySearch)
// ===========================================================================
// After gathering `countA` candidates for filter-set A and `countB` for set B,
// the original decides which set to draw from. When both are non-empty it flips
// a coin (rand(2)); when only one is non-empty it uses that one. Returns:
//    0 => draw from set A, 1 => draw from set B, -1 => neither (no candidates).
// (0x47c501 both / 0x47c510 A-only / 0x47c603 B-only.)
int DispatchPickSet(int countA, int countB, u16 rand2);

// The within-set pick: a uniform index in [0, count) via rand(count).
int DispatchPickIndex(int count, u16 randN);

// ===========================================================================
// VIBE_AiNeeds_EvaluateActions @0x47852c  (final selection tail)
// ===========================================================================
// After building a candidate list of `n` entries, the original chooses how many
// "splits" to apply. With n <= 3 it emits a single split (count = 1). Otherwise
// it walks the descending threshold table dword_47848C = {-1,1,3,5} from index
// `tableLen` (=dword_4784BC, 11... see note) downward while n > table[idx],
// then draws rand(remaining) into the divisor table dword_478490 = {1,3,5,7}.
//
// We reproduce the *closed* numeric core: given n and two random draws, compute
// the resulting (groupStart, divisor) and the optional +/-1 jitter.
//
// Returns the selected divisor from {1,3,5,7}. tableThresholds is the recovered
// {-1,1,3,5}; divisors is {1,3,5,7}. `randSpan` selects within the surviving
// prefix; the surviving length is computed exactly as the original loop.
struct ActionSplit {
    int divisor;        // chosen entry from {1,3,5,7}
    int groupIndex;     // 3 * rand(n)  -> base offset into the candidate list
    int jitter;         // -1, 0, or +1 applied when n % divisor == 0
};
extern const i32 kSplitThresholds[4];   // {-1,1,3,5}  (dword_47848C)
extern const i32 kSplitDivisors[4];     // { 1,3,5,7}  (dword_478490)

// Computes how many divisor slots survive the threshold walk for candidate
// count `n` (the loop at 0x478c42..0x478c57). `tableLen` is the starting index
// (the original seeds it from dword_4784BC = 11 but the walk is bounded by the
// 4-entry table; we clamp to 4). Returns count in [1,4].
int ActionSplitSurvivors(int n, int tableLen);

// Full selection given the two random draws. randGroup in [0,n) selects the
// group; randDiv in [0,survivors) selects the divisor; randJitter in {0,1}
// chooses +1 vs -1 when applicable.
ActionSplit SelectActionSplit(int n, u16 randGroup, u16 randDiv, u16 randJitter);

// ===========================================================================
// VIBE_AiTarget_FindNearestPerson @0x479dd8 (distance metric)
// ===========================================================================
// The "nearest" pick is randomized: each candidate's true Euclidean distance is
// multiplied by a per-candidate noise factor
//     noise = RandomFloatScaled() * 0.5 + 0.75   (in [0.75, 1.25])
// and the minimum noisy distance wins (init 1e21). NoisyDistance reproduces the
// metric for one candidate (dx,dy,dz already differenced).
double NoisyDistance(float dx, float dy, float dz, float randFloatScaled);

// ===========================================================================
// VIBE_AiPlayer_TryRangedAttack @0x47d364 (accept core)
// ===========================================================================
// Pre-gate: the actor's action-depth field (+404) must be >= 3 (0x47d37f:
// `if (depth < 3) return 0`).
bool RangedAttackDepthOk(i32 actionDepth);   // depth >= 3

// The aim radius used for the first sphere query:
//     radius = (double)rand(16) + 33.0   (dbl_61AE48)
double RangedAttackAimRadius(u16 rand16);

// The hit-accept rule: a curve rating plus a random float must reach 1.0
// (0x47d49a: `if (curve + rand >= 1.0)`). `randFloatScaled` is
// VIBE_Math_RandomFloatScaled() in [0,1].
bool RangedAttackHitAccept(double curveRating, double randFloatScaled);

} // namespace guild::sim
