// Unit tests for the world events/missions/history/family-tree/statistics core:
//   event descriptor lookup by id/category, seeded LCG random pick (python
//   golden), mission slot register/find + requirement evaluation, history date
//   parse + chronological classify + notify gate, family-tree parent/child/
//   sibling queries, and statistics accumulation totals.
#include "tests/framework/test.h"

#include <cstring>

#include "world/event.h"
#include "world/mission.h"
#include "world/history.h"
#include "world/stammbaum.h"
#include "world/statistics.h"

using namespace guild;
using namespace guild::world;

// Builds a synthetic descriptor table: `n` entries, entry i has value (i+10) and
// category cats[i]. (A synthetic table makes the per-category counts deterministic
// for the lookup tests; the shipped default image is exercised separately in
// DefaultImageLayout, where the +5 categories are the 0..5 book grouping.)
static void BuildSyntheticTable(const u8* cats, int n) {
    EventTableReset();
    for (int i = 0; i < n; ++i) {
        g_eventTable[i].word0    = i + 1;
        g_eventTable[i].value    = static_cast<u8>(i + 10);
        g_eventTable[i].category = cats[i];
        g_eventTable[i].paramA   = i * 100;
    }
    g_eventTableCount = n;
}

// ---------------------------------------------------------------------------
// Static descriptor image: byte-exact recovery (golden from get_bytes @0x63CD48).
// ---------------------------------------------------------------------------
TEST(WorldEvents, DefaultImageLayout) {
    EventTableLoadDefault();
    CHECK_EQ(g_eventTableCount, 48);

    // Row 0 is the free sentinel (category 0xFF, value 0).
    CHECK_EQ((int)g_eventTable[0].category, 0xFF);
    CHECK_EQ((int)g_eventTable[0].value, 0);

    // Populated rows: value at +4 increments 1,2,3,..; category at +5 is the
    // 0..5 book grouping (rows 1..4 -> cat 0, then 1,1,..,5).
    CHECK_EQ((int)g_eventTable[1].value, 1);       // byte_63CD4C[24*1]
    CHECK_EQ((int)g_eventTable[1].category, 0);
    CHECK_EQ((int)g_eventTable[5].category, 1);
    CHECK_EQ((int)g_eventTable[13].category, 2);
    CHECK_EQ((int)g_eventTable[21].category, 3);
    CHECK_EQ((int)g_eventTable[41].category, 5);

    // The default image is directly usable: it has populated entries in each of
    // categories 0..5, so CountByCategory is non-zero there.
    CHECK(EventTableCountByCategory(0) > 0);
    CHECK(EventTableCountByCategory(5) > 0);
}

// ---------------------------------------------------------------------------
// CountByCategory + FindByCategory.
// ---------------------------------------------------------------------------
TEST(WorldEvents, CountAndFindByCategory) {
    const u8 cats[6] = {1, 2, 1, 3, 1, 2};
    BuildSyntheticTable(cats, 6);

    CHECK_EQ(EventTableCountByCategory(1), 3);
    CHECK_EQ(EventTableCountByCategory(2), 2);
    CHECK_EQ(EventTableCountByCategory(3), 1);
    CHECK_EQ(EventTableCountByCategory(0), 0);
    CHECK_EQ(EventTableCountByCategory(6), -1);   // category > 5 -> -1

    // First entry of each category (by table index).
    CHECK_EQ(EventTableFindByCategory(1), 0);
    CHECK_EQ(EventTableFindByCategory(2), 1);
    CHECK_EQ(EventTableFindByCategory(3), 3);
    CHECK_EQ(EventTableFindByCategory(4), -1);
}

