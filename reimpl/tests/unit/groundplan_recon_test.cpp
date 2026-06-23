// Golden-vector unit tests for the ground-plan pure layout/eligibility math.
// gilde.exe: VIBE_Groundplan_GetBuildingState (0x4af464),
//            VIBE_Groundplan_GetWappenLabelId (0x4ae59c),
//            VIBE_Groundplan_RetZero          (0x4ae824).
#include "tests/framework/test.h"
#include "world/groundplan_recon.h"

#include <cstdint>
#include <vector>

using namespace guild;
using guild::world::GroundplanHooks;

// --------------------------------------------------------------------------
// Test stand-in building-type tables. These mirror the *contract* of the real
// accessors (which live in src/sim and are wired in production); the values
// here are chosen to exercise every branch, not to mimic the real game table.
// --------------------------------------------------------------------------
namespace {

u8 g_lastTypeCode = 0xFF;

// MapTypeToCategory: byte -> category. Encode a few fixed mappings.
u8 TestMapTypeToCategory(u8 typeByte) {
    g_lastTypeCode = typeByte;
    switch (typeByte) {
        case 10: return 3;   // -> sets state global = 1
        case 20: return 5;   // -> consults MapTypeToState
        case 30: return 5;   // -> MapTypeToState miss
        case 40: return 2;   // -> plain category passthrough
        default: return 0;
    }
}

// MapTypeToState: only typeByte 20 resolves (to state 7); 30 misses.
int TestMapTypeToState(u8 typeByte, u8* outState) {
    if (typeByte == 20) { *outState = 7; return 1; }
    return 0;
}

// GroupFromCode: identity for our purposes (code already IS the group here).
u8 TestGroupIdentity(u8 code) { return code; }

}  // namespace

// ==========================================================================
// RetZero
// ==========================================================================
TEST(GroundplanReconRetZero, AlwaysZero) {
    CHECK_EQ(world::Groundplan_RetZero(), 0);
}

// ==========================================================================
// GetBuildingState — branch coverage (cat 3, cat 5 hit, cat 5 miss, plain)
// ==========================================================================
TEST(GroundplanReconState, Category3SetsStateOne) {
    u8 stateGlobal = 0;
    GroundplanHooks h;
    h.MapTypeToCategory = &TestMapTypeToCategory;
    h.MapTypeToState = &TestMapTypeToState;
    h.stateGlobal = &stateGlobal;

    u8 type = 10;
    u8 r = world::Groundplan_GetBuildingState(&type, h);
    CHECK_EQ((int)r, 3);          // returns the category unchanged
    CHECK_EQ((int)stateGlobal, 1);  // byte_6317B5 = 1
}

TEST(GroundplanReconState, Category5HitOverridesState) {
    u8 stateGlobal = 0;
    GroundplanHooks h;
    h.MapTypeToCategory = &TestMapTypeToCategory;
    h.MapTypeToState = &TestMapTypeToState;
    h.stateGlobal = &stateGlobal;

    u8 type = 20;
    u8 r = world::Groundplan_GetBuildingState(&type, h);
    CHECK_EQ((int)r, 7);            // return becomes the resolved state
    CHECK_EQ((int)stateGlobal, 7);  // byte_6317B5 = 7
}

TEST(GroundplanReconState, Category5MissKeepsCategory) {
    u8 stateGlobal = 99;
    GroundplanHooks h;
    h.MapTypeToCategory = &TestMapTypeToCategory;
    h.MapTypeToState = &TestMapTypeToState;
    h.stateGlobal = &stateGlobal;

    u8 type = 30;
    u8 r = world::Groundplan_GetBuildingState(&type, h);
    CHECK_EQ((int)r, 5);             // category unchanged on a state miss
    CHECK_EQ((int)stateGlobal, 99);  // global untouched
}

TEST(GroundplanReconState, PlainCategoryPassthrough) {
    u8 stateGlobal = 0;
    GroundplanHooks h;
    h.MapTypeToCategory = &TestMapTypeToCategory;
    h.MapTypeToState = &TestMapTypeToState;
    h.stateGlobal = &stateGlobal;

    u8 type = 40;
    u8 r = world::Groundplan_GetBuildingState(&type, h);
    CHECK_EQ((int)r, 2);
    CHECK_EQ((int)stateGlobal, 0);
}

TEST(GroundplanReconState, NullHooksInert) {
    GroundplanHooks h;  // all null
    u8 type = 10;
    u8 r = world::Groundplan_GetBuildingState(&type, h);
    CHECK_EQ((int)r, 0);  // MapTypeToCategory null -> cat 0, no crash
}

// ==========================================================================
// WappenLabelForGroup — full group->label golden table
// ==========================================================================
TEST(GroundplanReconWappen, GroupTableGolden) {
    // group {1,2,10} -> 1241
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(1), 1241);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(2), 1241);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(10), 1241);
    // {3,4} -> 1245
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(3), 1245);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(4), 1245);
    // {11,12,6} -> 1249
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(11), 1249);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(12), 1249);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(6), 1249);
    // {5,7,8,9} -> 1253
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(5), 1253);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(7), 1253);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(8), 1253);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(9), 1253);
    // default -> 1241
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(0), 1241);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(13), 1241);
    CHECK_EQ(world::Groundplan_WappenLabelForGroup(255), 1241);
}

