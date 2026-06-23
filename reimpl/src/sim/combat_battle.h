#pragma once
// gilde.exe — Combat BATTLE state machine + AI role/order assignment
// (namespace guild::sim). MODULE: combat battle (prefix VIBE_Combat_*).
// Deferred by the earlier combat agent (see combat.cpp DEFERRED list); this file
// translates the battle-loop RULES that agent left out:
//   * VIBE_Combat_RunBattleSetup     @0x490014 — battle bootstrap + the abstract
//     "auto-resolve" path (strength comparison; the live-battle/UI glue stubbed).
//   * VIBE_Combat_RunBattleLoop      @0x492c28 — the per-frame battle driver
//     (role-assignment cadence + outcome check + order tick).
//   * VIBE_Combat_TickBattleState    @0x4905bc — per-frame projectile/bomb pass.
//   * VIBE_Combat_UpdateUnitOrders   @0x491688 — the per-unit order-tick state
//     machine (switch over OrderSlot::state 1..8): the attack range-gate +
//     hit-chance roll + damage prediction is the translated rule; command/path/
//     heightmap emission is routed through the mock sink.
//   * VIBE_Combat_GetSoundRangeScale @0x485dc0 — per-unit combat-strength score.
//   * VIBE_Combat_ScoreUnitForRole   @0x48be60 — role-fit score for a unit.
//   * VIBE_Combat_ComputeUnitBalanceWeights @0x48bba8 — strength-ratio bucketing.
//   * VIBE_Combat_AssignUnitsToRoles @0x48bfb0 — the role planner (RNG-driven).
//   * VIBE_Combat_BuildOrderForUnit  @0x48c24c — role -> order-action mapping.
//   * VIBE_Combat_EvaluateBattleOutcome @0x48cf6c — alive-count win/lose check.
//
// Determinism: two RNGs are used exactly as the originals — the CRT LCG
// (crt::RandNext; via Math_RandomModulo and RandomFloatScaled) for role rolls /
// hit chance, and the cutscene LCG (CutsceneRng) for damage rolls / reassignment.
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_types.h"

#include <array>
#include <vector>

