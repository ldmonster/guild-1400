// Unit tests for the combat ORCHESTRATION module (combat_loop.{h,cpp} +
// combat_action.{h,cpp}): the battle-loop frame driver, the per-frame battle
// state pass, attack-action sequencing, the anim-coupled melee resolver, and the
// deployment / scenario-setup / result DATA flow. RNGs (CRT LCG + cutscene LCG)
// are reproduced bit-exact; golden values are hand-computed from the recovered
// pseudocode.
#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/combat_battle.h"
#include "sim/combat_action.h"
#include "sim/combat_loop.h"
#include "crt/rand.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Helpers: build a CombatUnitAI backed by an owned CombatUnit.
// ---------------------------------------------------------------------------
namespace {
struct AiHolder {
    CombatUnit unit{};
    CombatUnitAI ai{};
    AiHolder(i32 id, i32 hp, i32 team, u8 alive = 1) {
        unit = CombatUnit{};
        unit.marker = static_cast<i16>(id);
        unit.id = id;
        unit.hp = hp;
        unit.teamId = team;
        unit.alive = alive;
        ai.unit = &unit;
    }
};

// A command sink that records the deltas the rules emit.
struct RecordingSink : ICombatCommandSink {
    std::vector<std::pair<i32,int>> damages;
    std::vector<i32> deaths;
    void OnUnitDamage(i32 unitId, int newHp) override { damages.push_back({unitId,newHp}); }
    void OnUnitDeath(i32 unitId) override { deaths.push_back(unitId); }
};
} // namespace

// ===========================================================================
// TickBattleState — pass ORDER + quit latch.
// ===========================================================================
TEST(SimCombatLoop, TickBattleStateRunsPassesInOrder) {
    BattleRuntime rt;
    std::vector<int> order;
    BattlePasses p;
    p.updateOrderSlots     = [&]{ order.push_back(0); };
    p.pickAndAssignTargets = [&]{ order.push_back(1); };
    p.updateProjectiles    = [&]{ order.push_back(2); };
    p.refreshHealthBars    = [&]{ order.push_back(3); };
    p.updateBombExplosions = [&]{ order.push_back(4); };
    p.updateThrownBombs    = [&]{ order.push_back(5); };
    TickBattleState(rt, p);
    CHECK_EQ(order.size(), static_cast<size_t>(6));
    for (int i = 0; i < 6; ++i) CHECK_EQ(order[static_cast<size_t>(i)], i);
    CHECK(!rt.quit);   // no forced winner -> no latch
}

TEST(SimCombatLoop, TickBattleStateLatchesOnForcedWinner) {
    BattleRuntime rt;
    rt.forcedWinner = BattleWinner::Attacker;
    BattlePasses p;   // all null (skipped)
    TickBattleState(rt, p);
    CHECK(rt.quit);
}

// ===========================================================================
// RunBattleLoopFrame — guards + cadence + outcome.
// ===========================================================================
TEST(SimCombatLoop, FrameStopsWhenFrameDead) {
    BattleRuntime rt;
    CutsceneRng rng;
    RoleWeights w{0.5f,0.6f,0.7f,0.8f,0.9f,1.0f};
    BattlePasses p;
    CHECK(!RunBattleLoopFrame(rt, /*frameAlive*/false, p, w, rng));
}

TEST(SimCombatLoop, FrameStopsOnDeployAbort) {
    BattleRuntime rt;
    rt.deployAbort = true;
    CutsceneRng rng;
    RoleWeights w{};
    BattlePasses p;
    CHECK(!RunBattleLoopFrame(rt, true, p, w, rng));
}

TEST(SimCombatLoop, FramePausedSkipsBattlePass) {
    BattleRuntime rt;
    rt.paused = true;
    bool ticked = false;
    BattlePasses p;
    p.updateOrderSlots = [&]{ ticked = true; };
    CutsceneRng rng;
    RoleWeights w{};
    bool cont = RunBattleLoopFrame(rt, true, p, w, rng);
    CHECK(!ticked);  // paused -> battle pass skipped
    CHECK(cont);     // still running (no winner)
}

