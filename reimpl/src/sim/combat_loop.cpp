#include "sim/combat_loop.h"

#include "crt/rand.h"

#include <cmath>

namespace guild::sim {

// ===========================================================================
// Weak default for the asset-load leaf (the host overrides; the test stubs).
// ===========================================================================
void LoadScenarioAssets_LoadAssets(const BattleDescriptor& battle) {
    (void)battle;   // scene/mesh/voice-bank/heightmap loading — presentation.
}

// ===========================================================================
// TickBattleState — per-frame battle pass ORDER (0x4905bc).
// ===========================================================================
// gilde.exe 0x4905bc — the original sequence, presentation stripped:
//   Object_SetValueOrText(timer)             [HUD]            -> skipped
//   Camera_EdgeScroll()                       [input/cam]      -> skipped
//   Combat_UpdateOrderSlots()                 [gameplay pass]  -> callback
//   Combat_PickObjectUnderCursor()            [input]          (.)
//   Combat_AssignSelectedTarget()             [input]          (| pick+assign)
//   Command_DispatchSelectedUnits(a1)         [command]        (.)
//   Combat_UpdateProjectiles()                [gameplay pass]  -> callback
//   Combat_RefreshHealthBars(a1)              [HUD-ish pass]   -> callback
//   Combat_UpdateBombExplosions()             [gameplay pass]  -> callback
//   Combat_UpdateThrownBombs()                [gameplay pass]  -> callback
//   if (dword_6315B0 || dword_6311F0) dword_631614 = 1;        -> latch quit
//   else if (host turn-end) emit op80;                         -> return
void TickBattleState(BattleRuntime& rt, const BattlePasses& passes) {
    if (passes.updateOrderSlots)     passes.updateOrderSlots();
    if (passes.pickAndAssignTargets) passes.pickAndAssignTargets();
    if (passes.updateProjectiles)    passes.updateProjectiles();
    if (passes.refreshHealthBars)    passes.refreshHealthBars();
    if (passes.updateBombExplosions) passes.updateBombExplosions();
    if (passes.updateThrownBombs)    passes.updateThrownBombs();

    // A forced winner (dword_6311F0) or the global stop (dword_6315B0) latches
    // the loop-exit flag (dword_631614 = 1).
    if (rt.forcedWinner != BattleWinner::Undecided)
        rt.quit = true;
}

// ===========================================================================
// RunBattleLoopFrame — the live-battle frame driver (0x492c28).
// ===========================================================================
namespace {
// The per-side role-planner run: filter the side to live members, then
// AssignUnitsToRoles (the original walks the roster, finding each Person record
// and calling AssignUnitsToRoles when it is a combat-capable kind != 6/7).
void RunRolePlanner(std::vector<CombatUnitAI*>& side, const RoleWeights& weights,
                    CutsceneRng& rng) {
    std::vector<CombatUnitAI*> live;
    live.reserve(side.size());
    for (CombatUnitAI* u : side) {
        if (!u || !u->unit) continue;
        u8 a = u->unit->alive;
        if (a != 0 && a != 4)        // alive and not fled
            live.push_back(u);
    }
    if (!live.empty())
        AssignUnitsToRoles(live, weights, rng);
}
} // namespace

// gilde.exe 0x492c28 — one iteration of the battle loop's `while` body.
bool RunBattleLoopFrame(BattleRuntime& rt, bool frameAlive, const BattlePasses& passes,
                        const RoleWeights& roleWeights, CutsceneRng& rng) {
    // (1) The loop guard: `RunFrameLoop(...) && !dword_6311F4`. A dead frame pump
    //     or a deployment-abort terminates the loop.
    if (!frameAlive || rt.deployAbort)
        return false;

    // (2) Paused (unk_B5FB54): the original freezes every unit's animation and
    //     shows the pause banner instead of ticking the battle. Else tick.
    if (rt.paused) {
        // VIBE_Hud_SetStatusBannerText(pauseText) — presentation, skipped.
    } else {
        TickBattleState(rt, passes);
    }

    // (3) Outcome / order processing — only when no winner is forced/decided and
    //     not paused (the original gates on !dword_6311F0 && !dword_6311F4 &&
    //     !unk_B5FB54).
    if (rt.forcedWinner == BattleWinner::Undecided && !rt.deployAbort && !rt.paused) {
        // (3a) Role-assignment cadence: v4+420 < now. The anchor `v4` starts at
        //      now-350 (warmup), and is reset to `now` after each planner run.
        if (rt.lastRoleAssignTick + kRoleAssignInterval < rt.gameTick) {
            RunRolePlanner(rt.attackers, roleWeights, rng);
            RunRolePlanner(rt.defenders, roleWeights, rng);
            rt.lastRoleAssignTick = rt.gameTick;
        }

        // (3b) Outcome check (host only in the original; the rule is the count).
        BattleWinner w = EvaluateBattleOutcome(rt.attackers, rt.defenders);
        if (w != BattleWinner::Undecided)
            rt.result = w;
        // (the per-unit UpdateUnitOrders order tick over the roster runs here in
        //  the original; it is driven by the order-slot pass in TickBattleState
        //  and combat_battle::TickOrderSlot — not re-issued here.)
    }

    // (4) Latch the quit flag once a winner exists (dword_631614 = 1).
    if (rt.forcedWinner != BattleWinner::Undecided ||
        rt.result != BattleWinner::Undecided || rt.deployAbort) {
        rt.quit = true;
    }
    return !rt.quit;
}

// ===========================================================================
// Scenario selection (0x489ba8 — the scene-name switch).
// ===========================================================================
ScenarioPick PickScenario(u8 modeFlags, bool isProductionType, u8 productionTypeByte,
                          u8 personKind, u8 taxByte) {
    ScenarioPick p{CombatScenario::None, 10};   // v69 = 10 default warmup edge

    // Mode-flag branch first (the original's `if ((v3 & 1) != 0)` etc.).
    if (modeFlags & kBattleRaid) {
        // raid: STADT (also indoor flag 8) vs BERGPASS; the original picks STADT
        // for the &8 indoor variant, BERGPASS otherwise — but only when NOT the
        // economic-raid (no roster) case below. Here flags&1 == open raid.
        p.scenario = CombatScenario::StadtAttack;
        return p;
    }
    if (modeFlags & kBattleDefend) {
        p.scenario = CombatScenario::Bergpass;
        return p;
    }
    if (modeFlags & kBattleAttack) {
        p.scenario = CombatScenario::Bergpass;
        return p;
    }

    // No mode flag -> economic raid on a building/object: select by the source
    // person's AiPlayer type byte / kind / tax-byte.
    if (isProductionType) {
        switch (productionTypeByte) {
            case 0x10: p.scenario = CombatScenario::Raeuberlager; p.warmupEdge = 15; break;
            case 0x0C: p.scenario = CombatScenario::Steinbruch;   break;
            case 0x0B: p.scenario = CombatScenario::Waldstueck;   break;
            case 0x0D: p.scenario = CombatScenario::Mine;         break;
            default:                                              break;
        }
        return p;
    }
    if (personKind == 4) {
        switch (taxByte) {
            case 1: p.scenario = CombatScenario::Schmugglerloch; break;
            case 2: p.scenario = CombatScenario::Unterschlupf;   break;
            case 3: p.scenario = CombatScenario::Diebesgilde;    break;
            default:                                             break;
        }
    } else if (personKind == 19) {
        switch (taxByte) {
            case 1: p.scenario = CombatScenario::StadtwacheKlein;  break;
            case 2: p.scenario = CombatScenario::StadtwacheMittel; break;
            case 3: p.scenario = CombatScenario::StadtwacheGross;  break;
            default:                                               break;
        }
    }
    return p;
}

// ===========================================================================
// LoadScenarioAssets — the STATE-SETUP data flow (0x489ba8).
// ===========================================================================
void LoadScenarioAssets(BattleRuntime& rt, BattleDescriptor& battle, CombatField& field,
                        const std::function<CombatUnitAI*(i32, CombatUnit*, i32, bool)>& aiFactory) {
    // Store the squad descriptor (dword_631208 = a1) and resolve the side owners
    // (dword_6311E8 = FindRecordById(*(a1+52)); dword_6311E4 = FindRecordById(
    //  *(a1+56))). We carry the ids directly.
    rt.squad = &battle;
    rt.attackerOwnerId = battle.attackerOwnerId;   // -> dword_6311E8
    rt.defenderOwnerId = battle.defenderOwnerId;   // -> dword_6311E4

    rt.attackers.clear();
    rt.defenders.clear();

    // Asset loading (scene/mesh/voice/heightmap/ware-placement) — forward-decl'd.
    LoadScenarioAssets_LoadAssets(battle);

    // Attacker roster (+128): spawn each populated slot, tag team = attacker owner.
    // (The original spawns into word_B5A350 via Combat_SpawnUnit and sets
    //  *(unit+91) = dword_6311E8.) NOTE: the original's defender spawn writes the
    //  attacker roster (+192) units with team dword_6311E8 and the (+128) units
    //  with team dword_6311E4 — i.e. roster +192 is side A and +128 is side B.
    //  We follow the original's roster-to-team mapping exactly.
    for (int i = 0; i < kRosterCapacity; ++i) {
        i32 pid = battle.attackerRoster[i];   // roster at +192 in the original
        if (pid == -1) continue;
        CombatUnit* u = field.Spawn(pid, /*hp*/100, /*team*/rt.attackerOwnerId);
        if (!u) continue;
        u->teamId = rt.attackerOwnerId;       // *(unit+91) = dword_6311E8
        CombatUnitAI* ai = aiFactory ? aiFactory(pid, u, rt.attackerOwnerId, true) : nullptr;
        if (ai) rt.attackers.push_back(ai);
    }
    for (int i = 0; i < kRosterCapacity; ++i) {
        i32 pid = battle.defenderRoster[i];   // roster at +128 in the original
        if (pid == -1) continue;
        CombatUnit* u = field.Spawn(pid, /*hp*/100, /*team*/rt.defenderOwnerId);
        if (!u) continue;
        u->teamId = rt.defenderOwnerId;       // *(unit+91) = dword_6311E4
        CombatUnitAI* ai = aiFactory ? aiFactory(pid, u, rt.defenderOwnerId, false) : nullptr;
        if (ai) rt.defenders.push_back(ai);
    }
}

// ===========================================================================
// UnloadScenario — the TEARDOWN data flow (0x48a7b4).
// ===========================================================================
// gilde.exe 0x48a7b4 (the unit loop):
//   for each unit slot (stride 536, 32): if alive-marker set and char ptr set:
//     if (host) {
//        rec = FindRecordById(unit.id);
//        if (rec.kind /*+2*/ != 11)
//           Building_AdjustStockAndNotify(rec, finalHp - rec.hp, slot);
//     }
//     Character_Destroy(char); char = 0;
std::vector<StockReconcile> UnloadScenario(BattleRuntime& rt, CombatField& field,
                                           const std::vector<int>& originalHp,
                                           const std::vector<u8>& sourceKind) {
    std::vector<StockReconcile> out;
    CombatUnit* units = field.units();
    int n = field.capacity();
    for (int i = 0; i < n; ++i) {
        CombatUnit& u = units[i];
        if (u.marker == -1)            // free slot (word_B5A350 == -1)
            continue;
        // The HP delta writeback excludes guards (source kind 11).
        u8 kind = (i < static_cast<int>(sourceKind.size())) ? sourceKind[i] : 0;
        if (kind != 11) {
            int orig = (i < static_cast<int>(originalHp.size())) ? originalHp[i] : u.hp;
            int delta = u.hp - orig;   // dword_B5A374[i] - *(rec+9)
            out.push_back(StockReconcile{u.id, delta});
            if (CombatCommandSink())
                CombatCommandSink()->OnUnitDamage(u.id, delta);
        }
        // Character_Destroy + slot clear (the live actor teardown).
        u.marker = -1;
        u.alive = 0;
    }
    rt.attackers.clear();
    rt.defenders.clear();
    rt.squad = nullptr;
    return out;
}

// ===========================================================================
// DecideAiDeployment — the deployment will-fight / bribe decision (0x48dab0).
// ===========================================================================
DeploymentDecision DecideAiDeployment(int defenderUnitCount, int guardThreshold,
                                      double baseStrength) {
    DeploymentDecision d;
    d.defenderUnitCount = defenderUnitCount;
    d.defenderStrength = -1;

    // The original: `if (!v46 /*count*/ || RandomModulo(5) > *(v72+272))` — i.e.
    // either no defenders, or the patrol-strength roll exceeds the guard count.
    int roll5 = static_cast<int>(static_cast<u16>(Math_RandomModulo(5)));
    if (defenderUnitCount != 0 && roll5 <= guardThreshold) {
        // Defenders present and confident: no will-fight decision branch taken
        // (the original simply leaves dword_6311F8 untouched / falls through to
        // the live battle). willFight stays false, strength -1.
        return d;
    }

    // Pick the confidence bucket: r = RandomModulo(4); 0->10, 1->90, else->50.
    int r = static_cast<int>(static_cast<u16>(Math_RandomModulo(4)));
    int bucket;
    if (r == 0)      bucket = kFightChanceLow;   // 10
    else if (r == 1) bucket = kFightChanceHigh;  // 90
    else             bucket = kFightChanceMid;   // 50

    // The fight roll: RandomModulo(90) + 10 <= bucket -> stand and fight.
    int fightRoll = static_cast<int>(static_cast<u16>(Math_RandomModulo(0x5A))) + 10;
    if (fightRoll <= bucket) {
        d.willFight = true;   // dword_6311F4 = 1 in the original (battle skipped)
        // dword_6311F8 = ComputeDefenderStrength(...) * 0.01.
        d.defenderStrength = static_cast<int>(baseStrength * kBribeStrengthScale);
    } else {
        d.willFight = false;  // declined: dword_6311F8 = -1 (proceed to battle)
        d.defenderStrength = -1;
    }
    return d;
}

// ===========================================================================
// Result-screen DATA flow (0x48e4e4).
// ===========================================================================
namespace {
int CountEffectiveSide(const std::vector<CombatUnitAI*>& side) {
    int n = 0;
    for (const CombatUnitAI* u : side) {
        if (!u || !u->unit) continue;
        u8 a = u->unit->alive;
        // alive byte set, != 4 (fled), not captured (captured pre-filtered).
        if (a != 0 && a != 4)
            ++n;
    }
    return n;
}
} // namespace

// gilde.exe 0x48e4e4 (the winner-pick block at the top).
BattleWinner DetermineResultWinner(const BattleRuntime& rt) {
    if (rt.forcedWinner != BattleWinner::Undecided) {
        // dword_6311F0 names the *eliminated* side: forced==attacker -> the
        // surviving winner is the defender, and vice-versa.
        return (rt.forcedWinner == BattleWinner::Attacker) ? BattleWinner::Defender
                                                           : BattleWinner::Attacker;
    }
    // Original rosters: v11 counts the +128 side (defender owner) and v15 the
    // +192 side (attacker owner) — see the LoadScenarioAssets roster-to-team
    // mapping. The rule is `if (v15 <= v11) winner = defender; else attacker`,
    // i.e. the attacker wins iff its effective count STRICTLY exceeds the
    // defender's; ties go to the defender.
    int atk = CountEffectiveSide(rt.attackers);   // v15 (attacker-owner side)
    int def = CountEffectiveSide(rt.defenders);   // v11 (defender-owner side)
    return (atk <= def) ? BattleWinner::Defender : BattleWinner::Attacker;
}

// gilde.exe 0x48e4e4 (the flags&4 mercenary-payout loop).
//   pay = MultiplyByRate(40 * (int)((double)skill * (1/42) + 1.0), rate);
//   total += pay;
int ComputeMercenaryPayout(const std::vector<u8>& skills, double rate) {
    int total = 0;
    for (u8 skill : skills) {
        // v99 = (double)(i16)skill * (1/42) + 1.0;  v103 = MultiplyByRate(40*(int)v99, rate).
        double v99 = static_cast<double>(static_cast<i16>(skill)) *
                         static_cast<double>(kMercPaySkillScale) + 1.0;
        int base = kMercPayBase * static_cast<int>(v99);   // 40 * (int)v99
        int pay = static_cast<int>(static_cast<double>(base) * rate); // MultiplyByRate
        total += pay;
    }
    return total;
}

} // namespace guild::sim
