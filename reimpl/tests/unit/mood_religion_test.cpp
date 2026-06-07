// Unit tests: world/mood_religion — golden vectors for the recoverable mood &
// religion arithmetic cores (VIBE_AiMethod_ComputeMoodLevel 0x467a50,
// VIBE_Npc_AdjustRelationByMood 0x56840c, VIBE_Person_AdjustMoodAndNotify 0x594afc,
// VIBE_MeisterAi_SelectMoodColor 0x4664d8, VIBE_Cheat_ParseSetReligion 0x4fbf70).
// Golden values computed with python3 against the recovered IEEE-754 constants.
#include "test.h"

#include "world/mood_religion.h"

using namespace guild;
using namespace guild::world;

// --- ComputeMoodLevel: value*0.25 tiered into 2 / (int)v / 6 -------------------
TEST(MoodReligionUnit, ComputeMoodLevelTiers) {
    CHECK_EQ(ComputeMoodLevel(0.0f),   2);   // 0    <= 2 -> 2
    CHECK_EQ(ComputeMoodLevel(4.0f),   2);   // 1.0  <= 2 -> 2
    CHECK_EQ(ComputeMoodLevel(8.0f),   2);   // 2.0  <= 2 -> 2 (boundary)
    CHECK_EQ(ComputeMoodLevel(12.0f),  3);   // 3.0  -> (int)3.0
    CHECK_EQ(ComputeMoodLevel(24.0f),  6);   // 6.0  -> top tier
    CHECK_EQ(ComputeMoodLevel(40.0f),  6);   // 10   -> top tier
    CHECK_EQ(ComputeMoodLevel(100.0f), 6);
    CHECK_EQ(ComputeMoodLevel(-5.0f),  2);   // negative floors at 2
}

// NOTE: VIBE_Npc_AdjustRelationByMood (0x56840c) is covered by the sim module
// (guild::sim::NpcAdjustRelationByMood) and its own tests — not re-tested here.

// --- ClampMoodDelta: signed delta clamped so result stays in [0,100] -----------
TEST(MoodReligionUnit, ClampMoodDelta) {
    CHECK_EQ(ClampMoodDelta(50, 10),  10);
    CHECK_EQ(ClampMoodDelta(50, -10), -10);
    CHECK_EQ(ClampMoodDelta(95, 20),  5);    // clamps to +100
    CHECK_EQ(ClampMoodDelta(5, -20),  -5);   // clamps to 0
    CHECK_EQ(ClampMoodDelta(0, -5),   0);
    CHECK_EQ(ClampMoodDelta(100, 5),  0);
    CHECK_EQ(ClampMoodDelta(50, 50),  50);   // exactly 100
    CHECK_EQ(ClampMoodDelta(50, 60),  50);   // over -> clamp
}

// --- MoodTierFromDraw: walks a cumulative-threshold row, 1..5 ------------------
TEST(MoodReligionUnit, MoodTierFromDrawRow0) {
    CHECK_EQ(MoodTierFromDraw(0, 0.0),   1);
    CHECK_EQ(MoodTierFromDraw(0, 0.2),   1);  // <= 0.20 -> col 0
    CHECK_EQ(MoodTierFromDraw(0, 0.3),   2);
    CHECK_EQ(MoodTierFromDraw(0, 0.55),  2);  // <= 0.55 -> col 1
    CHECK_EQ(MoodTierFromDraw(0, 0.9),   4);
    CHECK_EQ(MoodTierFromDraw(0, 0.95),  5);
    CHECK_EQ(MoodTierFromDraw(0, 1.0),   5);
    CHECK_EQ(MoodTierFromDraw(0, 1.5),   5);  // past all -> 5
}

// --- SelectMoodColorTiers parametric packing: primary | (second<<8) -----------
TEST(MoodReligionUnit, SelectMoodColorTiersPacking) {
    // moodClass==1 picks row 0; d0=0.3 -> tier 2; secondRow 1, d1=0.5 -> tier 3.
    CHECK_EQ(SelectMoodColorTiers(1, 0.3, 1, 0.5), 0x0302);
    // moodClass==2 picks row 1; d0=0.05 -> tier 1; secondRow 2, d1=0.9 -> tier 5.
    CHECK_EQ(SelectMoodColorTiers(2, 0.05, 2, 0.9), 0x0501);
    // default class picks row 2; d0=0.6 -> tier 4; secondRow 0, d1=0.1 -> tier 1.
    CHECK_EQ(SelectMoodColorTiers(7, 0.6, 0, 0.1), 0x0104);
}

// --- ParseSetReligion: "-<NAME>_<level>_<region>" -----------------------------
TEST(MoodReligionUnit, ParseSetReligionValid) {
    ReligionCheat k = ParseSetReligion("-KATHOLISCH_40_3");
    CHECK(k.valid);
    CHECK_EQ((int)k.code, 1);
    CHECK_EQ(k.level, 40);
    CHECK_EQ(k.region, 3);

    ReligionCheat e = ParseSetReligion("-EVANGELISCH_8_8");
    CHECK(e.valid);
    CHECK_EQ((int)e.code, 0);
    CHECK_EQ(e.level, 8);
    CHECK_EQ(e.region, 8);

    ReligionCheat lo = ParseSetReligion("-KATHOLISCH_1_1");
    CHECK(lo.valid);
    CHECK_EQ(lo.level, 1);
    CHECK_EQ(lo.region, 1);
}

TEST(MoodReligionUnit, ParseSetReligionRejects) {
    CHECK(!ParseSetReligion("KATHOLISCH_40_3").valid);   // missing leading '-'
    CHECK(!ParseSetReligion("-BUDDHIST_40_3").valid);    // unknown religion
    CHECK(!ParseSetReligion("-KATHOLISCH40_3").valid);   // missing first '_'
    CHECK(!ParseSetReligion("-KATHOLISCH_40").valid);    // missing second number
    CHECK(!ParseSetReligion("-KATHOLISCH_X_3").valid);   // non-digit
    CHECK(!ParseSetReligion("-KATHOLISCH_0_3").valid);   // level < 1
    CHECK(!ParseSetReligion("-KATHOLISCH_81_3").valid);  // level > 80
    CHECK(!ParseSetReligion("-KATHOLISCH_40_0").valid);  // region < 1
    CHECK(!ParseSetReligion("-KATHOLISCH_40_9").valid);  // region > 8
    CHECK(!ParseSetReligion(nullptr).valid);
}

// --- ReligionConversionCost: (int)(people*level*0.01) -------------------------
TEST(MoodReligionUnit, ReligionConversionCostGolden) {
    CHECK_EQ(ReligionConversionCost(0, 40),    0);
    CHECK_EQ(ReligionConversionCost(100, 40),  39);
    CHECK_EQ(ReligionConversionCost(768, 80),  614);
    CHECK_EQ(ReligionConversionCost(50, 1),    0);
    CHECK_EQ(ReligionConversionCost(500, 10),  49);
}
