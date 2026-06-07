#include "sim/combat.h"
#include "sim/combat_types.h"
#include "sim/duel.h"
#include "crt/rand.h"
#include "test.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// E2E #1 — a full melee fight between two synthetic combatants.
//
// Two units A (hp 300) and B (hp 300) trade melee blows, A strikes B first then
// they alternate, each blow = ApplyMeleeHit (dmg = RandomModulo(60)+40, death
// when post/pre HP ratio < 0.05). Seeded CRT RNG (Srand(1)). The blow-by-blow
// and winner are checked against a hand/python-computed reference:
//
//   turn 0 A->B dmg=78 hp=222
//   turn 1 B->A dmg=98 hp=202
//   turn 2 A->B dmg=73 hp=149
//   turn 3 B->A dmg=95 hp=107
//   turn 4 A->B dmg=71 hp=78
//   turn 5 B->A dmg=87 hp=20
//   turn 6 A->B dmg=70 hp=8
//   turn 7 B->A dmg=79 hp=-59  -> B's HP ratio (-59/79) < 0.05 -> A DEAD
//   WINNER = B  (A fell first; B survives at hp 8)
// ===========================================================================
TEST(SimCombatE2E, MeleeFightToDeath) {
    crt::Srand(1);
    CombatField field;
    CombatUnit* A = field.Spawn(/*id=*/1, /*hp=*/300, /*team=*/1);
    CombatUnit* B = field.Spawn(/*id=*/2, /*hp=*/300, /*team=*/2);
    CHECK(A && B);

    struct Blow { const char* who; int dmg; int hp; };
    const Blow ref[] = {
        {"A->B", 78, 222}, {"B->A", 98, 202}, {"A->B", 73, 149},
        {"B->A", 95, 107}, {"A->B", 71, 78},  {"B->A", 87, 20},
        {"A->B", 70, 8},   {"B->A", 79, -59},
    };

    const char* winner = nullptr;
    int turn = 0;
    while (turn < 40) {
        bool aStrikes = (turn % 2 == 0);
        CombatUnit& target = aStrikes ? *B : *A;
        int hpBefore = target.hp;
        bool dead = ApplyMeleeHit(target);
        int dmg = hpBefore - target.hp;

        CHECK(turn < static_cast<int>(sizeof(ref) / sizeof(ref[0])));
        CHECK_EQ(dmg, ref[turn].dmg);
        CHECK_EQ(target.hp, ref[turn].hp);

        if (dead) {
            winner = aStrikes ? "A" : "B";  // the striker won (defender fell)
            break;
        }
        ++turn;
    }

    CHECK(winner != nullptr);
    CHECK_EQ(std::string(winner), std::string("B"));
    CHECK_EQ(turn, 7);
    CHECK_EQ(A->hp, -59);
    CHECK_EQ(B->hp, 8);
    CHECK_EQ(static_cast<int>(A->alive), 0);  // A died
    CHECK_EQ(static_cast<int>(B->alive), 1);  // B survives
}

