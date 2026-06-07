#pragma once
// gilde.exe — Combat battle/result DRIVERS (namespace guild::sim, MODULE combat,
// prefix VIBE_Combat_*). These are the high-level sequencing routines the earlier
// combat batches left as UI / scene / network glue: the result-screen
// winner-determination data flow, the auto-resolve battle bootstrap, the
// deployment-screen AI bribe/will-fight decision, the team order-issue row walk,
// the order-slot table refresh, the click-target selection scan, the unit
// formation-mode slot resolution, and the pursuit-target per-unit roll loop.
//
// SCOPE. Each original addresses static game globals and issues cross-module
// Form / Text / Command / Person / Object / GameObject / Audio calls. This module
// reproduces the SAME control flow + arithmetic with the side effects routed
// through CombatDriversHooks (inert defaults = no-op / deterministic 0) and the
// per-team / per-slot state passed in BY VALUE, so the logic is testable with no
// OS / render / network coupling. The pure deterministic cores are translated 1:1;
// the surrounding widget plumbing is the caller's (hook) responsibility.
//
//   * RunResultScreen        (0x48e4e4) — winner = the side with MORE alive units;
//                                          forced winner / rank classification.
//   * RunBattleSetup         (0x490014) — auto-resolve: validate rosters, sum the
//                                          per-side strength rolls, pick the winner.
//   * BuildDeploymentScreen  (0x48dab0) — the AI "accept bribe vs. fight" roll.
//   * IssueOrdersForTeam     (0x48c15c) — resolve a team's squad row index.
//   * UpdateOrderSlots       (0x4882c0) — per-slot "in use" + output-ratio refresh.
//   * AssignSelectedTarget   (0x488874) — scan units for the click-selected target.
//   * SetUnitFormationMode   (0x48980c) — first-empty-slot scan + per-mode opcode.
//   * UpdatePursuitTargets   (0x48c400) — stale-pursuit clear + attack/flee loop.
//
// x87 FLOAT-SPILL NOTE. The deployment / setup / order rolls compute scaled output
// ratios in the x87 FPU at 80-bit extended precision and spill each intermediate
// through a 32-bit `float` slot before the integer truncation. We mirror that: the
// products accumulate in `double` but every value the original stores back into a
// `float` local is truncated through `float` first, so host double-rounding cannot
// leak into the final `(int)` cast.
#include "guild/common/types.h"

#include <vector>

namespace guild::sim {

// ===========================================================================
// Recovered constants
// ===========================================================================
// BuildDeploymentScreen AI rolls (0x48e23a / 0x48e3a7 / 0x48e3d8). The "will the
// AI accept the bribe / decline to fight" decision draws RandomModulo(0x5A) (=90)
// and compares (roll + 10) <= threshold, where threshold is 10/50/90 chosen by a
// RandomModulo(4) outcome (0 -> 10, 1 -> 90, else 50) or by the clicked widget.
constexpr int kDeployRollMod    = 0x5A;  // RandomModulo(90)
constexpr int kDeployRollBias   = 10;    // (roll + 10) <= threshold
constexpr int kDeployModeMod    = 4;     // RandomModulo(4) selects the threshold
constexpr int kDeployThreshLow  = 10;
constexpr int kDeployThreshMid  = 50;
constexpr int kDeployThreshHigh = 90;
// The fight-vs-bribe gate also draws RandomModulo(5) compared to the player's
// "force" stat at unit+272 (0x48e3a7 guard `RandomModulo(5) > *(int*)(a1+272)`).
constexpr int kDeployForceMod   = 5;

// RunBattleSetup auto-resolve strength rolls (0x490232 / 0x49024a). Each side's
// running score gets two RandomModulo(0x1E) (=30) bonus draws added before the
// comparison. Per-unit the score accrues GetSoundRangeScale(unit) (a float) and a
// 20*RandomModulo(soundRange) stock adjustment.
constexpr int kSetupBonusMod    = 0x1E;  // RandomModulo(30) bonus per side

// SetUnitFormationMode opcodes written per mode (0x48988f / 0x489a51 / 0x489b16).
//   mode 1 (line)   -> opcode 342 for both resolved slots.
//   mode 2/3 (other)-> opcode 344 then 352.
constexpr int kFormOpLine   = 342;
constexpr int kFormOpA      = 344;
constexpr int kFormOpB      = 352;

// UpdateOrderSlots / UpdatePursuitTargets slot-table geometry. (Prefixed to avoid a
// clash with combat_types.h's kOrderSlotStride == 44, a different record stride.)
constexpr int kDriverOrderSlotStride = 8;   // dword_B5A0xx step (v2 += 8, bound 48)
constexpr int kDriverOrderSlotCount  = 6;   // 48 / 8
constexpr int kTeamSlotCount         = 16;  // per-team slot ids (64 bytes / 4)

// ===========================================================================
// Leaf hooks — the cross-module side effects these drivers make. Tests install a
// recording mock; the inert default makes every leaf a no-op / deterministic 0.
// ===========================================================================
struct CombatDriversHooks {
    // AI rolls. Default returns 0 (deterministic). The integration test wires this
    // to the REAL sibling guild::sim::Math_RandomModulo over crt::RandNext.
    int (*randomModulo)(u16 n) = nullptr;

