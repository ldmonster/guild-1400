#pragma once
// gilde.exe — Combat live-battle ORCHESTRATION: the frame driver, the per-tick
// battle-state pass, the scenario state setup/teardown, and the deployment/result
// DATA flow (namespace guild::sim). MODULE: combat (prefix VIBE_Combat_*).
// Deferred by the earlier combat agents (combat.cpp / combat_battle.cpp DEFERRED
// lists); this file translates the orchestration RULES those left out:
//
//   * VIBE_Combat_RunBattleLoop      @0x492c28 — the live-battle frame driver. Per
//     frame: pump the master frame loop, optionally pause (freeze unit anims),
//     else tick the battle state; on a cadence (every 420 game-ticks) re-run the
//     role planner over the roster; each frame run the per-unit order tick over
//     the roster and the win/lose check; latch the result flag. The frame pump,
//     anim toggle, HUD banner and ambient track are forward-declared leaves.
//   * VIBE_Combat_TickBattleState    @0x4905bc — the per-frame battle pass: order
//     slots -> input pick/assign -> dispatch -> projectiles -> health bars ->
//     bomb passes; then the host-turn-end op80 emission. The ORDER of the gameplay
//     passes is the rule; each pass is a forward-declared leaf (projectile/bomb
//     cores live in projectile.{h,cpp}; order tick in combat_battle).
//   * VIBE_Combat_LoadScenarioAssets @0x489ba8 — the battle STATE SETUP: pick the
//     scenario by mode-flags/owner-type, resolve the two side-owner records from
//     the roster ids, spawn each rostered unit and tag its team, store the squad
//     descriptor. The mesh/voice-bank/heightmap/scene-graph/ware-placement is
//     asset loading (forward-declared / stubbed).
//   * VIBE_Combat_UnloadScenario     @0x48a7b4 — the teardown DATA flow: reconcile
//     each surviving unit's final HP back into its source building stock, then
//     destroy the live characters and release banks. The audio/cutscene/mesh
//     release is forward-declared.
//   * VIBE_Combat_BuildDeploymentScreen @0x48dab0 — the deployment DATA flow: the
//     AI-defender "will they stand and fight (or take a bribe)?" decision —
//     roster counts + the RandomModulo probability rolls -> the will-fight flag
//     and (on fight) the defender-strength value. The whole UI (text/forms/frame
//     loop) is deferred; the decision + flags are the rule.
//   * VIBE_Combat_RunResultScreen    @0x48e4e4 — the result DATA flow: determine
//     the winner (forced winner, else alive-unit count) and compute the mercenary
//     payout (sum of per-unit pay = 40 * (skill/42 + 1) * rate). The scoreboard /
//     text / dialogs / command emission are deferred.
//
// Determinism: the role cadence + outcome use combat_battle's RNGs; the
// deployment will-fight rolls use the CRT LCG (Math_RandomModulo) exactly as the
// original. Mutations route through the command sink (mock).
#include "guild/common/types.h"
#include "sim/combat.h"
#include "sim/combat_battle.h"
#include "sim/combat_types.h"

#include <functional>
#include <vector>

namespace guild::sim {

// ===========================================================================
// Battle runtime state (the live-battle globals the loop reads/writes).
// In the original these are scattered statics (dword_631208 squad ptr,
// dword_6311E8/E4 side owners, dword_6311F0 forced-winner, dword_6311F4 deploy
// abort, dword_631614 quit flag, dword_62EB44 game-tick clock, unk_B5FB54
// pause). We gather them into one struct so the loop is a pure function.
// ===========================================================================
struct BattleRuntime {
    BattleDescriptor* squad = nullptr;          // dword_631208 (descriptor)
    std::vector<CombatUnitAI*> attackers;       // side A live units
    std::vector<CombatUnitAI*> defenders;       // side B live units

    // Side-owner person ids (dword_6311E8 attacker / dword_6311E4 defender),
    // resolved from squad->attackerOwnerId / defenderOwnerId at setup.
    i32 attackerOwnerId = -1;
    i32 defenderOwnerId = -1;

    // Forced winner (dword_6311F0): 0 = decide by count; else attacker/defender.
    BattleWinner forcedWinner = BattleWinner::Undecided;

    bool   paused = false;       // unk_B5FB54 (pause-toggle)
    bool   quit   = false;       // dword_631614 (loop exit latch)
    bool   deployAbort = false;  // dword_6311F4 (deployment said "no fight")
    BattleWinner result = BattleWinner::Undecided;