// ---------------------------------------------------------------------------
// Seeded LCG random pick — golden values computed with python.
//   state(1) -> 1103515245*1+12345 = 0x41C67EA6, HIWORD=16838,
//   16838 % 0x7FFF = 16838.  For a 3-category pool: 16838 % 3 == 2 ->
//   the 3rd matching entry. For a 5-pool: 16838 % 5 == 3 -> 4th matching entry.
// ---------------------------------------------------------------------------
TEST(WorldEvents, SeededRandomPickGolden) {
    // Three category-1 entries at table indices 0,2,4 with values 10,12,14.
    const u8 cats[6] = {1, 2, 1, 3, 1, 2};
    BuildSyntheticTable(cats, 6);

    g_missionLcgState = 1;                  // seed the dedicated mission LCG
    // count=3, pick = 16838 % 3 = 2 -> the (pick+1)=3rd matching entry = index 4,
    // value 14.
    CHECK_EQ(EventPickRandomByCategory(1), 14);

    // No entries of category 4 -> -1, and the LCG is NOT advanced (the count<=0
    // guard returns before the advance).
    u32 before = g_missionLcgState;
    CHECK_EQ(EventPickRandomByCategory(4), -1);
    CHECK_EQ((int)g_missionLcgState, (int)before);

    // Five-entry pool, all category 2: pick = 16838 % 5 = 3 -> 4th entry.
    const u8 cats2[5] = {2, 2, 2, 2, 2};
    BuildSyntheticTable(cats2, 5);
    g_missionLcgState = 1;
    // values are 10,11,12,13,14; 4th matching entry (index 3) -> value 13.
    CHECK_EQ(EventPickRandomByCategory(2), 13);
}

// ---------------------------------------------------------------------------
// Mission slot register / find / single-slot mode.
// ---------------------------------------------------------------------------
TEST(WorldMission, SlotRegisterAndFind) {
    MissionSlotTableReset();

    int s0 = MissionSlotRegister(/*owner*/500, /*type*/7);
    CHECK_EQ(s0, 0);
    CHECK_EQ((int)g_missionSlots[0].type, 7);
    CHECK_EQ(g_missionSlots[0].owner, 500);
    CHECK_EQ(g_missionSlots[0].fieldAt28, 0);

    int s1 = MissionSlotRegister(/*owner*/501, /*type*/9);
    CHECK_EQ(s1, 1);                        // first slot occupied -> next free

    CHECK_EQ(MissionFindBySource(500), 0);
    CHECK_EQ(MissionFindBySource(501), 1);
    CHECK_EQ(MissionFindBySource(999), -1);

    // Single-slot mode (byte_63C8F4 == 5): always rewrites slot 0.
    g_missionSlotMode = 5;
    int sm = MissionSlotRegister(/*owner*/777, /*type*/3);
    CHECK_EQ(sm, 0);
    CHECK_EQ((int)g_missionSlots[0].type, 3);
    CHECK_EQ(g_missionSlots[0].owner, 777);
    g_missionSlotMode = 0;
}

TEST(WorldMission, PickAndRegisterRandom) {
    const u8 cats[6] = {1, 2, 1, 3, 1, 2};
    BuildSyntheticTable(cats, 6);
    MissionSlotTableReset();

    g_missionLcgState = 1;                  // pick category-1 -> value 14
    int slot = MissionPickAndRegisterRandom(/*owner*/1234, /*category*/1);
    CHECK_EQ(slot, 0);
    CHECK_EQ((int)g_missionSlots[0].type, 14);
    CHECK_EQ(g_missionSlots[0].owner, 1234);
}

// ---------------------------------------------------------------------------
// Mission requirement evaluation (type-gate + progress advance).
// ---------------------------------------------------------------------------
TEST(WorldMission, RequirementTrackable) {
    // Trackable set is exactly {11, 19, 23, 28, 40}.
    CHECK(MissionTypeIsTrackable(11));
    CHECK(MissionTypeIsTrackable(19));
    CHECK(MissionTypeIsTrackable(23));
    CHECK(MissionTypeIsTrackable(28));
    CHECK(MissionTypeIsTrackable(40));
    CHECK(!MissionTypeIsTrackable(10));
    CHECK(!MissionTypeIsTrackable(12));
    CHECK(!MissionTypeIsTrackable(20));
    CHECK(!MissionTypeIsTrackable(24));
    CHECK(!MissionTypeIsTrackable(29));
    CHECK(!MissionTypeIsTrackable(41));
}

