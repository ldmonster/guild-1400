// Integration test: world/mood_religion cross-wired against its REAL RNG siblings
// — guild::crt::rand (the shared ANSI LCG) and guild::util's RandomFloatScaled /
// RandomModulo wrappers. No stubs: the seeded-pick wrappers (SelectMoodColorTiers,
// AdjustRelationByMood) consume the production generator exactly as the originals
// (VIBE_MeisterAi_SelectMoodColor 0x4664d8 / VIBE_Npc_AdjustRelationByMood 0x56840c)
// do, so we verify the random mood-tier selection has a deterministic golden vector
// under a fixed seed.
#include "test.h"

#include "crt/rand.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"
#include "world/mood_religion.h"

using namespace guild;
using namespace guild::world;

// Seed=1 (the CRT initial state). The wrapper draws RandomFloatScaled (d0),
// RandomModulo(2 or 3) (secondRow), then RandomFloatScaled (d1) in that exact
// order against the REAL crt::RandNext LCG. Golden packed values computed in
// python3 against the same generator + threshold table.
TEST(MoodReligionIT, SelectMoodColorTiersSeededAgainstRealLcg) {
    crt::Srand(1);
    CHECK_EQ(SelectMoodColorTiers(0), 0x0203);  // class 0 -> row 2

    crt::Srand(1);
    CHECK_EQ(SelectMoodColorTiers(1), 0x0202);  // class 1 -> row 0 (RandomModulo(3))

    crt::Srand(1);
    CHECK_EQ(SelectMoodColorTiers(2), 0x0203);  // class 2 -> row 1

    crt::Srand(1);
    CHECK_EQ(SelectMoodColorTiers(3), 0x0203);  // default -> row 2
}

// The seeded mood-color pick must be reproducible: re-seeding to the same state
// reproduces the identical packed tier word, and the low byte (primary tier) is
// always 1..5, the high byte (second tier) likewise.
TEST(MoodReligionIT, SelectMoodColorTiersReproducibleAndInRange) {
    crt::Srand(12345);
    int first = SelectMoodColorTiers(0);
    crt::Srand(12345);
    int again = SelectMoodColorTiers(0);
    CHECK_EQ(first, again);

    for (u32 seed = 1; seed <= 64; ++seed) {
        crt::Srand(seed);
        int packed = SelectMoodColorTiers(static_cast<u8>(seed % 4));
        int primary = packed & 0xFF;
        int second  = (packed >> 8) & 0xFF;
        CHECK(primary >= 1 && primary <= 5);
        CHECK(second  >= 1 && second  <= 5);
    }
}

// End-to-end "religion edict" flow: parse the cheat string, then price its
// conversion across a hypothetical matching-people count using the real cost core.
TEST(MoodReligionIT, ReligionEdictParseThenPrice) {
    ReligionCheat k = ParseSetReligion("-KATHOLISCH_40_3");
    CHECK(k.valid);
    CHECK_EQ((int)k.code, 1);
    // 300 catholics at level 40 -> trunc(300*40*0.00999999978f) = 119
    // (the recovered constant is just under 0.01, so it floors to 119, not 120).
    CHECK_EQ(ReligionConversionCost(300, k.level), 119);

    ReligionCheat e = ParseSetReligion("-EVANGELISCH_10_2");
    CHECK(e.valid);
    CHECK_EQ((int)e.code, 0);
    CHECK_EQ(ReligionConversionCost(500, e.level), 49);
}