TEST(SimCombatLoop, FrameDetectsOutcomeWhenOneSideEmpty) {
    BattleRuntime rt;
    // Attacker alive, defenders all dead -> attacker wins, loop should terminate.
    AiHolder a(1, 100, 7);
    AiHolder d(2, 0, 9, /*alive*/0);
    rt.attackers = {&a.ai};
    rt.defenders = {&d.ai};
    CutsceneRng rng;
    RoleWeights w{};
    BattlePasses p;
    bool cont = RunBattleLoopFrame(rt, true, p, w, rng);
    CHECK_EQ(static_cast<int>(rt.result), static_cast<int>(BattleWinner::Attacker));
    CHECK(rt.quit);
    CHECK(!cont);
}

TEST(SimCombatLoop, RoleCadenceFiresAfterInterval) {
    BattleRuntime rt;
    AiHolder a(1, 100, 7);
    rt.attackers = {&a.ai};
    rt.lastRoleAssignTick = 0;
    rt.gameTick = kRoleAssignInterval + 1;   // crosses the cadence threshold
    // A weights row that always picks role 0 (cumulative threshold 1.0 at index 0).
    RoleWeights w{1.0f,1.0f,1.0f,1.0f,1.0f,1.0f};
    CutsceneRng rng; rng.state = 12345;
    BattlePasses p;
    RunBattleLoopFrame(rt, true, p, w, rng);
    // The cadence anchor advanced to `now`.
    CHECK_EQ(rt.lastRoleAssignTick, rt.gameTick);
    // The unit got a role assigned (was unassigned, so always assigned to 0).
    CHECK_EQ(static_cast<int>(a.ai.role), 0);
}

// ===========================================================================
// PerformAttackAction — validate -> classify -> apply.
// ===========================================================================
TEST(SimCombatLoop, PerformAttackEmptySlotNoop) {
    OrderSlot s{}; s.state = kOrderIdle; s.unitId = -1;
    AiHolder a(1, 100, 7);
    AttackActionResult r = PerformAttackAction(s, a.ai, nullptr, 0, false, false, 0);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(AttackActionKind::None));
}

TEST(SimCombatLoop, PerformAttackMeleeAppliesDamageOnHit) {
    crt::Srand(1);
    OrderSlot s{}; s.state = kOrderAttack; s.unitId = 1;
    AiHolder a(1, 100, 7); a.ai.weaponType = 342;   // sword (melee)
    AiHolder t(2, 80, 9);
    // armed + hasHit + damage 25: target scratch = predicted(70) then -= 25 = 45.
    AttackActionResult r = PerformAttackAction(s, a.ai, &t.ai, /*predicted*/70,
                                               /*armed*/true, /*hasHit*/true, /*dmg*/25);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(AttackActionKind::MeleeSwing));
    CHECK(r.connected);
    CHECK_EQ(r.appliedDamage, 25);
    CHECK_EQ(t.unit.targetScratch, 70 - 25);   // predicted then connect delta
}

TEST(SimCombatLoop, PerformAttackMeleeArmedButMissNoDelta) {
    crt::Srand(2);
    OrderSlot s{}; s.state = kOrderAttack; s.unitId = 1;
    AiHolder a(1, 100, 7); a.ai.weaponType = 340;   // stab (melee)
    AiHolder t(2, 80, 9);
    AttackActionResult r = PerformAttackAction(s, a.ai, &t.ai, /*predicted*/50,
                                               /*armed*/true, /*hasHit*/false, /*dmg*/25);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(AttackActionKind::MeleeSwing));
    CHECK(!r.connected);
    CHECK_EQ(t.unit.targetScratch, 50);   // only the predicted estimate, no delta
}

TEST(SimCombatLoop, PerformAttackThrowBomb) {
    crt::Srand(3);
    OrderSlot s{}; s.state = kOrderAttack; s.unitId = 1;
    AiHolder a(1, 100, 7); a.ai.weaponType = kWpnThrowBomb;  // 374
    AttackActionResult r = PerformAttackAction(s, a.ai, nullptr, 0,
                                               /*armed*/true, /*hasHit*/false, 0);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(AttackActionKind::ThrowBomb));
}

