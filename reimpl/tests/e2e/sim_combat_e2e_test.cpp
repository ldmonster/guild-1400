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
// IMPORTANT (gilde.exe 0x4a4b68 / 0x4a4eb4, disasm-verified): the taunt/aim flags
// are CROSS-WIRED. A's taunt sets byte_6315DE (rattledA) and A's aim sets
// byte_6315E0 (aimA), but A's OWN shots read byte_6315DF (rattledB) and
// byte_6315E1 (aimB). So A taunting/aiming does NOT affect A's own shots; A
// shoots un-rattled with the WIDE damage band.
//   1. A TAUNTS (B's rating-4 = 0.3): rf1=0.272622, rf2=0.151463 ->
//      def(0.451463) < v36(1.172622) -> A LANDS the taunt -> rattledA set,
//      outcome kTauntLanded. (Penalizes B's shots, not A's.)
//   2. A AIMS (rating-2 = 0.8): roll=0.286325 < 0.8 -> A gains aim (aimA).
//   3. A SHOOTS B (worth 200, hp 100). chance=0.9*0.8=0.72, aim OFF for A's
//      shots (reads aimB=false), band 15..44:
//        shot1 roll=0.563372 HIT dmg=29 hp=42  (score on TARGET B: scoreB=29)
//        shot2 roll=0.577380 HIT dmg=37 hp=-32 -> ratio < 0.2 -> FATAL (scoreB=66)
//   WINNER = A, shots=2, scoreB=66 (damage dealt TO B), B final hp=-32.
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
    CHECK(s.rattledA);   // A landed the taunt -> byte_6315DE set (penalizes B)

    DuelChoiceOutcome aim = Duel_ResolveAim(s, /*playerIsA=*/true,
                                            /*ownRating2=*/0.8f, rng);
    CHECK(aim == DuelChoiceOutcome::kAimGained);
    CHECK(s.aimA);       // byte_6315E0 set (read by B's shots, not A's)

    // --- shots ---
    struct Shot { bool hit; int dmg; int hp; };
    const Shot ref[] = {
        {true, 29, 42}, {true, 37, -32},
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
    CHECK_EQ(shot, 2);
    CHECK_EQ(uB->hp, -32);
    // Score is keyed on the TARGET: A's hits accumulate into scoreB (damage to B).
    CHECK_EQ(static_cast<int>(s.scoreB), 66);  // 29+37
    CHECK_EQ(static_cast<int>(s.scoreA), 0);   // A took no damage
}

// ===========================================================================
// E2E #3 — rattled penalty changes the shot outcome (determinism of flags).
// Same seed, but A's shot is rattled (chance *0.66): the first roll that hit at
// 0.72 now misses at 0.475, proving the flag feeds the math. NOTE the rattle byte
// is CROSS-WIRED: shooter A reads byte_6315DF (rattledB), so to rattle A's shot we
// set rattledB (gilde.exe 0x4a4bcd..0x4a4be6, disasm-verified).
// ===========================================================================
TEST(SimCombatE2E, RattledPenaltyAltersShot) {
    CutsceneRng base;  base.state = 777;
    // Advance past the two choice rolls + aim roll to reach the shot rolls,
    // matching the duel-to-death scenario's RNG position.
    base.RandFloat();  // taunt v36 component
    base.RandFloat();  // taunt def component
    base.RandFloat();  // aim roll

    CutsceneRng rngClean = base;
    CutsceneRng rngRattled = base;

    CombatUnit tClean{};   tClean.hp = 100;   tClean.worth = 200.0f;
    CombatUnit tRattled{}; tRattled.hp = 100; tRattled.worth = 200.0f;

    DuelState clean;  clean.skillA = 0.9f;
    // Cross-wired: A's shot reads rattledB, so set rattledB to penalize A.
    DuelState rat;    rat.skillA = 0.9f;   rat.rattledB = true;

    DuelShotResult rc = Duel_ResolveShot(clean, true, tClean, rngClean);
    DuelShotResult rr = Duel_ResolveShot(rat,   true, tRattled, rngRattled);

    // The very first shot roll is 0.563372. chance clean=0.72 (hit), rattled=0.4752 (miss).
    CHECK(rc.hit);
    CHECK(!rr.hit);
    CHECK_EQ(tRattled.hp, 100);          // miss -> no HP change
    CHECK(tClean.hp < 100);              // hit -> HP dropped
}
