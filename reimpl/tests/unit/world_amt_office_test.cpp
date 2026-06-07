// Unit tests for the Amt office slot-table search primitives, the Level-1/Level-3
// guild-eligibility predicates, and the History second-pass bracket collapser.
#include "test.h"

#include <cstring>

#include "world/amt_slot_table.h"
#include "world/guild_rank.h"
#include "world/history_second_pass.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Slot-table helpers.
// ---------------------------------------------------------------------------
namespace {
AmtSlot MakeSlot(u8 x, u8 y, bool occupied, i32 objectId = 0, i32 key = 0,
                 u8 size = 0) {
    AmtSlot s;
    std::memset(&s, 0, sizeof(s));
    s.x = x;
    s.y = y;
    s.size = size;
    s.marker = occupied ? 0x00 : kAmtSlotFreeHi; // 0xFF == free
    s.objectId = objectId;
    std::memcpy(&s, &key, sizeof(key)); // write the +0 dword key (clobbers pad)
    // re-apply fields that the key write may have hit (key only touches pad0[0..3]).
    return s;
}

void FillEmpty(AmtSlot* slots) {
    for (int i = 0; i < kAmtSlotCount; ++i)
        slots[i] = MakeSlot(0, 0, /*occupied=*/false);
}
} // namespace

TEST(world_amt_office, OfficeTypeMatchesBookCat) {
    // dword_62EC8E record bytes (office.cpp): bookCat for ranks 1..3 == 1,2,3.
    CHECK_EQ(AmtGetOfficeType(1), (u8)1);
    CHECK_EQ(AmtGetOfficeType(2), (u8)2);
    CHECK_EQ(AmtGetOfficeType(3), (u8)3);
    CHECK_EQ(AmtGetOfficeType(0), (u8)0);   // record 0 is all-zero
    CHECK_EQ(AmtGetOfficeType(0x25), (u8)0); // out of range -> 0
    CHECK_EQ(AmtGetOfficeType(0xFF), (u8)0);
}

TEST(world_amt_office, SlotOccupiedTest) {
    AmtSlot occ = MakeSlot(1, 2, true);
    AmtSlot free = MakeSlot(1, 2, false);
    CHECK(AmtSlotOccupied(occ));
    CHECK(!AmtSlotOccupied(free));
    CHECK_EQ(free.marker, kAmtSlotFreeHi);
}

TEST(world_amt_office, FindSlotByCoord) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);
    slots[10] = MakeSlot(7, 9, /*occupied=*/true);
    slots[20] = MakeSlot(7, 9, /*occupied=*/false); // free duplicate ignored
    CHECK_EQ(AmtFindSlotByCoord(slots, 7, 9), 10);
    CHECK_EQ(AmtFindSlotByCoord(slots, 8, 9), -1);
    // A free slot at the coord must not match.
    FillEmpty(slots);
    slots[5] = MakeSlot(3, 3, /*occupied=*/false);
    CHECK_EQ(AmtFindSlotByCoord(slots, 3, 3), -1);
}

TEST(world_amt_office, FindSlotByObjectId) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);
    slots[0] = MakeSlot(0, 0, true, /*objectId=*/0xCAFE);
    slots[1] = MakeSlot(0, 0, false, /*objectId=*/0xBEEF); // free -> skipped
    CHECK_EQ(AmtFindSlotByObjectId(slots, 0xCAFE), 0);
    CHECK_EQ(AmtFindSlotByObjectId(slots, 0xBEEF), -1);
    CHECK_EQ(AmtFindSlotByObjectId(slots, 0x1234), -1);
}

TEST(world_amt_office, FindRecordByKey) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);
    AmtSlot s = MakeSlot(0, 0, true);
    i32 key = 0x44332211;
    std::memcpy(&s, &key, sizeof(key));
    s.marker = 0x00; // keep occupied after key write
    slots[7] = s;
    CHECK_EQ(AmtSlotKey(slots[7]), key);
    CHECK_EQ(AmtFindRecordByKey(slots, key), 7);
    CHECK_EQ(AmtFindRecordByKey(slots, 0x55667788), -1);
}

TEST(world_amt_office, FindSlotAtPoint) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);
    // occupied slot at (10,10) with size 4 -> half-extent 2 -> box [8..12]x[8..12]
    slots[3] = MakeSlot(10, 10, /*occupied=*/true, 0, 0, /*size=*/4);
    CHECK_EQ(AmtFindSlotAtPoint(slots, 10, 10), 3); // centre
    CHECK_EQ(AmtFindSlotAtPoint(slots, 8, 8), 3);   // corner inside
    CHECK_EQ(AmtFindSlotAtPoint(slots, 12, 12), 3); // far corner inside
    CHECK_EQ(AmtFindSlotAtPoint(slots, 13, 10), -1); // outside X
    CHECK_EQ(AmtFindSlotAtPoint(slots, 10, 7), -1);  // outside Y
}