// ==========================================================================
// GetWappenLabelId — forced-override cases (byte_12335B8 = 1..4, >=5)
// ==========================================================================
TEST(GroundplanReconWappen, ForcedOverrides) {
    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    std::vector<u8> markers(1, 6);
    std::vector<u32> words(200, 0);

    CHECK_EQ(world::Groundplan_GetWappenLabelId(1, markers.data(), 1, words.data(), h), 1241);
    CHECK_EQ(world::Groundplan_GetWappenLabelId(2, markers.data(), 1, words.data(), h), 1245);
    CHECK_EQ(world::Groundplan_GetWappenLabelId(3, markers.data(), 1, words.data(), h), 1249);
    CHECK_EQ(world::Groundplan_GetWappenLabelId(4, markers.data(), 1, words.data(), h), 1253);
    CHECK_EQ(world::Groundplan_GetWappenLabelId(5, markers.data(), 1, words.data(), h), 1241);
    CHECK_EQ(world::Groundplan_GetWappenLabelId(99, markers.data(), 1, words.data(), h), 1241);
}

// ==========================================================================
// GetWappenLabelId — case 0 grid walk.
// markers stride is 536 bytes; type words stride 134 dwords.
// ==========================================================================
TEST(GroundplanReconWappen, GridWalkFirstSlotSelected) {
    // Slot 0 marker == 6 -> v1 stays 0. Type word HIBYTE = group 3 -> 1245.
    const int slots = 4;
    std::vector<u8> markers(slots * 536, 0);
    markers[0] = 6;  // first slot selected
    std::vector<u32> words(static_cast<size_t>(slots) * 134, 0);
    words[0] = static_cast<u32>(3u << 24);  // HIBYTE = 3

    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), slots,
                                                words.data(), h), 1245);
}

TEST(GroundplanReconWappen, GridWalkSecondSlotSelected) {
    // Slot 0 not selected/terminator, slot 1 (byte offset 536) == 6 -> v1 = 1.
    const int slots = 4;
    std::vector<u8> markers(slots * 536, 0);
    markers[0] = 1;       // not 6, not 7 -> advance
    markers[536] = 6;     // slot 1 selected
    std::vector<u32> words(static_cast<size_t>(slots) * 134, 0);
    // type word of slot 1 is words[134*1]; HIBYTE = 7 -> group 7 -> 1253.
    words[134] = static_cast<u32>(7u << 24);

    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), slots,
                                                words.data(), h), 1253);
}

TEST(GroundplanReconWappen, GridWalkTerminatorStopsAtSlot) {
    // Slot 0 != 6, slot 1 marker == 7 (terminator) -> loop breaks with v1 = 1.
    const int slots = 4;
    std::vector<u8> markers(slots * 536, 0);
    markers[0] = 1;
    markers[536] = 7;     // terminator at slot 1
    std::vector<u32> words(static_cast<size_t>(slots) * 134, 0);
    words[134] = static_cast<u32>(11u << 24);  // group 11 -> 1249

    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), slots,
                                                words.data(), h), 1249);
}

// --------------------------------------------------------------------------
// HARDENING (wave-12): the case-0 grid walk falls through without selecting a
// slot (no marker == 6 and no terminator == 7). In the binary the markers and
// the type-word details are two columns of ONE 768-slot person array, so the
// fall-through v1 still indexes adjacent in-bounds BSS; here the caller passes
// two co-sized spans, so an unclamped typeWords[134*v1] read ran off the end
// (ASan SEGV at groundplan_recon.cpp:140). The guard now returns the v0 default.
// --------------------------------------------------------------------------
TEST(GroundplanReconWappen, GridWalkFallthroughNoSelectionNoOob) {
    const int slots = 2;
    std::vector<u8> markers(slots * 536, 1);  // never 6 / 7 -> walk runs off the end
    std::vector<u32> words(static_cast<size_t>(slots) * 134, 0);
    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    // No slot selected -> code 0 -> group 0 -> v0 default 1241 (no OOB read).
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), slots,
                                                words.data(), h), 1241);
}

TEST(GroundplanReconWappen, GridWalkEmptySpanNoOob) {
    // Zero-slot spans: walk cannot select anything; must not index typeWords.
    GroundplanHooks h;
    h.GroupFromCode = &TestGroupIdentity;
    std::vector<u8> markers;   // empty
    std::vector<u32> words;    // empty
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), 0,
                                                words.data(), h), 1241);
}

TEST(GroundplanReconWappen, GridWalkNullGroupHookDefaults) {
    const int slots = 2;
    std::vector<u8> markers(slots * 536, 0);
    markers[0] = 6;
    std::vector<u32> words(static_cast<size_t>(slots) * 134, 0);
    words[0] = static_cast<u32>(5u << 24);

    GroundplanHooks h;  // GroupFromCode null -> group 0 -> 1241
    CHECK_EQ(world::Groundplan_GetWappenLabelId(0, markers.data(), slots,
                                                words.data(), h), 1241);
}