TEST(SimCombatLoop, PerformAttackDropBomb) {
    OrderSlot s{}; s.state = kOrderAttack; s.unitId = 1;
    AiHolder a(1, 100, 7); a.ai.weaponType = kWpnDropBomb;   // 372
    AttackActionResult r = PerformAttackAction(s, a.ai, nullptr, 0,
                                               /*armed*/false, /*hasHit*/true, 0);
    CHECK_EQ(static_cast<int>(r.kind), static_cast<int>(AttackActionKind::DropBomb));
}

// The shout-gate RandomModulo(10) is consumed unconditionally in the melee branch:
// verify the CRT LCG advanced by exactly one draw.
TEST(SimCombatLoop, PerformAttackConsumesShoutRoll) {
    OrderSlot s{}; s.state = kOrderAttack; s.unitId = 1;
    AiHolder a(1, 100, 7); a.ai.weaponType = 342;
    // The melee branch consumes exactly one RandomModulo(10) (the shout gate).
    // Reseed and compare: after the action, the next RandNext must equal the
    // value seen after an explicit single Math_RandomModulo(10) draw.
    crt::Srand(99);
    Math_RandomModulo(10);
    int expectedNext = crt::RandNext();
    crt::Srand(99);
    PerformAttackAction(s, a.ai, nullptr, 0, /*armed*/true, /*hasHit*/false, 0);
    CHECK_EQ(crt::RandNext(), expectedNext);
}

// ===========================================================================
// ResolveMeleeHit — range gate + death gate + banner.
// ===========================================================================
TEST(SimCombatLoop, ResolveMeleeOutOfRangeWhiffs) {
    AiHolder a(1, 100, 7); a.ai.weaponType = 342;
    CombatUnit v{}; v.alive = 1; v.id = 2;
    MeleeResolveResult r = ResolveMeleeHit(a.ai, v, /*dist*/50.0, /*range*/30.0,
                                           /*connected*/true, /*mag*/10, 1.0, 100.0, 342);
    CHECK(!r.inRange);
    CHECK(!r.killed);
    CHECK_EQ(static_cast<int>(v.alive), 1);   // untouched
}

TEST(SimCombatLoop, ResolveMeleeDeathGate) {
    AiHolder a(1, 100, 7); a.ai.weaponType = 340;
    CombatUnit v{}; v.alive = 1; v.id = 2;
    // currentHp/maxHp = 4/100 = 0.04 < kDeathRatio(0.05) -> dies.
    MeleeResolveResult r = ResolveMeleeHit(a.ai, v, 10.0, 30.0, true, 10, 4.0, 100.0, 340);
    CHECK(r.inRange);
    CHECK(r.killed);
    CHECK(!r.defeated);                       // weapon 340 != banner stab
    CHECK_EQ(static_cast<int>(v.alive), 0);   // alive cleared by ApplyUnitDeath
}

TEST(SimCombatLoop, ResolveMeleeSurvivesAboveRatio) {
    AiHolder a(1, 100, 7);
    CombatUnit v{}; v.alive = 1; v.id = 2;
    // 10/100 = 0.10 >= 0.05 -> survives.
    MeleeResolveResult r = ResolveMeleeHit(a.ai, v, 10.0, 30.0, true, 10, 10.0, 100.0, 342);
    CHECK(r.inRange);
    CHECK(!r.killed);
    CHECK_EQ(static_cast<int>(v.alive), 1);
}

TEST(SimCombatLoop, ResolveMeleeBannerStabMarksDefeated) {
    AiHolder a(1, 100, 7);
    CombatUnit v{}; v.alive = 1; v.id = 2;
    // weapon 366 (banner stab), fatal -> defeated marker (+8 = 4).
    MeleeResolveResult r = ResolveMeleeHit(a.ai, v, 10.0, 30.0, /*connected*/true,
                                           /*mag*/10, 1.0, 100.0, kWpnStab2 /*366*/);
    CHECK(r.killed);
    CHECK(r.defeated);
    CHECK_EQ(static_cast<int>(v.alive), kUnitDefeatedMarker);
}