TEST(world_amt_office, FindFreePlacementBlockedAtAnchor) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);
    slots[0] = MakeSlot(5, 5, /*occupied=*/true); // occupied exactly at anchor
    CHECK_EQ(AmtFindFreePlacement(slots, 5, 5, 4), 0); // blocked
}

TEST(world_amt_office, FindFreePlacementNoFreeSlot) {
    AmtSlot slots[kAmtSlotCount];
    for (int i = 0; i < kAmtSlotCount; ++i)
        slots[i] = MakeSlot((u8)(i + 1), (u8)(i + 1), /*occupied=*/true);
    // All occupied, none at (200,200) -> no free slot -> 0.
    CHECK_EQ(AmtFindFreePlacement(slots, 200, 200, 4), 0);
}

TEST(world_amt_office, FindFreePlacementZeroFootprint) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);                 // all free -> anyFree true
    CHECK_EQ(AmtFindFreePlacement(slots, 5, 5, 0), 1); // size<=0 -> 1
}

TEST(world_amt_office, FindFreePlacementSweepFindsPoint) {
    AmtSlot slots[kAmtSlotCount];
    FillEmpty(slots);                 // all free -> sweep finds a miss immediately
    CHECK_EQ(AmtFindFreePlacement(slots, 20, 20, 4), 1);
}

TEST(world_amt_office, FindOfficeTypeRecord) {
    AmtTypeRecord recs[4] = {
        {10, 1}, {20, 1}, {30, 1}, {40, 0}, // last has nextPresent==0 (terminator)
    };
    CHECK_EQ(AmtFindOfficeTypeRecord(recs, 4, 10), 0);
    CHECK_EQ(AmtFindOfficeTypeRecord(recs, 4, 30), 2);
    CHECK_EQ(AmtFindOfficeTypeRecord(recs, 4, 40), 3);
    CHECK_EQ(AmtFindOfficeTypeRecord(recs, 4, 99), -1);
    // Terminator stops the scan early: record 1 marks end (nextPresent 0).
    AmtTypeRecord recs2[3] = {{10, 1}, {20, 0}, {30, 1}};
    CHECK_EQ(AmtFindOfficeTypeRecord(recs2, 3, 30), -1); // unreachable past term
    CHECK_EQ(AmtFindOfficeTypeRecord(recs2, 3, 20), 1);
    CHECK_EQ(AmtFindOfficeTypeRecord(nullptr, 0, 1), -1);
}

// ---------------------------------------------------------------------------
// Guild rank eligibility (Level 1 / Level 3).
// ---------------------------------------------------------------------------
namespace {
bool MasterAlways(int /*cat*/, void* /*ctx*/) { return true; }
bool MasterNever(int /*cat*/, void* /*ctx*/) { return false; }
} // namespace

TEST(world_amt_office, Level1Eligibility) {
    // rank gate: 30..33 only.
    GuildPlayer p{};
    p.rank = 29; p.flags459 = 0; p.money = 100;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kNotGuild);
    p.rank = 34;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kNotGuild);
    // already in guild (flag bit 4).
    p.rank = 30; p.flags459 = 0x4; p.money = 100;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kAlreadyInGuild);
    // eligible (money >= 3).
    p.flags459 = 0; p.money = 3;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kEligible);
    // insufficient (money < 3).
    p.money = 2;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kInsufficientFunds);
    // bit 8 (Level2's flag) must NOT trigger Level1's already-in-guild.
    p.flags459 = 0x8; p.money = 3;
    CHECK(GuildGetEligibility(p) == GuildEligibility::kEligible);
}

TEST(world_amt_office, Level3Eligibility) {
    GuildPlayer p{};
    p.rank = 31; p.flags459 = 0; p.money = 100;
    // No master present -> NotGuild.
    CHECK(GuildCheckRankLevel3(p, MasterNever, nullptr) == GuildEligibility::kNotGuild);
    CHECK(GuildCheckRankLevel3(p, nullptr, nullptr) == GuildEligibility::kNotGuild);
    // Master present, flag bit 0x10 set -> already in guild.
    p.flags459 = 0x10;
    CHECK(GuildCheckRankLevel3(p, MasterAlways, nullptr) == GuildEligibility::kAlreadyInGuild);
    // Master present, money >= 8 -> eligible.
    p.flags459 = 0; p.money = 8;
    CHECK(GuildCheckRankLevel3(p, MasterAlways, nullptr) == GuildEligibility::kEligible);
    // money < 8 -> insufficient.
    p.money = 7;
    CHECK(GuildCheckRankLevel3(p, MasterAlways, nullptr) == GuildEligibility::kInsufficientFunds);
    // Level3 uses bit 0x10, not 0x8: bit 8 alone -> eligible (not already-in-guild).
    p.flags459 = 0x8; p.money = 8;
    CHECK(GuildCheckRankLevel3(p, MasterAlways, nullptr) == GuildEligibility::kEligible);
    // rank gate.
    p.rank = 29;
    CHECK(GuildCheckRankLevel3(p, MasterAlways, nullptr) == GuildEligibility::kNotGuild);
}