namespace guild::sim {

// ===========================================================================
// Per-unit combat metadata (the fields the battle AI reads that are NOT in the
// 536-byte CombatUnit raw layout: the weapon attached to the unit, its current
// role, and the production/fitness rating). In the originals these come from
// VIBE_Combat_FindObjectDef (the attached weapon object) and
// VIBE_Building_EvalProductionRating / ComputeOutputRatio. We attach them here
// so the rules are testable without the object-table / building model.
// ===========================================================================
struct CombatUnitAI {
    CombatUnit* unit = nullptr;
    i16  weaponType = kWpnStab;   // ObjectDef::weaponType (340/342/.../374)
    u8   weaponClass = 0;         // ObjectDef::weaponClass (+88: 0/1/2)
    u8   weaponMinDmg = 0;        // ObjectDef body[3]
    u8   weaponMaxDmg = 0;        // ObjectDef body[4]
    u8   role = kRoleAttack;      // CombatUnit+452 (assigned role)
    float productionRating = 1.0f; // EvalProductionRating(unit, 3)
    float outputRatio = 1.0f;      // ComputeOutputRatio(unit), in [0,1] (== HP frac)
    // Active-target health (for weaponClass 1/2 ranged gate): <= 0 means the
    // current ranged target is dead (FindActiveTarget(unit)[8] <= 0).
    int  activeTargetHp = 1;
};

// ===========================================================================
// Unit combat strength
// ===========================================================================

// gilde.exe 0x485dc0 — VIBE_Combat_GetSoundRangeScale.
//   weight = weapon-type weight (switch): 340->0.5, 342->0.8, 344->1.0,
//            350->0.7, 352->0.7, 366/368->0.8, 374->0.9, else 0.3;
//   return EvalProductionRating(unit,3) * weight * ComputeOutputRatio(unit).
// (Used as each unit's contribution to the squad balance & role scoring.)
double GetSoundRangeScale(const CombatUnitAI& u);

// The weapon-type -> strength weight map alone (exposed for testing).
double WeaponWeight(i16 weaponType);

// ===========================================================================
// Role scoring & assignment
// ===========================================================================

// gilde.exe 0x48be60 — VIBE_Combat_ScoreUnitForRole(unit, role).
// Scores how well `u` fits candidate `role` (0..5):
//   role 0 (attack):  GetSoundRangeScale * (role==unset?1.0 : 0.5;  dead-ranged-
//                     target -> 0.2)
//   role 1 (conquer): mult / GetSoundRangeScale   (mult: role1?1.0 : role!=0?0.5 :
//                     0.4;  dead-ranged-target -> 1.0)
//   role 2 (moveConq):role==2 ? 1.0 : 0.5
//   role 3 (hold):    1.0
//   role 4 (tile):    role==2 ? 0.2 : 1/outputRatio
//   role 5 (escape):  role==2 ? 0.2 : 1/outputRatio
// Higher == better fit.
double ScoreUnitForRole(const CombatUnitAI& u, u8 role);

// A 6-element cumulative role-probability vector (the balance-table row the
// original indexes out of dword_B59C40[30*mode + 6*bucket]). The values are
// CUMULATIVE thresholds in [0,1] used by AssignUnitsToRoles' RNG pick.
using RoleWeights = std::array<float, 6>;

// gilde.exe 0x48bba8 — VIBE_Combat_ComputeUnitBalanceWeights.
// Computes the strength ratio of one side vs the other (attacker: enemy/own;
// defender: own/enemy... i.e. sideStrength/otherStrength per the original) and
// buckets it into 0..4 via thresholds {1.75,1.25,0.75,0.5}. Returns the (mode,
// bucket) index pair into the caller-supplied balance table. The mode is derived
// from the battle flags + which side. (The actual probability row is data-driven
// in the original; here the caller provides the table.)
struct BalanceIndex { int mode; int bucket; };
BalanceIndex ComputeBalanceIndex(const BattleDescriptor& battle, bool isAttacker,
                                 double sideStrength, double otherStrength,
                                 int defenderEnemyCount);

// The strength-ratio bucket thresholds (flt_61B8C4..D0).
constexpr float kBalanceHi   = 1.75f; // flt_61B8C4 -> bucket 4
constexpr float kBalanceMid  = 1.25f; // flt_61B8C8 -> bucket 3
constexpr float kBalanceLow  = 0.75f; // flt_61B8CC -> bucket 2
constexpr float kBalanceVLow = 0.50f; // flt_61B8D0 -> bucket 1/0
constexpr double kRoleReassignChance = 0.25; // dbl_61B8DC

// gilde.exe 0x48bfb0 — VIBE_Combat_AssignUnitsToRoles.
// For each of the side's units: roll RandomFloatScaled() (CRT LCG / 32767),
// pick a role index r where the cumulative `weights[r]` first >= roll; among all
// units pick the best ScoreUnitForRole(unit, r); if that unit is unassigned OR a
// CutsceneRng::RandFloat() > 0.25 reassignment roll passes, set its role to r and
// remove it from the pool. Mutates each CombatUnitAI::role.
//   `units` = the side's units (already filtered to live members).
//   `weights` = the role-probability row (cumulative). `rng` = cutscene LCG.
void AssignUnitsToRoles(std::vector<CombatUnitAI*>& units, const RoleWeights& weights,
                        CutsceneRng& rng);

// ===========================================================================
// Order building (role -> battle action) — emission via the command sink.
// ===========================================================================

// The battle action a unit's role resolves to (the BuildOrderForUnit switch).
// In the originals each maps to a Command_Build* packet; we surface the decision
// and route the emission through the sink.
enum class BattleAction {
    None,            // unit dead/captured -> no order
    AttackNearest,   // role 0, melee -> nearest enemy unit
    AttackThreatTile,// role 0, ranged(class 2) -> most-threatened tile
    ConquerWare,     // role 1 -> nearest ware object
    MoveToConquer,   // role 2 -> nearest conquer target
    Hold,            // role 3 -> simple/hold
    TileOrder,       // role 4 -> tile packet
    Escape,          // role 5 -> nearest escape tile
};

// gilde.exe 0x48c24c — VIBE_Combat_BuildOrderForUnit (decision core).
// Returns the action the unit's role resolves to (None if the unit is dead, has
// no actor, or is captured (+533==1)). `alive` mirrors *(unit+8); `captured`
// mirrors actor[533]==1; for role 0 it also needs the ranged gate state.
BattleAction BuildOrderForUnit(const CombatUnitAI& u, bool alive, bool captured);

// ===========================================================================
// Order-tick state machine (the attack rule core of UpdateUnitOrders)
// ===========================================================================

// The per-unit attack evaluation extracted from state 2 of UpdateUnitOrders.
// Computes (a) whether the unit is in range to fire, (b) the hit-chance roll
// outcome, and (c) the predicted-damage value written to the order slot.
struct AttackEval {
    bool inRange = false;     // v84 — within weapon range of the target
    bool fires   = false;     // the RandomModulo(255) hit-chance roll passed
    int  predictedDamage = 0; // *(v4+9) — the displayed/queued damage estimate
};

// gilde.exe 0x491688 (state 2, the LABEL_46 rule block).
//   inRange = (distance(self, target) <= weaponRange);   // *(def+12) range float
//   // hit chance: accuracy = (i16)def[2] * 0.01 * (i16)self.skill (* mod if set)
//   chance = (int)accuracy;
//   roll   = Math_RandomModulo(255);                      // CRT LCG
//   fires  = roll > 255 - chance;
//   if (fires) {
//      predicted = (int)((minDmg + RandInt(maxDmg-minDmg)) * worthScale * 0.01)
//   } else predicted = 0;
//  - With a unit target, the worth scale is target.worth; without, 900.0.
// `selfSkill` is the unit's skill byte (*(self+131)); `accuracyByte` is def[2];
// `skillMod` is dword_631200 (1.0 when dword_6311E4 unset); `distance` and
// `weaponRange` are world units. `rng` is the cutscene LCG (damage roll).
// `alwaysFires` mirrors the binary's weaponClass==2 short-circuit (def[88]==2):
// such weapons FIRE without rolling Math_RandomModulo(255) — the hit-chance roll
// is skipped entirely (so the CRT LCG is NOT advanced for that path). Defaults to
// false (the rolling melee/class-0/1 path).
AttackEval EvaluateAttack(double distance, double weaponRange, u8 accuracyByte,
                          u8 selfSkill, double skillMod, bool hasUnitTarget,
                          float targetWorth, u8 weaponMinDmg, u8 weaponMaxDmg,
                          CutsceneRng& rng, bool alwaysFires = false);

constexpr double kAccuracyScale   = 0.01;  // dbl_61BBB4
constexpr double kRangeApproach   = 0.1;   // dbl_61BBAC (close-approach gate frac)
constexpr double kNoTargetWorth   = 900.0; // dbl_61BBBC (worth when no unit target)

// One step of the order-tick state machine for a single OrderSlot. Mirrors the
// switch(slot.state) in UpdateUnitOrders, advancing the slot and (for attack)
// running EvaluateAttack and writing the predicted damage / firing flag. Command
// /path/heightmap emission is routed through the sink (or skipped). `self` is the
// acting unit; `target` (may be null) is the resolved target unit.
// Returns true if the slot did work this tick (the original's per-slot effect).
struct OrderTickContext {
    double distanceToTarget = 0.0; // pre-resolved distance(self, target)
    double weaponRange = 1e9;      // def[+12] range float
    u8     accuracyByte = 0;       // def[2]
    u8     selfSkill = 1;          // self+131
    double skillMod = 1.0;         // dword_631200 modifier (1.0 if unset)
    CutsceneRng* rng = nullptr;    // cutscene LCG for the attack damage roll
};
bool TickOrderSlot(OrderSlot& slot, CombatUnitAI& self, CombatUnitAI* target,
                   const OrderTickContext& ctx);

// ===========================================================================
// Battle outcome
// ===========================================================================

// The resolved winner side of a battle.
enum class BattleWinner { Undecided, Attacker, Defender };

// gilde.exe 0x48cf6c — VIBE_Combat_EvaluateBattleOutcome.
//   count alive attacker units (marker!=0,!=4 and not captured(+533==1)) -> a;
//   count alive defender units likewise -> d;
//   if (d <= 0) winner = attacker;  else if (a <= 0) winner = defender;
//   (and a flag-capture special-case: if all flag-objects belong to attacker ->
//    defender wins — modelled via `allFlagsAttacker`).
// `attackers`/`defenders` are the two sides' unit sets. `alive(u)` returns the
// marker byte (0 dead, 4 fled). Returns the winner (Undecided if both survive).
BattleWinner EvaluateBattleOutcome(const std::vector<CombatUnitAI*>& attackers,
                                   const std::vector<CombatUnitAI*>& defenders,
                                   bool allFlagsAttacker = false);

// ===========================================================================
// Abstract auto-resolve (RunBattleSetup, the dword_6315BC == set fast path)
// ===========================================================================

// gilde.exe 0x490014 — the auto-resolve branch of VIBE_Combat_RunBattleSetup.
//   attackerScore = sum(GetSoundRangeScale(u) for live attacker u) +
//                   Math_RandomModulo(30);
//   defenderScore = sum(GetSoundRangeScale(u) for live defender u) +
//                   Math_RandomModulo(30);
//   winner = (attackerScore > defenderScore) ? attacker : defender;
// (The original also rolls per-unit stock damage via RandomModulo + adjusts
//  building stock — that is economy side-effect, routed through the sink/omitted;
//  the WIN/LOSE decision is the rule.) Uses the CRT LCG for the two +30 rolls,
// in attacker-then-defender order (matching the original's evaluation order).
struct AutoResolveResult {
    int attackerScore = 0;
    int defenderScore = 0;
    BattleWinner winner = BattleWinner::Undecided;
};
AutoResolveResult AutoResolveBattle(const std::vector<CombatUnitAI*>& attackers,
                                    const std::vector<CombatUnitAI*>& defenders);

} // namespace guild::sim
