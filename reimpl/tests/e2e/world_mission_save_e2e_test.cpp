// End-to-end mission lifecycle for the deferred save/load + dialog cores:
//   seed live descriptors -> register a fixed-seed random mission -> locate it via
//   the owned-slot scan -> resolve its reward descriptor -> route the give dialog ->
//   decode the give outcome -> persist the whole table and reload it, verifying the
//   restored state survives a save/load cycle. Cross-module against the REAL
//   mission / event / rules siblings.
//
// GUARDED: the real on-disk save-file round-trip (writing the serialized table to a
// real save path) runs only when GUILD_E2E_ASSETS is set; otherwise an in-memory
// round-trip stands in and the test still asserts the lifecycle invariants.
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "world/mission.h"
#include "world/mission_save.h"
#include "world/mission_dialog.h"
#include "world/mission_rules.h"
#include "world/event.h"

using namespace guild;
using namespace guild::world;

TEST(WorldMissionSaveE2E, FullLifecycle) {
    // --- 1. Seed a live descriptor table (value -> category, voice, money). -----
    EventTableReset();
    const u8 vals[] = {11, 19, 23, 28, 40};
    const u8 cats[] = {0, 1, 2, 3, 5};
    for (int i = 0; i < 5; ++i) {
        g_eventTable[i].word0    = i + 1;
        g_eventTable[i].value    = vals[i];
        g_eventTable[i].category = cats[i];
        g_eventTable[i].paramB   = 200 + i;
    }
    g_eventTableCount = 5;

    // --- 2. Register a fixed-seed random mission for an owner. ------------------
    MissionSlotTableReset();
    g_missionSlotMode = 0;
    g_missionLcgState = 1;                 // deterministic LCG seed
    int slot = MissionPickAndRegisterRandom(/*owner*/ 4242, /*category*/ 1);
    CHECK(slot >= 0);
    // Category 1 has exactly one descriptor (value 19) -> the pick is forced to 19.
    CHECK_EQ(g_missionSlots[slot].type, (u8)19);
    CHECK_EQ(g_missionSlots[slot].owner, (i32)4242);

    // --- 3. Locate it via the owned-slot scan. ---------------------------------
    int owned = MissionFindOwnedSlot(4242);
    CHECK_EQ(owned, slot);

    // --- 4. Resolve its reward descriptor. -------------------------------------
    MissionRewardInfo info{};
    CHECK(MissionResolveReward(g_missionSlots[owned].type, &info));
    CHECK_EQ(info.descriptorIndex, 1);          // value 19 -> table idx 1
    CHECK_EQ(info.voiceIndex, g_eventTable[1].paramB);

    // --- 5. Route + decode the give dialog. ------------------------------------
    CHECK(MissionDispatchDialog(/*giveFlag*/ -1, 0, 0, 0) == MissionDialogKind::kGive);
    // Engine returns -1 from the history dialog -> abandon -> session reload.
    CHECK(MissionDecodeGive(-1) == MissionGiveOutcome::kAbandon);
    CHECK(MissionGiveTriggersReload(MissionGiveOutcome::kAbandon));
    // History seed for the matched descriptor category (+5).
    CHECK_EQ(MissionGiveHistorySeed(g_eventTable[1].category),
             g_eventTable[1].category + 1);

    // --- 6. Persist + reload the table; lifecycle state must survive. ----------
    std::vector<u8> buf(kMissionTableSerializedBytes, 0);
    MissionStream w{buf.data(), buf.size(), 0};
    CHECK(MissionSaveSlotTable(w));
    CHECK_EQ(w.pos, kMissionTableSerializedBytes);

    bool wroteRealFile = false;
    if (std::getenv("GUILD_E2E_ASSETS")) {
        // Guarded real on-disk round-trip.
        const char* path = "/tmp/guild_mission_e2e.sav";
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(buf.data(), 1, buf.size(), f);
            std::fclose(f);
            std::vector<u8> rd(kMissionTableSerializedBytes, 0xEE);
            if (FILE* g = std::fopen(path, "rb")) {
                size_t got = std::fread(rd.data(), 1, rd.size(), g);
                std::fclose(g);
                CHECK_EQ(got, kMissionTableSerializedBytes);
                CHECK_EQ(rd, buf);   // bytes survive the disk trip unchanged
                wroteRealFile = true;
            }
            std::remove(path);
        }
    }
    if (!wroteRealFile) {
        std::printf("  [note] GUILD_E2E_ASSETS unset: in-memory round-trip only\n");
    }

    // Wipe and reload from the serialized image.
    MissionSlotTableReset();
    g_missionSlotMode = 0xAA;
    MissionStream r{buf.data(), buf.size(), 0};
    CHECK(MissionLoadSlotTable(r));
    CHECK_EQ(g_missionSlotMode, (u8)0);
    CHECK_EQ(g_missionSlots[owned].type, (u8)19);
    CHECK_EQ(g_missionSlots[owned].owner, (i32)4242);
    CHECK_EQ(MissionFindOwnedSlot(4242), owned);
}
