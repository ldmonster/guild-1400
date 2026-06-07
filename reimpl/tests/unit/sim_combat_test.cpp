#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/duel.h"
#include "crt/rand.h"
#include "test.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// Struct layout (byte-for-byte against the recovered strides/offsets).
// ===========================================================================
TEST(SimCombat, StructLayout) {
    CHECK_EQ(sizeof(CombatUnit), static_cast<size_t>(536));
    CHECK_EQ(kUnitStride, 536);
    CHECK_EQ(kUnitCapacity, 32);
    CHECK_EQ(offsetof(CombatUnit, marker), static_cast<size_t>(0));
    CHECK_EQ(offsetof(CombatUnit, id), static_cast<size_t>(4));
    CHECK_EQ(offsetof(CombatUnit, alive), static_cast<size_t>(8));
    CHECK_EQ(offsetof(CombatUnit, worth), static_cast<size_t>(28));
    CHECK_EQ(offsetof(CombatUnit, hp), static_cast<size_t>(36));
    CHECK_EQ(offsetof(CombatUnit, teamId), static_cast<size_t>(364));
    CHECK_EQ(offsetof(CombatUnit, actorPtr), static_cast<size_t>(388));
    // ObjectDef partial: weaponType@+0, baseStat@+7, weaponClass@+88.
    CHECK_EQ(offsetof(ObjectDef, weaponType), static_cast<size_t>(0));
    CHECK_EQ(offsetof(ObjectDef, baseStat), static_cast<size_t>(7));
    CHECK_EQ(offsetof(ObjectDef, weaponClass), static_cast<size_t>(88));
}

// ===========================================================================
// RNG helpers — golden vectors (python-computed).
// ===========================================================================

// CRT LCG (crt::RandNext), seed=1, first 12 outputs.
static const int kCrtSeed1[12] = {
    16838, 5758, 10113, 17515, 31051, 5627, 23010, 7419, 16212, 4086, 2749, 12767,
};

TEST(SimCombat, CrtRandNextGolden) {
    crt::Srand(1);
    for (int i = 0; i < 12; ++i)
        CHECK_EQ(crt::RandNext(), kCrtSeed1[i]);
}

TEST(SimCombat, MathRandomModulo) {
    // Math_RandomModulo(n) == RandNext() % n; n==0 -> 0.
    crt::Srand(1);
    CHECK_EQ(Math_RandomModulo(0), 0);  // does not consume the generator
    crt::Srand(1);
    CHECK_EQ(Math_RandomModulo(60), kCrtSeed1[0] % 60);
    CHECK_EQ(Math_RandomModulo(60), kCrtSeed1[1] % 60);
    CHECK_EQ(Math_RandomModulo(100), kCrtSeed1[2] % 100);
}

TEST(SimCombat, RollMeleeDamageGolden) {
    // RollMeleeDamage == RandomModulo(60)+40 -> band [40,99].
    crt::Srand(1);
    int expected[5] = {78, 98, 73, 95, 71};   // python golden, seed=1
    for (int i = 0; i < 5; ++i) {
        int d = RollMeleeDamage();
        CHECK_EQ(d, expected[i]);
        CHECK(d >= 40 && d <= 99);
    }
}

TEST(SimCombat, CutsceneRandIntGolden) {
    CutsceneRng rng;
    rng.state = 12345;
    int expected[5] = {19, 29, 8, 18, 8};   // RandInt(30), seed=12345
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(static_cast<int>(rng.RandInt(30)), expected[i]);
    // n==0 returns the argument unchanged (original early-out).
    CutsceneRng r2; r2.state = 999;
    CHECK_EQ(r2.RandInt(0), 0u);
}

TEST(SimCombat, CutsceneRandFloatGolden) {
    CutsceneRng rng;
    rng.state = 12345;
    double expected[5] = {0.655181884765625, 0.304840087890625, 0.67498779296875,
                          0.10675048828125, 0.5166015625};
    for (int i = 0; i < 5; ++i) {
        double f = rng.RandFloat();
        CHECK(std::fabs(f - expected[i]) < 1e-9);
        CHECK(f >= 0.0 && f < 1.0);
    }
}