    // GetSoundRangeScale(unit) -> the per-unit auto-resolve strength contribution
    // (0x485dc0). Default 0.0.
    double (*soundRangeScale)(const void* unit) = nullptr;

    // BuildOrderForUnit (0x48c24c) — push one unit's slot order. Default no-op.
    void (*buildOrderForUnit)(int teamRow, const void* unit, int slotIndex) = nullptr;
};

void SetCombatDriversHooks(const CombatDriversHooks* hooks);
const CombatDriversHooks& GetCombatDriversHooks();

// Convenience: roll via the installed hook (or 0 if none / n == 0). Mirrors the
// original `(u16)VIBE_Math_RandomModulo(n)` truncation to 16 bits.
u16 DriverRoll(u16 n);

// ===========================================================================
// RunResultScreen winner determination  (gilde.exe 0x48e4e4, head 0x48e5b3)
// ===========================================================================
// A combat unit "counts" toward its side's surviving total when:
//   * its slot id is occupied (id != -1), AND
//   * its alive byte (+8) is non-zero AND != 4 (4 == "escaped/withdrew"), AND
//   * its target-building class (+533 of the resolved building) != 1.
// `CountSurvivors` returns that count for one team's id array.
struct ResultUnit {
    i32 id     = -1;   // slot id (-1 == empty)
    u8  alive  = 0;    // +8 alive/state byte (0 dead, 4 escaped)
    u8  bldClass = 0;  // resolved building +533 (1 == "captured", excluded)
};
int CountSurvivors(const std::vector<ResultUnit>& team);

// gilde.exe 0x48e641 — the winner pick. `forcedWinner` (dword_6311F0) overrides:
//   if set, the winner is the OTHER guild than forcedWinner==attacker (the original
//   `v19 = (forced == attacker) ? defender : attacker`). Otherwise: v11 counts the
//   ATTACKER survivors (the +128 slots), v15 the DEFENDER survivors (+192). The
//   original test is `if (v15 <= v11) winner = defender; else winner = attacker;`
//   i.e. the DEFENDER wins unless the defender has STRICTLY MORE survivors than the
//   attacker. Returns +1 == attacker wins, -1 == defender wins.
enum class DriverBattleWinner { kAttacker = 1, kDefender = -1 };
DriverBattleWinner PickWinner(int attackerSurvivors, int defenderSurvivors);
DriverBattleWinner PickWinnerForced(bool forcedIsAttacker);

// gilde.exe 0x48e93b / 0x48e9df — the loser-rank classification used for the
// result-screen label index. The person's class byte (record+0, the 589-byte
// person record's first byte) maps:  19 -> 0, 16 -> 1, 4 -> 2, anything else keeps
// the previous index (the original never re-assigns for other values).
int ClassifyRankIndex(u8 personClass, int previous);

// ===========================================================================
// RunBattleSetup auto-resolve  (gilde.exe 0x490014, dword_6315BC branch)
// ===========================================================================
// Roster validation (0x49003c / 0x49005b): a slot id that is set (!= -1) but whose
// person record no longer resolves is cleared to -1 and flags an "invalid" state.
// Defender slots that DO resolve are counted (v4 — the auto-resolve only proceeds
// when at least one defender survives). Returns the live-defender count; `outDirty`
// is set true if any slot was cleared.
struct SetupSlot {
    i32  id = -1;       // slot id (-1 == empty)
    bool resolves = true; // VIBE_Person_FindRecordById(id) != null
};
int ValidateRoster(std::vector<SetupSlot>& defenders, bool* outDirty);

// gilde.exe 0x490108..0x49024e — the auto-resolve score accumulation + winner pick.
// For each side, every unit whose alive byte is non-zero AND != 4 contributes
// GetSoundRangeScale(unit) to that side's float-accumulated score (spilled through
// the x87 stack). Then each side gets a RandomModulo(30) bonus added, and the
// winner is the side with the GREATER total; a tie (atkBonus+atk <= defBonus+def?)
// resolves to the ATTACKER (the original `v31 <= Begin -> defender` — note `Begin`
// holds the attacker total here, so `defScore <= atkScore` picks the ATTACKER).
struct AutoResolveUnit {
    u8     alive = 0;       // +8 (0 dead / 4 escaped excluded)
    double strength = 0.0;  // GetSoundRangeScale(unit)
};
// Sum one side's base score (truncated through float per the original's v59/v58
// int spill: each add is `(int)(scale + (double)acc)`).
int SumSideScore(const std::vector<AutoResolveUnit>& side);

// Decide the winner given the two base scores and the two bonus rolls already drawn.
// The original (0x490232/0x49024e) forms attackerTotal = attackerScore +
// attackerBonus (the `Begin` value) and defenderTotal = defenderScore +
// defenderBonus (the `v31` value), then `if (defenderTotal <= attackerTotal) winner
// = defender(6311E4); else winner = attacker(6311E8)`. So the DEFENDER wins on a tie
// or attacker-advantage; only a strict defender advantage flips it to the attacker.
// (attackerScore accrues from the +128 slots, defenderScore from the +192 slots.)
DriverBattleWinner AutoResolveWinner(int attackerScore, int defenderScore,
                               int attackerBonus, int defenderBonus);

// ===========================================================================
// BuildDeploymentScreen AI decision  (gilde.exe 0x48dab0)
// ===========================================================================
// The defender-side AI "fight or accept the bribe" decision when no human is
// driving the deployment widget (0x48e371 branch):
//   * If the side has NO units OR RandomModulo(5) > playerForce -> the AI engages
//     its threshold roll; else it holds (no decision, fights normally).
//   * threshold = RandomModulo(4) ? (==1 ? 90 : 50) : 10.
//   * draw = RandomModulo(90) + 10.  draw <= threshold  -> ACCEPT (flees / defects),
//     else REFUSE (fights).
// We return the decision so the caller can run the strength resolve. All rolls go
// through DriverRoll (the installed hook), so a seeded test is fully deterministic.
enum class DeployDecision { kHold, kAccept, kRefuse };

// `hasUnits`  == the side has at least one live unit (the +128 slot scan found one).
// `playerForce` == *(int*)(a1+272), the player's force/aggression stat.
DeployDecision DeploymentAiDecision(bool hasUnits, int playerForce);

// gilde.exe 0x48e23a (human path) — when the human clicks one of the three offer
// widgets the threshold is fixed by which widget: low(10)/mid(50)/high(90). The
// accept test is identical: RandomModulo(90) + 10 <= threshold.
bool DeploymentOfferAccepted(int threshold);

// ===========================================================================
// IssueOrdersForTeam  (gilde.exe 0x48c15c)
// ===========================================================================
// Resolve the squad-row index for a team: scan the team table (dword_631208 + 52,
// stride 4, count = the +48 byte) for the entry whose value equals the team's id
// (team+4). Returns the matching row index, or -1 if none. The original then
// addresses `&dword_11AB000[215 * row]`; we return just the row.
int ResolveTeamRow(const std::vector<i32>& teamTable, int teamCount, i32 teamId);

// ===========================================================================
// UpdateOrderSlots per-slot refresh  (gilde.exe 0x4882c0)
// ===========================================================================
// For each of the 6 order slots, decide its "in use" flag and compute the on-screen
// output-ratio value. A slot with id == -1 is skipped. The +392 "active" byte is set
// from the production gate; the displayed value is
//   (int)(ComputeOutputRatio(unit) * dbl_61B264)   (spilled through float).
// We model the per-slot computation; the table-wide loop is the caller's.
struct OrderSlotInput {
    i32    slotId = -1;        // dword_B5A018[i] (-1 == empty)
    bool   recCaptured = false;// resolved record +533 == 1 (hide path)
    bool   active = false;     // +392 "in use" byte
    double outputRatio = 0.0;  // ComputeOutputRatio(unit)
};
struct OrderSlotResult {
    bool skip = false;       // empty / captured -> no value rendered
    int  count = 0;          // contributes +1 to dword_63120C when active
    int  outputValue = 0;    // the rendered (int)(ratio * scale)
};
// `ratioScale` == dbl_61B264. Returns the per-slot render decision.
OrderSlotResult ComputeOrderSlot(const OrderSlotInput& in, double ratioScale);

// ===========================================================================
// AssignSelectedTarget  (gilde.exe 0x488874)
// ===========================================================================
// Scan up to 32 unit pointers (dword_11BB6A0) for the first that is (a) non-null,
// (b) has its +392 "selectable" byte set, (c) belongs to the active team (its +364
// owner equals the active team's entity) OR the global override (dword_63C7B8) is
// set, and (d) has a non-null +388 and a set +8 alive byte. Returns the index of
// the first match, or -1.
struct SelectableUnit {
    bool present   = false;   // pointer != null
    bool selectable = false;  // +392
    bool ownerMatch = false;  // +364 == active team's entity
    bool hasField  = false;   // +388 != 0
    u8   alive     = 0;       // +8
};
int FindSelectedTarget(const std::vector<SelectableUnit>& units, bool globalOverride);

// ===========================================================================
// SetUnitFormationMode  (gilde.exe 0x48980c)
// ===========================================================================
// Resolve the first FREE formation slot: scan slots[0..15] (the +128 array, dword
// stride). Returns 0 if slot 0 is already free (id == -1); otherwise the index of
// the first slot equal to -1; or -1 if all 16 are occupied (the original's `v3 = -1`
// fall-through). This is the `for(i; *v4 != -1; ++i) if (i>=16) i=-1` scan.
int FindFirstFreeFormationSlot(const std::vector<i32>& slots);

// gilde.exe 0x489820 / 0x489b69 — the per-mode command opcode. mode 0 -> none
// (returns -1, the early `if (a2 == 1)` etc. all fail), mode 1 -> 342, mode 2 or 3
// -> the (344, 352) pair (we return the FIRST, 344; the caller emits both).
int FormationModeOpcode(u8 mode);

// ===========================================================================
// UpdatePursuitTargets  (gilde.exe 0x48c400)
// ===========================================================================
// The stale-pursuit clear predicate and the per-unit attack/flee roll already exist
// as pure helpers in combat_slots4 (PursuitRecordShouldClear / PursuitPressesAttack);
// this driver wraps the FULL per-team loop: for each commanded unit it (1) decides
// attack vs. flee using outputRatio and a RandomModulo(100) roll, returning the list
// of decisions so the caller can emit the orders. `scaledOutputRatio` per unit is
// (int)(ComputeOutputRatio(unit) * dbl_61B8EC) spilled through float.
struct PursuitUnit {
    i32    id = -1;            // team slot id (-1 == empty -> skipped)
    double outputRatio = 0.0; // ComputeOutputRatio(unit)
};
enum class PursuitAction { kSkip, kAttack, kFlee };
// `ratioScale` == dbl_61B8EC. Rolls RandomModulo(100) per non-empty unit via the
// installed hook. Returns one action per input unit (kSkip for empty slots).
std::vector<PursuitAction> DrivePursuitTargets(const std::vector<PursuitUnit>& units,
                                               double ratioScale);

} // namespace guild::sim