// ---------------------------------------------------------------------------
// History second-pass bracket collapse + orchestrator.
// ---------------------------------------------------------------------------
TEST(world_amt_office, CollapseNoRegion) {
    char out[64];
    CHECK(HistoryCollapseLabel("Plain chronicle text", out) ==
          HistoryCollapseResult::kRendered);
    CHECK(std::strcmp(out, "Plain chronicle text") == 0);
}

TEST(world_amt_office, CollapseSingleRegion) {
    char out[64];
    // "X[AB#CD]EF" -> keep "AB", drop "CD" -> "XABEF".
    CHECK(HistoryCollapseLabel("X[AB#CD]EF", out) ==
          HistoryCollapseResult::kRendered);
    CHECK(std::strcmp(out, "XABEF") == 0);
}

TEST(world_amt_office, CollapseRegionAtStart) {
    char out[64];
    // "[keep#drop]tail" -> "keeptail".
    CHECK(HistoryCollapseLabel("[keep#drop]tail", out) ==
          HistoryCollapseResult::kRendered);
    CHECK(std::strcmp(out, "keeptail") == 0);
}

TEST(world_amt_office, CollapseEmptyKeep) {
    char out[64];
    // "[#drop]Y" -> keep "" -> "Y".
    CHECK(HistoryCollapseLabel("[#drop]Y", out) ==
          HistoryCollapseResult::kRendered);
    CHECK(std::strcmp(out, "Y") == 0);
}

TEST(world_amt_office, CollapseTwoRegions) {
    char out[64];
    // "[A#x]-[B#y]" -> "A-B".
    CHECK(HistoryCollapseLabel("[A#x]-[B#y]", out) ==
          HistoryCollapseResult::kRendered);
    CHECK(std::strcmp(out, "A-B") == 0);
}

TEST(world_amt_office, CollapseSyntaxErrors) {
    char out[64];
    // '#' with no open region.
    CHECK(HistoryCollapseLabel("ab#cd", out) == HistoryCollapseResult::kSyntaxErr);
    // ']' with no open region.
    CHECK(HistoryCollapseLabel("ab]cd", out) == HistoryCollapseResult::kSyntaxErr);
    // nested '['.
    CHECK(HistoryCollapseLabel("[a[b#c]", out) == HistoryCollapseResult::kSyntaxErr);
    // duplicate '#'.
    CHECK(HistoryCollapseLabel("[a#b#c]", out) == HistoryCollapseResult::kSyntaxErr);
    // ']' before '#'.
    CHECK(HistoryCollapseLabel("[ab]", out) == HistoryCollapseResult::kSyntaxErr);
    // unterminated region.
    CHECK(HistoryCollapseLabel("[a#b", out) == HistoryCollapseResult::kSyntaxErr);
}

TEST(world_amt_office, CollapseNullAndEmpty) {
    char out[16];
    CHECK(HistoryCollapseLabel(nullptr, out) == HistoryCollapseResult::kRendered);
    CHECK_EQ(out[0], '\0');
    CHECK(HistoryCollapseLabel("", out) == HistoryCollapseResult::kRendered);
    CHECK_EQ(out[0], '\0');
}

TEST(world_amt_office, LabelPassesGate) {
    bool ranCmd = false;
    // First fails -> 0, no commandline.
    CHECK_EQ(HistoryRunLabelPasses(false, true, true, &ranCmd), 0);
    CHECK(!ranCmd);
    // Second fails -> 0.
    CHECK_EQ(HistoryRunLabelPasses(true, false, true, &ranCmd), 0);
    CHECK(!ranCmd);
    // Both pass, no commandline flag -> 1, no commandline.
    CHECK_EQ(HistoryRunLabelPasses(true, true, false, &ranCmd), 1);
    CHECK(!ranCmd);
    // Both pass + flag -> 1, commandline fires.
    CHECK_EQ(HistoryRunLabelPasses(true, true, true, &ranCmd), 1);
    CHECK(ranCmd);
}