// ===========================================================================
// Combat-unit array & id lookup.
// ===========================================================================
TEST(SimCombat, FieldSpawnAndFind) {
    CombatField field;
    CHECK(field.FindUnitById(42) == nullptr);  // empty
    CombatUnit* a = field.Spawn(42, 300, 1);
    CombatUnit* b = field.Spawn(7, 250, 2);
    CHECK(a != nullptr);
    CHECK(b != nullptr);
    CHECK_EQ(field.FindUnitById(42), a);
    CHECK_EQ(field.FindUnitById(7), b);
    CHECK(field.FindUnitById(99) == nullptr);
    CHECK_EQ(a->hp, 300);
    CHECK_EQ(a->teamId, 1);
    CHECK_EQ(static_cast<int>(a->alive), 1);
    CHECK_EQ(static_cast<int>(a->marker), 42);
}

TEST(SimCombat, FieldCapacity) {
    CombatField field;
    for (int i = 0; i < kUnitCapacity; ++i)
        CHECK(field.Spawn(1000 + i, 100, 0) != nullptr);
    CHECK(field.Spawn(2000, 100, 0) == nullptr);  // full
}

// ===========================================================================
// Melee damage + death gate.
// ===========================================================================
TEST(SimCombat, ApplyMeleeHitDamage) {
    crt::Srand(1);
    CombatUnit u{}; u.marker = 1; u.id = 1; u.alive = 1; u.hp = 300;
    bool dead = ApplyMeleeHit(u);   // dmg = 78 (seed=1 first roll)
    CHECK_EQ(u.hp, 300 - 78);
    CHECK(!dead);                   // 222/300 = 0.74 >= 0.05
    CHECK_EQ(static_cast<int>(u.alive), 1);
}

TEST(SimCombat, ApplyUnitDeathGate) {
    CombatUnit u{}; u.alive = 1;
    // ratio >= 0.05 survives.
    CHECK(!ApplyUnitDeath(u, 10.0, 100.0));   // 0.10
    CHECK_EQ(static_cast<int>(u.alive), 1);
    CHECK(!ApplyUnitDeath(u, 5.0, 100.0));    // exactly 0.05 -> survives
    CHECK_EQ(static_cast<int>(u.alive), 1);
    // ratio < 0.05 dies.
    CHECK(ApplyUnitDeath(u, 4.0, 100.0));     // 0.04
    CHECK_EQ(static_cast<int>(u.alive), 0);
    // negative HP definitely dead.
    CombatUnit v{}; v.alive = 1;
    CHECK(ApplyUnitDeath(v, -10.0, 100.0));
    CHECK_EQ(static_cast<int>(v.alive), 0);
}

TEST(SimCombat, ApplyMeleeHitToDeath) {
    // A unit at low HP: one melee hit drops it below the 0.05 ratio (post/pre).
    crt::Srand(1);
    CombatUnit u{}; u.marker = 1; u.id = 1; u.alive = 1; u.hp = 80;
    // dmg=78 -> hp=2; 2/80 = 0.025 < 0.05 -> dead.
    bool dead = ApplyMeleeHit(u);
    CHECK_EQ(u.hp, 2);
    CHECK(dead);
    CHECK_EQ(static_cast<int>(u.alive), 0);
}

// ===========================================================================
// Distance & target selection.
// ===========================================================================
TEST(SimCombat, DistanceXZ) {
    CHECK(std::fabs(DistanceXZ(0, 0, 3, 4) - 5.0) < 1e-6);
    CHECK(std::fabs(DistanceXZ(1, 1, 1, 1) - 0.0) < 1e-6);
    CHECK(std::fabs(DistanceXZ(-1, -1, 2, 3) - 5.0) < 1e-6);
}