// ===========================================================================
// E2E #2 — a full pistol duel: choice round (taunt, aim) then shots to death.
//
// A (skill 0.9) duels B (skill 0.7). CutsceneRng seeded at state=777.
//   1. A TAUNTS (B's rating-4 = 0.3): v41=1.172614, def=0.451459 -> A NOT rattled.
//   2. A AIMS (rating-2 = 0.8): roll=0.286316 < 0.8 -> A gains aim.
//   3. A SHOOTS B (worth 200, hp 100). chance=0.9*0.8=0.72, aim band 5..19:
//        shot1 roll=0.563354 HIT dmg=19 hp=62 scoreA=19
//        shot2 roll=0.577362 HIT dmg=12 hp=38 scoreA=31
//        shot3 roll=0.418243 HIT dmg=12 hp=14 -> 14/100=0.14 < 0.2 -> FATAL
//   WINNER = A, shots=3, scoreA=43, B final hp=14.
// ===========================================================================
TEST(SimCombatE2E, PistolDuelToDeath) {
    CutsceneRng rng;
    rng.state = 777;

    CombatField field;
    CombatUnit* uA = field.Spawn(/*id=*/10, /*hp=*/100, /*team=*/1);
    CombatUnit* uB = field.Spawn(/*id=*/20, /*hp=*/100, /*team=*/2);
    uA->worth = 200.0f;
    uB->worth = 200.0f;

    DuelState s;
    s.skillA = 0.9f;
    s.skillB = 0.7f;

    // --- choice round ---
    DuelChoiceOutcome taunt = Duel_ResolveTaunt(s, /*attackerIsA=*/true,
                                                /*attackerSkill=*/0.9f,
                                                /*defenderRating4=*/0.3f, rng);
    CHECK(taunt == DuelChoiceOutcome::kTauntLanded);
    CHECK(!s.rattledA);

    DuelChoiceOutcome aim = Duel_ResolveAim(s, /*playerIsA=*/true,
                                            /*ownRating2=*/0.8f, rng);
    CHECK(aim == DuelChoiceOutcome::kAimGained);
    CHECK(s.aimA);

    // --- shots ---
    struct Shot { bool hit; int dmg; int hp; };
    const Shot ref[] = {
        {true, 19, 62}, {true, 12, 38}, {true, 12, 14},
    };

    const double maxHp = 100.0;
    bool fatal = false;
    int shot = 0;
    while (!fatal && shot < 20) {
        DuelShotResult r = Duel_ResolveShot(s, /*shooterIsA=*/true, *uB, rng);
        CHECK(shot < static_cast<int>(sizeof(ref) / sizeof(ref[0])));
        CHECK_EQ(r.hit, ref[shot].hit);
        CHECK_EQ(r.damage, ref[shot].dmg);
        CHECK_EQ(uB->hp, ref[shot].hp);

        if (r.hit)
            fatal = Duel_CheckFatalHit(s, static_cast<double>(uB->hp), maxHp);
        ++shot;
    }

    CHECK(fatal);
    CHECK(s.over);
    CHECK_EQ(shot, 3);
    CHECK_EQ(uB->hp, 14);
    CHECK_EQ(static_cast<int>(s.scoreA), 43);  // 19+12+12
    CHECK_EQ(static_cast<int>(s.scoreB), 0);   // B never shot
    // A is the winner: B is below the fatal ratio, A untouched.
    CHECK(s.scoreA > s.scoreB);
}

// ===========================================================================
// E2E #3 — rattled penalty changes the shot outcome (determinism of flags).
// Same seed, but A is rattled (chance *0.66): the first roll that hit at 0.72
// now misses at 0.475, proving the flag feeds the math.
// ===========================================================================
TEST(SimCombatE2E, RattledPenaltyAltersShot) {
    CutsceneRng base;  base.state = 777;
    // Advance past the two choice rolls + aim roll to reach the shot rolls,
    // matching the duel-to-death scenario's RNG position.
    base.RandFloat();  // taunt v41 component
    base.RandFloat();  // taunt def component
    base.RandFloat();  // aim roll

    CutsceneRng rngClean = base;
    CutsceneRng rngRattled = base;

    CombatUnit tClean{};   tClean.hp = 100;   tClean.worth = 200.0f;
    CombatUnit tRattled{}; tRattled.hp = 100; tRattled.worth = 200.0f;

    DuelState clean;  clean.skillA = 0.9f; clean.aimA = true;
    DuelState rat;    rat.skillA = 0.9f;   rat.aimA = true; rat.rattledA = true;

    DuelShotResult rc = Duel_ResolveShot(clean, true, tClean, rngClean);
    DuelShotResult rr = Duel_ResolveShot(rat,   true, tRattled, rngRattled);

    // The very first shot roll is 0.563354. chance clean=0.72 (hit), rattled=0.4752 (miss).
    CHECK(rc.hit);
    CHECK(!rr.hit);
    CHECK_EQ(tRattled.hp, 100);          // miss -> no HP change
    CHECK(tClean.hp < 100);              // hit -> HP dropped
}