// ===========================================================================
// PickScenario — mode flags + owner-type.
// ===========================================================================
TEST(SimCombatLoop, PickScenarioByModeFlags) {
    CHECK_EQ(static_cast<int>(PickScenario(kBattleRaid, false, 0, 0, 0).scenario),
             static_cast<int>(CombatScenario::StadtAttack));
    CHECK_EQ(static_cast<int>(PickScenario(kBattleDefend, false, 0, 0, 0).scenario),
             static_cast<int>(CombatScenario::Bergpass));
    CHECK_EQ(static_cast<int>(PickScenario(kBattleAttack, false, 0, 0, 0).scenario),
             static_cast<int>(CombatScenario::Bergpass));
}

TEST(SimCombatLoop, PickScenarioRaeuberlagerWarmup) {
    ScenarioPick p = PickScenario(0, /*production*/true, 0x10, 0, 0);
    CHECK_EQ(static_cast<int>(p.scenario), static_cast<int>(CombatScenario::Raeuberlager));
    CHECK_EQ(p.warmupEdge, 15);
}

TEST(SimCombatLoop, PickScenarioByPersonKind) {
    CHECK_EQ(static_cast<int>(PickScenario(0, false, 0, /*kind*/4, /*tax*/2).scenario),
             static_cast<int>(CombatScenario::Unterschlupf));
    CHECK_EQ(static_cast<int>(PickScenario(0, false, 0, /*kind*/19, /*tax*/3).scenario),
             static_cast<int>(CombatScenario::StadtwacheGross));
}

// ===========================================================================
// DecideAiDeployment — will-fight / bribe (CRT LCG, seeded).
// ===========================================================================
TEST(SimCombatLoop, DeploymentNoDefendersTakesDecisionBranch) {
    crt::Srand(7);
    // 0 defenders -> always enters the decision branch. Compute the golden by
    // replaying the exact RandomModulo draws.
    crt::Srand(7);
    int r4 = static_cast<int>(static_cast<u16>(crt::RandNext() % 4));   // RandomModulo(4)? no — order
    (void)r4;
    // Re-run faithfully: the first draw is RandomModulo(5) (count==0 short-circuits
    // the `||` so the roll5 is NOT drawn). Actually `!v46` true -> roll5 skipped.
    crt::Srand(7);
    DeploymentDecision d = DecideAiDeployment(/*count*/0, /*guard*/3, /*strength*/5000.0);
    // Golden: replay RandomModulo(4) then RandomModulo(90).
    crt::Srand(7);
    int rr = crt::RandNext() % 4;
    int bucket = (rr == 0) ? 10 : (rr == 1) ? 90 : 50;
    int fight = (crt::RandNext() % 90) + 10;
    bool willFight = fight <= bucket;
    CHECK_EQ(d.willFight, willFight);
    if (willFight)
        CHECK_EQ(d.defenderStrength, static_cast<int>(5000.0 * 0.01));
    else
        CHECK_EQ(d.defenderStrength, -1);
}

TEST(SimCombatLoop, DeploymentConfidentGuardsSkip) {
    crt::Srand(123);
    // count != 0 and roll5 <= guard -> early return (no decision). Force guard high.
    crt::Srand(123);
    int roll5 = crt::RandNext() % 5;
    crt::Srand(123);
    DeploymentDecision d = DecideAiDeployment(/*count*/3, /*guard*/4, 1000.0);
    if (roll5 <= 4) {   // always true (roll5 in [0,4]) -> skip
        CHECK(!d.willFight);
        CHECK_EQ(d.defenderStrength, -1);
    }
    CHECK_EQ(d.defenderUnitCount, 3);
}

// ===========================================================================
// DetermineResultWinner + mercenary payout.
// ===========================================================================
TEST(SimCombatLoop, ResultWinnerByCount) {
    BattleRuntime rt;
    AiHolder a1(1, 100, 7), a2(2, 100, 7);
    AiHolder d1(3, 0, 9, 0);   // dead defender
    rt.attackers = {&a1.ai, &a2.ai};
    rt.defenders = {&d1.ai};
    // atk effective 2, def effective 0 -> attacker wins.
    CHECK_EQ(static_cast<int>(DetermineResultWinner(rt)),
             static_cast<int>(BattleWinner::Attacker));
}