TEST(SimCombat, FindNearestEnemyUnit) {
    CombatUnit ally{};  ally.alive = 1;  ally.teamId = 1;
    CombatUnit near{};  near.alive = 1;  near.teamId = 2;
    CombatUnit far{};   far.alive = 1;   far.teamId = 2;
    CombatUnit dead{};  dead.alive = 0;  dead.teamId = 2;
    std::vector<UnitPose> cand = {
        {&ally, 1.0f, 0.0f, 1.0},   // same team -> ignored
        {&far,  10.0f, 0.0f, 1.0},  // far
        {&near, 3.0f, 0.0f, 1.0},   // nearest enemy
        {&dead, 0.5f, 0.0f, 1.0},   // dead -> ignored
    };
    const CombatUnit* pick = FindNearestEnemyUnit(0, 0, 1, cand);
    CHECK_EQ(pick, &near);
    // No enemies -> nullptr.
    std::vector<UnitPose> none = {{&ally, 1.0f, 0.0f, 1.0}};
    CHECK(FindNearestEnemyUnit(0, 0, 1, none) == nullptr);
    // hpRatio weighting: a far enemy with very low hpRatio beats a near full one.
    std::vector<UnitPose> weighted = {
        {&near, 3.0f, 0.0f, 1.0},   // weighted score 3*1.0 = 3
        {&far, 10.0f, 0.0f, 0.1},   // weighted score 10*0.1 = 1 -> wins
    };
    CHECK_EQ(FindNearestEnemyUnit(0, 0, 1, weighted), &far);
}

// ===========================================================================
// Command hook (lockstep mock).
// ===========================================================================
namespace {
struct MockSink : ICombatCommandSink {
    int lastDmgId = -1, lastHp = 0, lastDeathId = -1;
    int dmgCalls = 0, deathCalls = 0;
    void OnUnitDamage(i32 id, i32 hp) override { lastDmgId = id; lastHp = hp; ++dmgCalls; }
    void OnUnitDeath(i32 id) override { lastDeathId = id; ++deathCalls; }
};
} // namespace

TEST(SimCombat, CommandSinkRoutesDamageAndDeath) {
    MockSink sink;
    SetCombatCommandSink(&sink);
    CHECK_EQ(CombatCommandSink(), static_cast<ICombatCommandSink*>(&sink));

    crt::Srand(1);
    CombatUnit u{}; u.id = 55; u.alive = 1; u.hp = 80;
    bool dead = ApplyMeleeHit(u);   // dmg=78 -> hp=2 -> dead
    CHECK(dead);
    CHECK_EQ(sink.dmgCalls, 1);
    CHECK_EQ(sink.lastDmgId, 55);
    CHECK_EQ(sink.lastHp, 2);
    CHECK_EQ(sink.deathCalls, 1);
    CHECK_EQ(sink.lastDeathId, 55);

    SetCombatCommandSink(nullptr);
    CHECK(CombatCommandSink() == nullptr);
}

// ===========================================================================
// Duel rules.
// ===========================================================================
TEST(SimCombat, DuelRngMod) {
    DuelRng rng;
    rng.table = {100, 201, 302, 7};
    CHECK_EQ(rng.Mod(10), 0);   // 100%10
    CHECK_EQ(rng.Mod(10), 1);   // 201%10
    CHECK_EQ(rng.Mod(10), 2);   // 302%10
    CHECK_EQ(rng.Mod(10), 7);   // 7%10
    CHECK_EQ(rng.Mod(10), 0);   // wraps to table[0]
    DuelRng empty;
    CHECK_EQ(empty.Mod(5), 0);  // empty table -> 0
}

TEST(SimCombat, DuelCheckFatalHit) {
    DuelState s;
    CHECK(!Duel_CheckFatalHit(s, 30.0, 100.0));  // 0.3 >= 0.2 -> survives
    CHECK(!s.over);
    CHECK(!Duel_CheckFatalHit(s, 20.0, 100.0));  // exactly 0.2 -> survives
    CHECK(!s.over);
    CHECK(Duel_CheckFatalHit(s, 19.0, 100.0));   // 0.19 < 0.2 -> fatal
    CHECK(s.over);
}

