// End-to-end test for the combat BATTLE module: stage a small battle (two squads
// of two units each, fixed stats, seeded RNG), run the order-tick attack loop
// round by round until one side is wiped out, and verify the round-by-round HP /
// alive state and the final outcome against a hand-computed reference.
//
// The round driver mirrors VIBE_Combat_RunBattleLoop's cadence: each tick every
// live unit on each side runs its attack order (EvaluateAttack hit-chance gate),
// and on a hit applies a melee damage roll (RandomModulo(60)+40) through the
// ApplyUnitDeath gate (death when current/maxHp < 0.05). The order is attackers
// then defenders, each acting on the first live enemy — fully deterministic under
// a single seeded CRT generator.
#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/combat_battle.h"
#include "sim/projectile.h"
#include "crt/rand.h"
#include "test.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct Combatant {
    CombatUnitAI ai;
    int maxHp = 150;
};

CombatUnitAI* FirstAlive(std::vector<Combatant*>& side) {
    for (Combatant* c : side)
        if (c->ai.unit->alive)
            return &c->ai;
    return nullptr;
}

int CountAlive(std::vector<Combatant*>& side) {
    int n = 0;
    for (Combatant* c : side)
        if (c->ai.unit->alive)
            ++n;
    return n;
}

// One unit acts against the first live enemy: hit-chance gate, then melee damage.
void Act(Combatant& attacker, std::vector<Combatant*>& enemies, CutsceneRng& rng) {
    (void)attacker;                        // the acting unit only consumes RNG here
    CombatUnitAI* tgt = FirstAlive(enemies);
    if (!tgt)
        return;
    // High accuracy (255 * skill 100 -> chance clamps so a fire happens on roll>0).
    AttackEval e = EvaluateAttack(/*dist*/10.0, /*range*/100.0, /*acc*/255,
                                  /*skill*/100, /*mod*/1.0, /*hasTarget*/true,
                                  static_cast<float>(tgt->unit->worth),
                                  /*min*/0, /*max*/0, rng);
    if (!e.fires)
        return;
    int dmg = RollMeleeDamage();           // RandomModulo(60)+40 (CRT LCG)
    tgt->unit->hp -= dmg;                  // death gate applied by the caller
}

} // namespace

