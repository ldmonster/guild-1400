// Unit tests for the illness / disease module (guild::sim) — gilde.exe.
//
// Covers: the disease-event table (byte-exact), the per-group "free"
// eligibility masks, the bitfield pack arithmetic, the disease-candidate
// predicate, and golden-vector picks driven by the shared ANSI LCG with fixed
// seeds (computed in Python against the recovered algorithm).
#include "test.h"

#include "sim/illness.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

// --- Event table is byte-exact (unk_647728). ------------------------------
TEST(SimIllness, EventTable) {
    CHECK_EQ(kDiseaseEvents[0].countMod, 1);
    CHECK_EQ(kDiseaseEvents[1].countMod, 16);
    CHECK_EQ(kDiseaseEvents[4].countMod, 4);
    CHECK_EQ(kDiseaseEvents[8].countMod, 4);
    CHECK(kDiseaseEvents[1].costScale == 40.0f);
    CHECK(kDiseaseEvents[2].costScale == 15.0f);
    CHECK(kDiseaseEvents[3].costScale == 20.0f);
    CHECK(kDiseaseEvents[8].costScale == 3.0f);
    CHECK(kDiseaseEvents[9].costScale == 12.0f);
}

// --- Per-group "free" masks. ----------------------------------------------
TEST(SimIllness, GroupIsFree) {
    // Empty state: all 8 groups free.
    for (int b = 0; b < 8; ++b)
        CHECK(IllnessGroupIsFree(0u, b));
    // group 0 occupies bits 4..7.
    CHECK(!IllnessGroupIsFree(0x00000010u, 0));
    CHECK(IllnessGroupIsFree(0x00000010u, 1));
    // group 3 occupies bits 14..16 (mask 0x1C000).
    CHECK(!IllnessGroupIsFree(0x00004000u, 3));
    // group 7 occupies bits 25..28 (mask 0x1E000000).
    CHECK(!IllnessGroupIsFree(0x02000000u, 7));
    CHECK(IllnessGroupIsFree(0x02000000u, 0));
    // out-of-range bit -> not free.
    CHECK(!IllnessGroupIsFree(0u, -1));
    CHECK(!IllnessGroupIsFree(0u, 8));
}

// --- Pack arithmetic: clears the group sub-field, writes masked value. -----
TEST(SimIllness, PackGroup) {
    // group 2 (bits 4..7): write 0xA into a dirty word.
    CHECK_EQ(IllnessPackGroup(0x12345678u, 2, 0xAB), 0x123456B8u);
    // group 9 (bits 25..28): write 0xF into clear state.
    CHECK_EQ(IllnessPackGroup(0u, 9, 0xF), 0x1E000000u);
    // group 4 (bits 12..13): only 2 bits, value masked &3.
    CHECK_EQ(IllnessPackGroup(0u, 4, 0xFF), 0x00003000u);
    // group 5 (bits 14..16): 3 bits, value masked &7.
    CHECK_EQ(IllnessPackGroup(0u, 5, 0xFF), 0x0001C000u);
    // unknown group -> unchanged.
    CHECK_EQ(IllnessPackGroup(0xDEADBEEFu, 1, 5), 0xDEADBEEFu);
    // pack is idempotent on its own sub-field: re-packing the same value holds.
    u32 s = IllnessPackGroup(0u, 3, 0xC);
    CHECK_EQ(IllnessPackGroup(s, 3, 0xC), s);
}