TEST(SimCombat, DuelShotMissAndHitScoring) {
    // Build a deterministic CutsceneRng. With skillA=0.0, chance=0 -> always miss.
    CutsceneRng rng; rng.state = 12345;
    DuelState s; s.skillA = 0.0f;
    CombatUnit t{}; t.hp = 100; t.worth = 100.0f;
    DuelShotResult r = Duel_ResolveShot(s, /*shooterIsA=*/true, t, rng);
    CHECK(!r.hit);
    CHECK_EQ(r.damage, 0);
    CHECK_EQ(t.hp, 100);
    CHECK_EQ(static_cast<int>(s.scoreA), 0);

    // High skill -> guaranteed hit; aim off -> dmg = RandInt(30)+15.
    // Recompute golden: state=12345 after one RandFloat() consumed above.
    // First RandFloat (miss roll) used state advance #1. Now next RandFloat is
    // the chance roll #2, then RandInt #3 for the damage. Use a fresh rng to be
    // explicit about the golden values.
    CutsceneRng rng2; rng2.state = 999;
    DuelState s2; s2.skillA = 1.0f;            // chance = 0.8, large
    CombatUnit t2{}; t2.hp = 100; t2.worth = 200.0f;
    DuelShotResult r2 = Duel_ResolveShot(s2, true, t2, rng2);
    // chance=0.8; the roll must be < 0.8 to hit. Verify scoring math is internally
    // consistent: hp drop = (int)(worth * 0.01 * dmg).
    if (r2.hit) {
        CHECK(r2.damage >= 15 && r2.damage <= 44);
        int drop = static_cast<int>(200.0 * 0.01 * r2.damage);
        CHECK_EQ(t2.hp, 100 - drop);
        CHECK_EQ(static_cast<int>(s2.scoreA), r2.damage);
    }
}

TEST(SimCombat, DuelAimNarrowsDamageBand) {
    // aim on -> RandInt(15)+5 (5..19); aim off -> RandInt(30)+15 (15..44).
    // Force a hit by giving a huge skill so chance >> any roll.
    for (int trial = 0; trial < 50; ++trial) {
        CutsceneRng rng; rng.state = 1000 + trial;
        DuelState s; s.skillA = 100.0f; s.aimA = true;
        CombatUnit t{}; t.hp = 1000; t.worth = 1.0f;
        DuelShotResult r = Duel_ResolveShot(s, true, t, rng);
        CHECK(r.hit);
        CHECK(r.damage >= 5 && r.damage <= 19);
    }
}

TEST(SimCombat, DuelTauntAndAimFlags) {
    // Taunt: defender's roll >= attacker's -> attacker rattled.
    // Force determinism with crafted ratings.
    {
        CutsceneRng rng; rng.state = 4242;
        DuelState s;
        // attackerSkill very low, defenderRating4 very high -> defender wins.
        DuelChoiceOutcome o = Duel_ResolveTaunt(s, /*attackerIsA=*/true,
                                                /*attackerSkill=*/0.0f,
                                                /*defenderRating4=*/100.0f, rng);
        CHECK(o == DuelChoiceOutcome::kTauntFailed);
        CHECK(s.rattledA);
    }
    {
        CutsceneRng rng; rng.state = 4242;
        DuelState s;
        DuelChoiceOutcome o = Duel_ResolveTaunt(s, true, 100.0f, 0.0f, rng);
        CHECK(o == DuelChoiceOutcome::kTauntLanded);
        CHECK(!s.rattledA);
    }
    // Aim: RandFloat() < ownRating2 -> gain aim.
    {
        CutsceneRng rng; rng.state = 4242;
        DuelState s;
        DuelChoiceOutcome o = Duel_ResolveAim(s, /*playerIsA=*/true,
                                              /*ownRating2=*/1.5f, rng);  // always < 1.5
        CHECK(o == DuelChoiceOutcome::kAimGained);
        CHECK(s.aimA);
    }
    {
        CutsceneRng rng; rng.state = 4242;
        DuelState s;
        DuelChoiceOutcome o = Duel_ResolveAim(s, true, 0.0f, rng);  // roll >= 0 always
        CHECK(o == DuelChoiceOutcome::kAimMissed);
        CHECK(!s.aimA);
    }
}