TEST(WorldMission, RequirementAdvance) {
    MissionSlotTableReset();
    MissionSlotRegister(/*owner*/100, /*type*/23);  // slot 0, trackable type 23

    // Matching trackable crime advances the slot's progress (fieldAt28).
    CHECK_EQ(g_missionSlots[0].fieldAt28, 0);
    CHECK(MissionRequirementAdvance(0, 23));
    CHECK_EQ(g_missionSlots[0].fieldAt28, 1);
    CHECK(MissionRequirementAdvance(0, 23));
    CHECK_EQ(g_missionSlots[0].fieldAt28, 2);

    // A trackable but NON-matching crime type still returns true (the original
    // unconditionally does mov eax,1 once the slot is found + type is trackable),
    // but it does NOT bump the progress counter (only slot.type == crimeType does).
    CHECK(MissionRequirementAdvance(0, 11));   // 11 trackable, != slot type 23
    CHECK_EQ(g_missionSlots[0].fieldAt28, 2);  // unchanged

    // Untrackable type returns false (the type-gate's xor eax,eax) even if it
    // equals the slot type.
    MissionSlotTableReset();
    MissionSlotRegister(/*owner*/100, /*type*/12);   // type 12 is untrackable
    CHECK(!MissionRequirementAdvance(0, 12));
    CHECK_EQ(g_missionSlots[0].fieldAt28, 0);

    // Empty slot (FindBySource miss) returns false.
    MissionSlotTableReset();
    CHECK(!MissionRequirementAdvance(0, 23));
}

// ---------------------------------------------------------------------------
// History: dated-text parse + chronological classify + notify gate.
// ---------------------------------------------------------------------------
TEST(WorldHistory, ParseDate) {
    ParsedDate d;
    CHECK(HistoryParseDate("15.06.1453", &d));
    CHECK_EQ(d.day, 15);
    CHECK_EQ(d.month, 6);
    CHECK_EQ(d.year, 1453);
    CHECK_EQ(d.yearOffset, 53);            // 1453 - 1400
    CHECK_EQ(d.mode, 0);

    // Zero day normalises to 1, mode 1.
    CHECK(HistoryParseDate("00.06.1400", &d));
    CHECK_EQ(d.day, 1);
    CHECK_EQ(d.mode, 1);
    CHECK_EQ(d.yearOffset, 0);

    // Zero month normalises to 1, mode 2.
    CHECK(HistoryParseDate("12.00.1410", &d));
    CHECK_EQ(d.month, 1);
    CHECK_EQ(d.mode, 2);

    // Wrong length is rejected (the original requires exactly 10 chars).
    CHECK(!HistoryParseDate("1.6.1400", &d));
    CHECK(!HistoryParseDate("15.06.14530", &d));
    CHECK(!HistoryParseDate(nullptr, &d));
}

TEST(WorldHistory, ChronologicalClassify) {
    // currentDay - entryDay == 1 -> emit; < 1 -> stop; > 1 -> continue.
    CHECK_EQ((int)HistoryClassifyEntry(100, 99), (int)HistoryScanResult::kEmit);
    CHECK_EQ((int)HistoryClassifyEntry(100, 100), (int)HistoryScanResult::kStop);
    CHECK_EQ((int)HistoryClassifyEntry(100, 101), (int)HistoryScanResult::kStop);
    CHECK_EQ((int)HistoryClassifyEntry(100, 98), (int)HistoryScanResult::kContinue);
    CHECK_EQ((int)HistoryClassifyEntry(100, 1),  (int)HistoryScanResult::kContinue);
}

TEST(WorldHistory, NotifyGate) {
    CHECK(HistoryNotifyKindIsImportant(6));
    CHECK(HistoryNotifyKindIsImportant(7));
    CHECK(!HistoryNotifyKindIsImportant(5));
    CHECK(!HistoryNotifyKindIsImportant(0));

    // Arrest text-id selection.
    CHECK_EQ(HistoryNotifyArrestTextId(6, /*detained*/false, 50), 7313);
    CHECK_EQ(HistoryNotifyArrestTextId(6, /*detained*/true, 50), 7314);   // day < 117
    CHECK_EQ(HistoryNotifyArrestTextId(6, /*detained*/true, 200), 7315);  // day >= 117
    CHECK_EQ(HistoryNotifyArrestTextId(7, true, 117), 7315);              // boundary
    CHECK_EQ(HistoryNotifyArrestTextId(3, false, 50), -1);               // gate fails
}

