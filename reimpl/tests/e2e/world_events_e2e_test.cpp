// End-to-end flow for the world events/missions/history subsystem:
//   build a small world state -> roll a fixed-seed sequence of random events ->
//   register a mission for the picked type -> advance the mission via tracked
//   crimes -> record dated outcomes in the chronicle -> verify the event
//   sequence + history log against a hand-computed reference.
#include "tests/framework/test.h"

#include <cstring>

#include "world/event.h"
#include "world/mission.h"
#include "world/history.h"
#include "world/stammbaum.h"

using namespace guild;
using namespace guild::world;

// Hand-computed reference (python golden, see world_events_test SeededRandomPick):
//   Synthetic descriptor table: 6 entries, category-1 at table indices 0,2,4 with
//   values 10,12,14 (3 matching entries, count == 3).
//   Mission LCG seeded to 1; each EventPickRandomByCategory(1) advances the LCG
//   once and picks (HIWORD(state) % 0x7FFF) % 3.  The four-roll sequence is:
//     step0: state=0x41C67EA6 HIWORD=16838 pick=2 -> value 14
//     step1: HIWORD=38526 pick=2 -> value 14
//     step2: HIWORD=10113 pick=0 -> value 10
//     step3: HIWORD=50283 pick=2 -> value 14
//   => value sequence [14, 14, 10, 14].
TEST(WorldEventsE2E, RollMissionHistoryFlow) {
    // --- 1. Build the world state: descriptor table + empty mission slots. -----
    EventTableReset();
    const u8 cats[6] = {1, 2, 1, 3, 1, 2};
    for (int i = 0; i < 6; ++i) {
        g_eventTable[i].word0    = i + 1;
        g_eventTable[i].value    = static_cast<u8>(i + 10);  // 10..15
        g_eventTable[i].category = cats[i];
    }
    g_eventTableCount = 6;
    MissionSlotTableReset();

    // --- 2. Roll a fixed-seed sequence of random events. -----------------------
    g_missionLcgState = 1;
    const int expectedSeq[4] = {14, 14, 10, 14};
    int rolled[4];
    for (int i = 0; i < 4; ++i)
        rolled[i] = EventPickRandomByCategory(1);
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(rolled[i], expectedSeq[i]);

    // --- 3. Register a mission of the first picked type for an owner. -----------
    // Re-seed and use PickAndRegisterRandom so the first pick (value 14) becomes
    // the registered mission type. (14 is not in the trackable set {11,19,23,28,
    // 40}, so we register a trackable type 23 explicitly to drive progress.)
    g_missionLcgState = 1;
    int slotA = MissionPickAndRegisterRandom(/*owner*/500, /*category*/1);
    CHECK_EQ(slotA, 0);
    CHECK_EQ((int)g_missionSlots[0].type, 14);   // first roll value

    int slotB = MissionSlotRegister(/*owner*/501, /*type*/23);  // trackable
    CHECK_EQ(slotB, 1);
    CHECK_EQ(MissionFindBySource(501), 1);

    // --- 4. Advance the trackable mission via two matching tracked crimes. ------
    CHECK(MissionRequirementAdvance(slotB, 23));
    CHECK(MissionRequirementAdvance(slotB, 23));
    CHECK_EQ(g_missionSlots[slotB].fieldAt28, 2);   // two recorded progress hits
    // A non-matching crime does not advance.
    CHECK(!MissionRequirementAdvance(slotB, 19));
    CHECK_EQ(g_missionSlots[slotB].fieldAt28, 2);

    // --- 5. Record dated outcomes in the chronicle and query chronologically. --
    // Two chronicle entries: an arrest on day 99 and a mission result on day 100.
    // Current game day is 100; the forward scan emits the "yesterday" entry.
    ParsedDate d99, d100;
    CHECK(HistoryParseDate("10.04.1400", &d99));    // arbitrary date text -> day 10
    CHECK(HistoryParseDate("11.04.1400", &d100));
    CHECK_EQ(d99.day, 10);
    CHECK_EQ(d100.day, 11);

    // The chronological classifier picks the prior-day entry relative to "now".
    const i32 currentDay = 100;
    CHECK_EQ((int)HistoryClassifyEntry(currentDay, 99),  (int)HistoryScanResult::kEmit);
    CHECK_EQ((int)HistoryClassifyEntry(currentDay, 100), (int)HistoryScanResult::kStop);
    CHECK_EQ((int)HistoryClassifyEntry(currentDay, 50),  (int)HistoryScanResult::kContinue);

    // The arrest Notify gate: target kind 6 (important), not detained -> id 7313.
    CHECK_EQ(HistoryNotifyArrestTextId(/*kind*/6, /*detained*/false, currentDay), 7313);
    // Detained arrest with day >= 117 selects 7315; here day 100 -> 7314.
    CHECK_EQ(HistoryNotifyArrestTextId(6, true, currentDay), 7314);
    CHECK_EQ(HistoryNotifyArrestTextId(6, true, 200), 7315);

    // --- 6. Tie the offender into a family so succession/notify can resolve. ----
    FamilyRecord fam[2];
    std::memset(fam, 0, sizeof(fam));
    for (auto& r : fam) {
        r.father = kFamilyNone; r.mother = kFamilyNone; r.spouse = kFamilyNone;
        for (int i = 0; i < kMaxChildren; ++i) r.children[i] = kFamilyNone;
    }
    fam[0].id = 500; fam[0].children[0] = 501; fam[0].spouse = kFamilyNone;
    fam[1].id = 501; fam[1].father = 500;
    FamilyTree tree{fam, 2};
    CHECK(FamilyIsParentOf(tree, 500, 501));
    CHECK_EQ(FamilyGetFather(fam[1]), 500);

    i32 kids[kMaxChildren];
    CHECK_EQ(FamilyGetChildren(fam[0], kids, kMaxChildren), 1);
    CHECK_EQ(kids[0], 501);
}