    u32    gameTick = 0;         // dword_62EB44 (advanced by the frame pump)
    u32    lastRoleAssignTick = 0; // v4 base (role cadence anchor)
};

// The role-reassignment cadence (the original gates on
// `v4 + 420 < dword_62EB44`, having set v4 = dword_62EB44 - 350 at entry so the
// first reassignment fires after 70 ticks, then every 420).
constexpr u32 kRoleAssignInterval = 420;   // game-ticks between role planner runs
constexpr u32 kRoleAssignWarmup   = 350;    // initial v4 = now - 350 offset

// gilde.exe 0x4905bc — VIBE_Combat_TickBattleState (per-frame pass ORDER).
// Runs the gameplay passes in the original's order and returns whether the
// turn-end op80 should be emitted (the dword_62D22C == dword_631270 host gate).
// The individual passes (order slots, pick, assign, dispatch, projectile, health
// bars, bomb explosions, thrown bombs) are supplied as callbacks so the rules are
// testable; pass nullptr to skip a leaf. Sets `rt.quit` if dword_6315B0 / forced
// winner is set (the original's `dword_631614 = 1`).
struct BattlePasses {
    std::function<void()> updateOrderSlots;     // VIBE_Combat_UpdateOrderSlots
    std::function<void()> pickAndAssignTargets; // pick + assign + dispatch
    std::function<void()> updateProjectiles;    // VIBE_Combat_UpdateProjectiles
    std::function<void()> refreshHealthBars;    // VIBE_Combat_RefreshHealthBars
    std::function<void()> updateBombExplosions; // VIBE_Combat_UpdateBombExplosions
    std::function<void()> updateThrownBombs;    // VIBE_Combat_UpdateThrownBombs
};
void TickBattleState(BattleRuntime& rt, const BattlePasses& passes);

// gilde.exe 0x492c28 — VIBE_Combat_RunBattleLoop (frame driver ORCHESTRATION).
// Drives the battle one frame: returns whether the loop should continue (the
// `RunFrameLoop(...) && !dword_6311F4` guard, here modelled by a caller-supplied
// `frameAlive` predicate AND the outcome not yet decided). Per call it:
//   1. evaluate the frame pump (frameAlive); stop if dead or deploy-aborted.
//   2. if paused: skip the battle pass (the original shows the pause banner);
//      else: TickBattleState.
//   3. if the outcome is not yet forced/decided and not paused:
//        a. role cadence: if (lastRoleAssignTick + 420 < gameTick) re-run the
//           role planner over the roster (AssignUnitsToRoles per side) and reset
//           the anchor to `now`.
//        b. EvaluateBattleOutcome over the roster + per-unit order tick
//           (UpdateUnitOrders) — modelled by the supplied passes + the weights.
//   4. if a winner is forced/decided -> latch rt.quit.
// `passes` supplies the per-frame battle leaves; `roleWeights` the per-side role
// probability rows; `rng` the cutscene LCG (role reassignment). Returns false
// once the loop should terminate.
bool RunBattleLoopFrame(BattleRuntime& rt, bool frameAlive, const BattlePasses& passes,
                        const RoleWeights& roleWeights, CutsceneRng& rng);

// ===========================================================================
// Scenario state setup / teardown (DATA flow only — assets forward-declared).
// ===========================================================================

// Asset-load forward declaration (the bulk of LoadScenarioAssets: scene/mesh/
// voice-bank/heightmap/ware-placement). Wired by the host; the test stubs it.
// gilde.exe 0x489ba8 (the asset-loading remainder).
void LoadScenarioAssets_LoadAssets(const BattleDescriptor& battle);

// The combat scenario the mode-flags + owner type select. (LoadScenarioAssets'
// scene-name switch; recovered from the Kampfszenario_* string table.)
enum class CombatScenario {
    None,
    StadtAttack,     // (flags&1)&&(flags&8) STADT  / (flags&2)&&... STADT
    Bergpass,        // (flags&1) BERGPASS / (flags&4) BERGPASS
    Raeuberlager,    // production type 0x10 -> RAEUBERLAGER (warmup edge 15)
    Steinbruch,      // type 0x0C
    Waldstueck,      // type 0x0B
    Mine,            // type 0x0D
    Schmugglerloch,  // kind 4, tax-byte 1
    Unterschlupf,    // kind 4, tax-byte 2
    Diebesgilde,     // kind 4, tax-byte 3
    StadtwacheKlein, // kind 19, tax-byte 1
    StadtwacheMittel,// kind 19, tax-byte 2
    StadtwacheGross, // kind 19, tax-byte 3
};

// gilde.exe 0x489ba8 (the scenario-pick branch). Selects the scenario file by the
// mode flags first, then (for the no-flag economic-raid case) by the source
// person's kind/tax-byte. `personKind` is the source Person +0 kind byte (4/19/
// production); `taxByte` is +583; `isProductionType` mirrors Building_IsProduction.
// For the production case `productionTypeByte` is the AiPlayer type byte
// (dword_13CE294 + 589*type). Returns the scenario id.
struct ScenarioPick { CombatScenario scenario; int warmupEdge; };
ScenarioPick PickScenario(u8 modeFlags, bool isProductionType, u8 productionTypeByte,
                          u8 personKind, u8 taxByte);

// gilde.exe 0x489ba8 — VIBE_Combat_LoadScenarioAssets (the STATE-SETUP rule).
// Resolves the two side-owner records (attackerOwnerId from roster +52, defender
// from +56), spawns every rostered unit (attacker roster +128 -> teamA, defender
// roster +192 -> teamB), and stores the squad descriptor into `rt`. The spawn is
// performed against the supplied `field`; each spawned unit gets the side owner
// id as its team. Asset loading is delegated to `LoadScenarioAssets_LoadAssets`.
// `aiFactory(personId)` supplies the CombatUnitAI metadata for a freshly spawned
// unit (the host wires it to the object-def / building model; the test mocks it).
void LoadScenarioAssets(BattleRuntime& rt, BattleDescriptor& battle, CombatField& field,
                        const std::function<CombatUnitAI*(i32 personId, CombatUnit*,
                                                          i32 teamOwnerId, bool attacker)>& aiFactory);

// gilde.exe 0x48a7b4 — VIBE_Combat_UnloadScenario (the TEARDOWN data flow).
// For each unit still in the field, reconcile its final HP back into the source
// building stock as a delta (finalHp - originalHp) — unless the source person is
// kind 11 (a guard, excluded) — then mark the unit destroyed. The HP delta is
// reported through the command sink (the original calls
// Building_AdjustStockAndNotify; here it is the casualty/loot writeback). Returns
// the list of (personId, hpDelta) reconciliations (also routed through the sink).
struct StockReconcile { i32 personId; int hpDelta; };
std::vector<StockReconcile> UnloadScenario(BattleRuntime& rt, CombatField& field,
                                           const std::vector<int>& originalHp,
                                           const std::vector<u8>& sourceKind);

// ===========================================================================
// Deployment-screen DATA flow (the will-fight / bribe decision).
// ===========================================================================

// gilde.exe 0x48dab0 — VIBE_Combat_BuildDeploymentScreen (the DECISION rule, the
// AI-defender path: defender owner is NOT the human player (+2 != 6)).
// Counts the defender roster (+128) units; then:
//   if (no defenders || RandomModulo(5) > squad.guardCount(+272)) {
//       r = RandomModulo(4);  bucket = r==0?10 : r==1?90 : 50;
//       if (RandomModulo(90) + 10 <= bucket) -> WILL FIGHT:
//            deployAbort = true (battle skipped on the human side),
//            defenderStrength = ComputeDefenderStrength(...) * 0.01;
//       else -> NO FIGHT (defenderStrength = -1).
//   }
// `defenderUnitCount` = populated defender roster slots; `guardThreshold` =
// *(squad+272) (the patrol/guard strength gate); `baseStrength` is the value the
// original feeds ComputeDefenderStrength (we pass it pre-computed). The three
// RandomModulo draws are made in the original's order (5, 4, 90) so the CRT LCG
// stays in sync. Returns the decision.
struct DeploymentDecision {
    bool willFight = false;       // dword_6311F4 set -> defender stands
    int  defenderStrength = -1;   // dword_6311F8 (-1 == declined / no fight)
    int  defenderUnitCount = 0;   // populated defender roster slots
};
DeploymentDecision DecideAiDeployment(int defenderUnitCount, int guardThreshold,
                                      double baseStrength);

// The deployment will-fight probability buckets (10/50/90 percent) and the
// bribe-strength scale (dbl_61BA6C == 0.01).
constexpr int    kFightChanceLow  = 10;   // r==0
constexpr int    kFightChanceMid  = 50;   // r==2/3
constexpr int    kFightChanceHigh = 90;   // r==1
constexpr double kBribeStrengthScale = 0.01; // dbl_61BA6C

// ===========================================================================
// Result-screen DATA flow (winner + mercenary payout).
// ===========================================================================

// gilde.exe 0x48e4e4 — VIBE_Combat_RunResultScreen (the winner-determination
// rule). If a winner is forced (dword_6311F0) it picks the OTHER side as the
// loser-owner-vs-winner-owner (the original maps forced==attackerOwner ->
// winner = defender, else attacker — i.e. dword_6311F0 names the *eliminated*
// side). Otherwise it counts each side's still-effective units (alive byte set,
// != 4 fled, not captured) and the side with MORE effective units wins; on a tie
// (v15 <= v11) the DEFENDER wins. Returns the winner.
BattleWinner DetermineResultWinner(const BattleRuntime& rt);

// gilde.exe 0x48e4e4 (the flags&4 mercenary-payout block). For each surviving
// attacker unit, pay = MultiplyByRate(40 * (int)((skill * 1/42) + 1.0), rate);
// the per-unit pays are summed into the total bounty (EnqueueCmd15). `skills` is
// each unit's skill byte (CombatUnit+131 -> here the unit's selfSkill); `rate` is
// the currency rate multiplier (1.0 if unset). Returns the total payout.
int ComputeMercenaryPayout(const std::vector<u8>& skills, double rate);
constexpr float kMercPaySkillScale = 1.0f / 42.0f; // flt_61BACC (0.0238095)
constexpr int   kMercPayBase       = 40;            // 40 * (...)

} // namespace guild::sim