TEST(SimCombatBattleE2E, TwoSquadAttackLoop) {
    crt::Srand(42);                        // seeds the hit-chance + melee rolls
    CutsceneRng rng; rng.state = 1;        // damage-band RNG (unused: min==max==0)

    CombatField field;
    // Two attacker units, two defender units; all 150 HP, fixed max 150.
    Combatant a0; a0.ai.unit = field.Spawn(1, 150, 1); a0.ai.unit->worth = 1.0f;
    Combatant a1; a1.ai.unit = field.Spawn(2, 150, 1); a1.ai.unit->worth = 1.0f;
    Combatant d0; d0.ai.unit = field.Spawn(3, 150, 2); d0.ai.unit->worth = 1.0f;
    Combatant d1; d1.ai.unit = field.Spawn(4, 150, 2); d1.ai.unit->worth = 1.0f;

    std::vector<Combatant*> atk{&a0, &a1};
    std::vector<Combatant*> def{&d0, &d1};

    auto applyDeath = [](Combatant* c) {
        if (c->ai.unit->alive &&
            static_cast<double>(c->ai.unit->hp) / static_cast<double>(c->maxHp) < kDeathRatio)
            c->ai.unit->alive = 0;
    };

    // Hand-computed reference (python), round by round (hp + alive per unit):
    //   R1: atk[97,150] alive[1,1]  def[-4,150] alive[0,1]
    //   R2: atk[97,150] alive[1,1]  def[-4,5]   alive[0,0]
    struct Ref { int aHp[2]; int aAlive[2]; int dHp[2]; int dAlive[2]; };
    const Ref ref[2] = {
        {{97, 150}, {1, 1}, {-4, 150}, {0, 1}},
        {{97, 150}, {1, 1}, {-4,   5}, {0, 0}},
    };

    int round = 0;
    std::vector<CombatUnitAI*> atkAI{&a0.ai, &a1.ai};
    std::vector<CombatUnitAI*> defAI{&d0.ai, &d1.ai};

    while (CountAlive(atk) > 0 && CountAlive(def) > 0 && round < 20) {
        // attackers act
        for (Combatant* c : atk) {
            if (c->ai.unit->alive) {
                Act(*c, def, rng);
                for (Combatant* e : def) applyDeath(e);
            }
        }
        // defenders act
        for (Combatant* c : def) {
            if (c->ai.unit->alive) {
                Act(*c, atk, rng);
                for (Combatant* e : atk) applyDeath(e);
            }
        }

        // Outcome check (mirrors EvaluateBattleOutcome each tick).
        BattleWinner w = EvaluateBattleOutcome(atkAI, defAI);
        (void)w;

        // Verify against the reference.
        CHECK_EQ(a0.ai.unit->hp, ref[round].aHp[0]);
        CHECK_EQ(a1.ai.unit->hp, ref[round].aHp[1]);
        CHECK_EQ(static_cast<int>(a0.ai.unit->alive != 0), ref[round].aAlive[0]);
        CHECK_EQ(static_cast<int>(a1.ai.unit->alive != 0), ref[round].aAlive[1]);
        CHECK_EQ(d0.ai.unit->hp, ref[round].dHp[0]);
        CHECK_EQ(d1.ai.unit->hp, ref[round].dHp[1]);
        CHECK_EQ(static_cast<int>(d0.ai.unit->alive != 0), ref[round].dAlive[0]);
        CHECK_EQ(static_cast<int>(d1.ai.unit->alive != 0), ref[round].dAlive[1]);
        ++round;
    }

    CHECK_EQ(round, 2);                              // resolved in two rounds
    CHECK_EQ(CountAlive(atk), 2);                    // both attackers survive
    CHECK_EQ(CountAlive(def), 0);                    // defenders wiped

    // Final outcome: defenders empty -> attacker wins.
    CHECK(EvaluateBattleOutcome(atkAI, defAI) == BattleWinner::Attacker);
}

// A second e2e flow: the abstract auto-resolve path produces a deterministic
// winner from the squad strengths, and EvaluateBattleOutcome agrees once HP is
// zeroed on the losing side.
TEST(SimCombatBattleE2E, AutoResolveThenOutcome) {
    crt::Srand(1);
    CombatField field;
    CombatUnit* a = field.Spawn(1, 100, 1);
    CombatUnitAI ua; ua.unit = a; ua.weaponType = 344;        // weight 1.0
    ua.productionRating = 10.0f; ua.outputRatio = 1.0f;       // strength 10
    CombatUnit* d = field.Spawn(2, 100, 2);
    CombatUnitAI ud; ud.unit = d; ud.weaponType = 340;        // weight 0.5
    ud.productionRating = 1.0f; ud.outputRatio = 1.0f;        // strength 0.5

    std::vector<CombatUnitAI*> atk{&ua};
    std::vector<CombatUnitAI*> def{&ud};

    AutoResolveResult r = AutoResolveBattle(atk, def);
    // defScore = RandomModulo(30)+0 = 16838%30 = 8; atkScore = (u16)(10 + 5758%30=28) = 38.
    CHECK_EQ(r.defenderScore, 8);
    CHECK_EQ(r.attackerScore, 38);
    // Per the original's (inverted-looking) comparison defScore<=atkScore -> DEFENDER.
    CHECK(r.winner == BattleWinner::Defender);

    // Apply the auto-resolve verdict: loser (attacker) is removed; verify the
    // live-count outcome then matches.
    a->alive = 0;
    CHECK(EvaluateBattleOutcome(atk, def) == BattleWinner::Defender);
}
