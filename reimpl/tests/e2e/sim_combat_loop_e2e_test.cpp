// End-to-end test for the combat ORCHESTRATION flow: drive a synthetic battle
// through the real RunBattleLoopFrame driver to resolution, with seeded RNGs and
// mock leaves, and verify the round-by-round state, the final outcome, and the
// emitted command deltas against a hand-computed reference.
//
// Scenario: a 2-attacker vs 2-defender brawl. Each "frame" the mock battle pass
// applies one melee exchange (via PerformAttackAction's connect rule and the
// ResolveMeleeHit death gate) draining HP from the defenders, until a side is
// wiped and EvaluateBattleOutcome (inside RunBattleLoopFrame) decides the winner.
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

namespace {
struct AiHolder {
    CombatUnit unit{};
    CombatUnitAI ai{};
    AiHolder(i32 id, i32 hp, i32 team) {
        unit = CombatUnit{};
        unit.marker = static_cast<i16>(id);
        unit.id = id; unit.hp = hp; unit.teamId = team; unit.alive = 1;
        ai.unit = &unit;
    }
};

struct RecordingSink : ICombatCommandSink {
    std::vector<std::pair<i32,int>> damages;
    std::vector<i32> deaths;
    void OnUnitDamage(i32 id, int newHp) override { damages.push_back({id,newHp}); }
    void OnUnitDeath(i32 id) override { deaths.push_back(id); }
};
} // namespace

TEST(SimCombatLoopE2E, SyntheticBattleToResolution) {
    crt::Srand(0xC0FFEE);
    RecordingSink sink;
    SetCombatCommandSink(&sink);

    // Two attackers (team 100), two defenders (team 200).
    AiHolder a1(1, 100, 100), a2(2, 100, 100);
    AiHolder d1(3, 60, 200),  d2(4, 60, 200);
    a1.ai.weaponType = 342; a2.ai.weaponType = 342;   // swords (melee)

    BattleRuntime rt;
    rt.attackers = {&a1.ai, &a2.ai};
    rt.defenders = {&d1.ai, &d2.ai};

    // The mock battle pass: each frame, each living attacker lands a fixed 25-dmg
    // melee connect on the lowest-id living defender, then we run the death gate.
    // This is deterministic so we can hand-compute the round-by-round HP.
    auto meleeExchange = [&]() {
        for (CombatUnitAI* atk : rt.attackers) {
            if (!atk->unit->alive) continue;
            // pick lowest-id living defender
            CombatUnitAI* tgt = nullptr;
            for (CombatUnitAI* d : rt.defenders)
                if (d->unit->alive && (!tgt || d->unit->id < tgt->unit->id)) tgt = d;
            if (!tgt) break;

            OrderSlot s{}; s.state = kOrderAttack; s.unitId = atk->unit->id;
            // predicted estimate == current target hp; connect subtracts 25.
            AttackActionResult ar = PerformAttackAction(
                s, *atk, tgt, /*predicted*/tgt->unit->hp, /*armed*/true,
                /*hasHit*/true, /*dmg*/25);
            CHECK_EQ(static_cast<int>(ar.kind), static_cast<int>(AttackActionKind::MeleeSwing));
            // Apply the connect to the actual HP (PerformAttackAction writes the
            // scratch; the live battle commits it to hp on the swing landing).
            tgt->unit->hp -= 25;
            // Death gate via the anim-coupled resolver (in-range, fatal if ratio<0.05).
            double maxHp = 60.0;
            ResolveMeleeHit(*atk, *tgt->unit, /*dist*/10.0, /*range*/30.0,
                            /*connected*/true, /*mag*/25,
                            static_cast<double>(tgt->unit->hp), maxHp, atk->weaponType);
        }
    };

    BattlePasses passes;
    passes.updateOrderSlots = meleeExchange;

    RoleWeights w{1.0f,1.0f,1.0f,1.0f,1.0f,1.0f};
    CutsceneRng rng; rng.state = 999;

    // ---- Hand-computed reference --------------------------------------------
    // d1 starts 60. Frame1: a1 hits d1 -> 35; a2 hits d1 -> 10 (ratio .166, alive).
    // Frame2: a1 hits d1 -> -15 (ratio < .05 -> dead); a2 retargets d2 -> 35.
    // Frame3: a1 hits d2 -> 10; a2 hits d2 -> -15 (dead). Both defenders dead.
    // Frame4: RunBattleLoopFrame's EvaluateBattleOutcome sees def==0 -> Attacker.
    // (The outcome may be detected at the end of frame 3's processing.)

    int frames = 0;
    bool cont = true;
    while (cont && frames < 50) {
        cont = RunBattleLoopFrame(rt, /*frameAlive*/true, passes, w, rng);
        ++frames;

        if (frames == 1) {
            CHECK_EQ(d1.unit.hp, 10);    // 60 - 25 - 25
            CHECK_EQ(static_cast<int>(d1.unit.alive), 1);
            CHECK_EQ(d2.unit.hp, 60);
        }
        if (frames == 2) {
            CHECK_EQ(d1.unit.hp, -15);
            CHECK_EQ(static_cast<int>(d1.unit.alive), 0);   // death gate fired
            CHECK_EQ(d2.unit.hp, 35);                        // a2 retargeted d2
        }
    }

    // The battle resolved with the attackers winning.
    CHECK_EQ(static_cast<int>(rt.result), static_cast<int>(BattleWinner::Attacker));
    CHECK(rt.quit);
    CHECK(!cont);
    // Both defenders dead, both attackers untouched.
    CHECK_EQ(static_cast<int>(d1.unit.alive), 0);
    CHECK_EQ(static_cast<int>(d2.unit.alive), 0);
    CHECK_EQ(static_cast<int>(a1.unit.alive), 1);
    CHECK_EQ(static_cast<int>(a2.unit.alive), 1);
    // Two deaths were emitted through the command sink (d1, d2).
    CHECK_EQ(sink.deaths.size(), static_cast<size_t>(2));
    CHECK_EQ(sink.deaths[0], 3);
    CHECK_EQ(sink.deaths[1], 4);
    // Resolved within a small number of frames (4 expected: 3 to wipe + 1 detect,
    // but outcome can latch on frame 3's check).
    CHECK(frames <= 4);

    SetCombatCommandSink(nullptr);
}