// ---------------------------------------------------------------------------
// Family-tree parent / child / sibling / spouse queries.
// ---------------------------------------------------------------------------
TEST(WorldStammbaum, FamilyQueries) {
    // Dynasty:  father F(1) + mother M(2) -> children A(10), B(11); A married to W(20).
    FamilyRecord recs[5];
    std::memset(recs, 0, sizeof(recs));
    for (auto& r : recs) {
        r.father = kFamilyNone; r.mother = kFamilyNone; r.spouse = kFamilyNone;
        for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    }
    // F
    recs[0].id = 1;  recs[0].children[0] = 10; recs[0].children[1] = 11; recs[0].spouse = 2;
    // M
    recs[1].id = 2;  recs[1].children[0] = 10; recs[1].children[1] = 11; recs[1].spouse = 1;
    // A (child)
    recs[2].id = 10; recs[2].father = 1; recs[2].mother = 2; recs[2].spouse = 20;
    // B (child)
    recs[3].id = 11; recs[3].father = 1; recs[3].mother = 2;
    // W (A's wife)
    recs[4].id = 20; recs[4].spouse = 10;

    FamilyTree tree{recs, 5};

    // Parent / spouse links.
    CHECK_EQ(FamilyGetFather(recs[2]), 1);
    CHECK_EQ(FamilyGetMother(recs[2]), 2);
    CHECK_EQ(FamilyGetSpouse(recs[2]), 20);
    CHECK_EQ(FamilyGetFather(recs[0]), kFamilyNone);

    // Children of F.
    i32 kids[kMaxChildren];
    int nk = FamilyGetChildren(recs[0], kids, kMaxChildren);
    CHECK_EQ(nk, 2);
    CHECK_EQ(kids[0], 10);
    CHECK_EQ(kids[1], 11);

    // Parent-of relation.
    CHECK(FamilyIsParentOf(tree, 1, 10));
    CHECK(FamilyIsParentOf(tree, 2, 11));
    CHECK(!FamilyIsParentOf(tree, 1, 20));   // W is not F's child
    CHECK(!FamilyIsParentOf(tree, kFamilyNone, 10));

    // Siblings of A(10): B(11) shares both parents; W(20) does not.
    i32 sibs[8];
    int ns = FamilyGetSiblings(tree, 10, sibs, 8);
    CHECK_EQ(ns, 1);
    CHECK_EQ(sibs[0], 11);

    // Siblings of W(20): none.
    CHECK_EQ(FamilyGetSiblings(tree, 20, sibs, 8), 0);
}

// ---------------------------------------------------------------------------
// Statistics: economy accumulator totals.
// ---------------------------------------------------------------------------
TEST(WorldStatistics, AccumulateCategoryTotals) {
    float a[kStatAccumCount];
    for (int i = 0; i < kStatAccumCount; ++i)
        a[i] = static_cast<float>(i + 1);   // 1..20

    // total[k] = (a[k]+a[k+5]+a[k+10]+a[k+15]) * 0.25
    //   k=0: (1+6+11+16)=34 -> 8.5
    //   k=1: (2+7+12+17)=38 -> 9.5
    //   k=2: (3+8+13+18)=42 -> 10.5
    //   k=3: (4+9+14+19)=46 -> 11.5
    float out[kStatCategoryCnt];
    StatisticsAccumulateCategoryTotals(a, out);
    CHECK(out[0] == 8.5f);
    CHECK(out[1] == 9.5f);
    CHECK(out[2] == 10.5f);
    CHECK(out[3] == 11.5f);

    CHECK(StatisticsCategoryTotal(a, 0) == 8.5f);
    CHECK(StatisticsCategoryTotal(a, 3) == 11.5f);

    // All-zero accumulators -> zero totals.
    float z[kStatAccumCount] = {0};
    StatisticsAccumulateCategoryTotals(z, out);
    CHECK(out[0] == 0.0f);
    CHECK(out[3] == 0.0f);
}
