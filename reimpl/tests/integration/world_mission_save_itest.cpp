// Integration: drive the deferred mission save/load + dialog-flow cores against
// the REAL mission siblings — the slot table + register/find (world/mission.cpp),
// the descriptor table + LCG random pick (world/event.cpp), and the reward/outcome
// rules (world/mission_rules.cpp). No mocks: a mission is registered through the
// real register path, located through the real owned-slot scan, resolved through
// the real descriptor resolver, then the whole table is round-tripped through the
// real save/load serializer.
#include "test.h"

#include <vector>

#include "world/mission.h"
#include "world/mission_save.h"
#include "world/mission_dialog.h"
#include "world/mission_rules.h"
#include "world/event.h"

using namespace guild;
using namespace guild::world;

// Build a real descriptor table: value v -> category, so PickRandomByType and the
// reward resolver have live data.
static void SeedDescriptors() {
    EventTableReset();
    // value 11 (cat 0), 19 (cat 1), 23 (cat 2), 28 (cat 3), 40 (cat 5)
    const u8 vals[] = {11, 19, 23, 28, 40};
    const u8 cats[] = {0, 1, 2, 3, 5};
    for (int i = 0; i < 5; ++i) {
        g_eventTable[i].word0    = i + 1;
        g_eventTable[i].value    = vals[i];
        g_eventTable[i].category = cats[i];
        g_eventTable[i].paramB   = 100 + i;   // voice index
        g_eventTable[i].paramC   = 500 * (i + 1); // money-ish param
    }
    g_eventTableCount = 5;
}

TEST(MissionSaveItest, RegisterScanResolveRoundTrip) {
    SeedDescriptors();
    MissionSlotTableReset();
    g_missionSlotMode = 0;

    // Register two missions through the REAL register path for two owners.
    int s0 = MissionSlotRegister(/*owner*/ 7001, /*type*/ 19);
    int s1 = MissionSlotRegister(/*owner*/ 7002, /*type*/ 28);
    CHECK_EQ(s0, 0);
    CHECK_EQ(s1, 1);

    // Real owned-slot scan locates owner 7002's mission at slot 1.
    CHECK_EQ(MissionFindOwnedSlot(7002), 1);
    CHECK_EQ(MissionFindOwnedSlot(7001), 0);
    CHECK_EQ(MissionFindOwnedSlot(9999), -1);

    // Real descriptor resolve for owner 7002's mission type (28 -> descriptor idx 3).
    MissionRewardInfo info{};
    CHECK(MissionResolveReward(g_missionSlots[1].type, &info));
    CHECK_EQ(info.descriptorIndex, 3);
    CHECK_EQ(info.voiceIndex, g_eventTable[3].paramB);  // rec+0x0C

    // Give-dialog seed derives from the matched descriptor's category (+5).
    CHECK_EQ(MissionGiveHistorySeed(g_eventTable[3].category),
             g_eventTable[3].category + 1);

    // Dispatcher routes correctly for this active person.
    CHECK(MissionDispatchDialog(/*give*/ 0, /*active*/ 28, /*person*/ 28, /*mode*/ 5)
          == MissionDialogKind::kAccept);

    // Round-trip the whole live table through the REAL serializer.
    std::vector<u8> buf(kMissionTableSerializedBytes, 0);
    MissionStream w{buf.data(), buf.size(), 0};
    CHECK(MissionSaveSlotTable(w));

    // Mutate live state, then restore from the saved image.
    i32 savedOwner0 = g_missionSlots[0].owner;
    u8  savedType1  = g_missionSlots[1].type;
    MissionSlotTableReset();
    g_missionSlotMode = 99;  // wrong, will be overwritten by load
    MissionStream r{buf.data(), buf.size(), 0};
    CHECK(MissionLoadSlotTable(r));
    CHECK_EQ(g_missionSlotMode, (u8)0);
    CHECK_EQ(g_missionSlots[0].owner, savedOwner0);
    CHECK_EQ(g_missionSlots[1].type, savedType1);

    // After restore the real scan still finds the owners.
    CHECK_EQ(MissionFindOwnedSlot(7001), 0);
    CHECK_EQ(MissionFindOwnedSlot(7002), 1);
}

TEST(MissionSaveItest, SingleSlotModeAndCompletionRoute) {
    SeedDescriptors();
    MissionSlotTableReset();

    // SlotSetSingle stamps slot 0 unconditionally; the real completion decode then
    // classifies the follow-up.
    MissionSlotSetSingle(8001, 23);
    CHECK_EQ(g_missionSlots[0].owner, (i32)8001);
    CHECK_EQ(g_missionSlots[0].type, (u8)23);
    CHECK_EQ(MissionFindOwnedSlot(8001), 0);

    // active != person + mode 5 -> Completion; the real completion decoder maps the
    // engine's outcome code 3 to a load-session.
    CHECK(MissionDispatchDialog(0, 100, 200, 5) == MissionDialogKind::kCompletion);
    CHECK(MissionDecodeCompletion(3) == MissionCompletionOutcome::kLoadSession);
    CHECK(MissionResultIsLoadSession(2));
}