TEST(SimCombatLoop, ResultWinnerTieGoesDefender) {
    BattleRuntime rt;
    AiHolder a1(1, 100, 7);
    AiHolder d1(2, 100, 9);
    rt.attackers = {&a1.ai};
    rt.defenders = {&d1.ai};
    // atk 1, def 1 -> tie (def <= atk) -> defender.
    CHECK_EQ(static_cast<int>(DetermineResultWinner(rt)),
             static_cast<int>(BattleWinner::Defender));
}

TEST(SimCombatLoop, ResultForcedWinnerNamesEliminated) {
    BattleRuntime rt;
    rt.forcedWinner = BattleWinner::Attacker;   // attacker eliminated -> defender wins
    CHECK_EQ(static_cast<int>(DetermineResultWinner(rt)),
             static_cast<int>(BattleWinner::Defender));
}

TEST(SimCombatLoop, MercenaryPayoutGolden) {
    // skills {42, 84, 0}; v99 = skill/42 + 1 -> {2.0, 3.0, 1.0};
    // 40*(int)v99 = {80, 120, 40}; rate 1.0 -> total 240.
    std::vector<u8> skills{42, 84, 0};
    CHECK_EQ(ComputeMercenaryPayout(skills, 1.0), 80 + 120 + 40);
    // rate 2.0 -> doubled.
    CHECK_EQ(ComputeMercenaryPayout(skills, 2.0), 160 + 240 + 80);
}

// ===========================================================================
// LoadScenarioAssets / UnloadScenario — spawn + reconcile data flow.
// ===========================================================================
TEST(SimCombatLoop, LoadScenarioSpawnsAndTags) {
    BattleRuntime rt;
    BattleDescriptor b;
    b.attackerOwnerId = 100;
    b.defenderOwnerId = 200;
    b.attackerRoster[0] = 11;
    b.attackerRoster[1] = 12;
    b.defenderRoster[0] = 21;
    CombatField field;
    std::vector<AiHolder*> holders;   // keep AIs alive
    auto factory = [&](i32 pid, CombatUnit* u, i32 team, bool) -> CombatUnitAI* {
        (void)pid; (void)team;
        AiHolder* h = new AiHolder(u->id, u->hp, u->teamId);
        h->ai.unit = u;   // bind to the field-owned unit
        holders.push_back(h);
        return &h->ai;
    };
    LoadScenarioAssets(rt, b, field, factory);
    CHECK_EQ(rt.attackers.size(), static_cast<size_t>(2));
    CHECK_EQ(rt.defenders.size(), static_cast<size_t>(1));
    CHECK_EQ(rt.attackerOwnerId, 100);
    CHECK_EQ(rt.defenderOwnerId, 200);
    // Teams tagged from owner ids.
    CHECK_EQ(field.FindUnitById(11)->teamId, 100);
    CHECK_EQ(field.FindUnitById(21)->teamId, 200);
    for (auto* h : holders) delete h;
}

TEST(SimCombatLoop, UnloadReconcilesStockDelta) {
    RecordingSink sink;
    SetCombatCommandSink(&sink);
    BattleRuntime rt;
    CombatField field;
    CombatUnit* u1 = field.Spawn(/*id*/11, /*hp*/100, /*team*/100);
    CombatUnit* u2 = field.Spawn(/*id*/12, /*hp*/100, /*team*/100);
    // After battle, u1 took 30 damage, u2 unscathed.
    u1->hp = 70;
    u2->hp = 100;
    // originalHp/sourceKind indexed by slot; slot0=u1, slot1=u2; kind 11 excluded.
    std::vector<int> orig(field.capacity(), 0);
    std::vector<u8>  kind(field.capacity(), 0);
    orig[0] = 100; orig[1] = 100;
    kind[0] = 5; kind[1] = 11;   // u2 is a guard -> excluded from writeback
    auto recs = UnloadScenario(rt, field, orig, kind);
    CHECK_EQ(recs.size(), static_cast<size_t>(1));   // only u1 reconciled
    CHECK_EQ(recs[0].personId, 11);
    CHECK_EQ(recs[0].hpDelta, 70 - 100);             // -30
    CHECK_EQ(sink.damages.size(), static_cast<size_t>(1));
    // Field cleared.
    CHECK(field.FindUnitById(11) == nullptr);
    SetCombatCommandSink(nullptr);
    (void)u2;
}