// --- Disease-candidate predicate. -----------------------------------------
TEST(SimIllness, DiseaseCandidate) {
    IllnessRec rec{};
    rec.marker = 0;       // not free
    rec.active = 1;       // present
    rec.kind = 5;         // person (<10)

    IllnessTurnView owns{ /*owner*/ true, /*object*/ false };
    IllnessTurnView objs{ /*owner*/ false, /*object*/ true };
    IllnessTurnView none{ /*owner*/ false, /*object*/ false };

    CHECK(IllnessIsDiseaseCandidate(&rec, owns));
    CHECK(IllnessIsDiseaseCandidate(&rec, objs));
    CHECK(!IllnessIsDiseaseCandidate(&rec, none));

    // free slot -> never candidate.
    rec.marker = static_cast<i16>(0xFFFF);
    CHECK(!IllnessIsDiseaseCandidate(&rec, owns));
    rec.marker = 0;

    // absent actor (+8 == 0) -> never candidate.
    rec.active = 0;
    CHECK(!IllnessIsDiseaseCandidate(&rec, owns));
    rec.active = 1;

    // non-person (kind >= 10, signed) -> never candidate.
    rec.kind = 10;
    CHECK(!IllnessIsDiseaseCandidate(&rec, owns));
    rec.kind = 20;
    CHECK(!IllnessIsDiseaseCandidate(&rec, owns));
    // kind with high bit set is NEGATIVE as signed char -> still a person.
    rec.kind = 0x80;
    CHECK(IllnessIsDiseaseCandidate(&rec, owns));
}

// --- Golden-vector picks (fixed-seed LCG). --------------------------------
// Computed in Python against the recovered algorithm (see report).
TEST(SimIllness, PickGoldenSeed1) {
    crt::Srand(1);
    DiseasePick p = IllnessPickRandomDiseaseEvent(0u);
    CHECK(p.applied);
    CHECK_EQ(p.chosenBit, 6);
    CHECK_EQ(p.group, 8);
    CHECK_EQ(p.severity, 2);
    CHECK_EQ(p.cost, 6);
    CHECK_EQ(p.newState, 0x01000000u);   // 16777216
    CHECK_EQ(static_cast<int>(p.eventByte), 8);

    // Second pick continues the same LCG stream.
    DiseasePick p2 = IllnessPickRandomDiseaseEvent(0u);
    CHECK(p2.applied);
    CHECK_EQ(p2.chosenBit, 1);
    CHECK_EQ(p2.group, 3);
    CHECK_EQ(p2.severity, 11);
    CHECK_EQ(p2.cost, 220);
    CHECK_EQ(p2.newState, 2816u);
    CHECK_EQ(static_cast<int>(p2.eventByte), 3);
}

TEST(SimIllness, PickGoldenSeed12345) {
    crt::Srand(12345);
    // groups 0 (0xF50&0xF0=0x50) and 1 (0xF00&0xF=... ) partly occupied.
    DiseasePick p = IllnessPickRandomDiseaseEvent(0x00000F50u);
    CHECK(p.applied);
    CHECK_EQ(p.chosenBit, 4);
    CHECK_EQ(p.group, 6);
    CHECK_EQ(p.severity, 4);
    CHECK_EQ(p.cost, 32);
    CHECK_EQ(p.newState, 528208u);
    CHECK_EQ(static_cast<int>(p.eventByte), 6);
}

TEST(SimIllness, PickGoldenForcedGroup5) {
    crt::Srand(777);
    // Occupy every group except group 5 (bits 20..22) so the probe must land
    // on it regardless of the random start.
    u32 all = 0u;
    for (int b = 0; b < 8; ++b) {
        // pack max into each group, then clear group 5.
        int g = b + 2;
        all = IllnessPackGroup(all, g, 0xF);
    }
    all &= ~0x00700000u;  // free group 5 (bits 20..22)
    DiseasePick p = IllnessPickRandomDiseaseEvent(all);
    CHECK(p.applied);
    CHECK_EQ(p.chosenBit, 5);
    CHECK_EQ(p.group, 7);
    CHECK_EQ(p.severity, 2);
    CHECK_EQ(p.cost, 10);
    CHECK_EQ(static_cast<int>(p.eventByte), 7);
}

// --- All-occupied state yields no pick. -----------------------------------
TEST(SimIllness, NoFreeGroup) {
    crt::Srand(42);
    u32 all = 0u;
    for (int b = 0; b < 8; ++b)
        all = IllnessPackGroup(all, b + 2, 0xF);
    DiseasePick p = IllnessPickRandomDiseaseEvent(all);
    CHECK(!p.applied);
    CHECK_EQ(p.chosenBit, -1);
    CHECK_EQ(p.cost, 0);
    CHECK_EQ(p.newState, all);
}