// A second flow: the full setup -> loop -> teardown lifecycle with the result
// winner + mercenary payout DATA flow, verifying the deployment decision and the
// final reconciliation are consistent.
TEST(SimCombatLoopE2E, SetupResolveTeardownLifecycle) {
    crt::Srand(42);
    RecordingSink sink;
    SetCombatCommandSink(&sink);

    BattleDescriptor b;
    b.modeFlags = kBattleAttack;
    b.attackerOwnerId = 100;
    b.defenderOwnerId = 200;
    b.attackerRoster[0] = 1;
    b.attackerRoster[1] = 2;
    b.defenderRoster[0] = 3;

    CombatField field;
    std::vector<AiHolder*> holders;
    auto factory = [&](i32, CombatUnit* u, i32, bool) -> CombatUnitAI* {
        AiHolder* h = new AiHolder(u->id, u->hp, u->teamId);
        h->ai.unit = u;
        h->ai.weaponType = 342;
        holders.push_back(h);
        return &h->ai;
    };

    BattleRuntime rt;
    LoadScenarioAssets(rt, b, field, factory);
    CHECK_EQ(rt.attackers.size(), static_cast<size_t>(2));
    CHECK_EQ(rt.defenders.size(), static_cast<size_t>(1));

    // Scenario pick for an open attack -> Bergpass.
    ScenarioPick sp = PickScenario(b.modeFlags, false, 0, 0, 0);
    CHECK_EQ(static_cast<int>(sp.scenario), static_cast<int>(CombatScenario::Bergpass));

    // Resolve: kill the defender directly, then run one frame to detect outcome.
    field.FindUnitById(3)->alive = 0;
    field.FindUnitById(3)->hp = 0;
    rt.defenders[0]->unit->alive = 0;

    RoleWeights w{};
    CutsceneRng rng;
    BattlePasses passes;
    bool cont = RunBattleLoopFrame(rt, true, passes, w, rng);
    CHECK(!cont);
    CHECK_EQ(static_cast<int>(rt.result), static_cast<int>(BattleWinner::Attacker));

    // Result winner data flow agrees.
    CHECK_EQ(static_cast<int>(DetermineResultWinner(rt)),
             static_cast<int>(BattleWinner::Attacker));

    // Mercenary payout for the two surviving attackers (skill bytes 42, 42).
    std::vector<u8> skills{42, 42};
    int payout = ComputeMercenaryPayout(skills, 1.0);
    CHECK_EQ(payout, 80 + 80);   // each 40*(42/42+1=2) = 80

    // Teardown: attacker units took no damage -> zero stock delta; defender died.
    std::vector<int> orig(field.capacity(), 0);
    std::vector<u8>  kind(field.capacity(), 0);
    // Slots: 0->id1,1->id2,2->id3 in spawn order.
    orig[0] = 100; orig[1] = 100; orig[2] = 100;
    kind[0] = 5; kind[1] = 5; kind[2] = 5;
    field.FindUnitById(3)->hp = 0;   // defender wiped (loss of 100 stock)
    auto recs = UnloadScenario(rt, field, orig, kind);
    CHECK_EQ(recs.size(), static_cast<size_t>(3));
    // The defender's reconciliation is a full -100 loss.
    bool sawDefenderLoss = false;
    for (auto& r : recs)
        if (r.personId == 3) { CHECK_EQ(r.hpDelta, -100); sawDefenderLoss = true; }
    CHECK(sawDefenderLoss);
    CHECK(field.FindUnitById(1) == nullptr);   // field cleared

    for (auto* h : holders) delete h;
    SetCombatCommandSink(nullptr);
}
