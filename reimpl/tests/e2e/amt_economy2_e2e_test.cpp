// E2E flow across the Amt office-administration leaves: a simulated "office
// reset + rivalry raid + member highlight" turn. Exercises ResetGuildSlots,
// FindNextActiveBuilding, ComputeBuildingRivalryScore, HighlightGuildMembers and
// TriggerOfficeNotice through one shared hook table, verifying the cross-function
// command sequence.
#include <string>
#include <vector>

#include "tests/framework/test.h"
#include "world/amt_economy2.h"

using namespace guild;
using namespace guild::world;

namespace {

struct World {
    std::vector<std::string> commands; // ordered command log
    int addEntryRet = 1;
    // building wealth oracle handled by the BuildingScanEntry views.
};
World* g_w = nullptr;

int E2eAdd(u8 holder, i32 primary, int state, int succ, int flag) {
    g_w->commands.push_back("ADD h=" + std::to_string(holder) + " s=" + std::to_string(state) +
                            " p=" + std::to_string(primary));
    (void)succ; (void)flag;
    return g_w->addEntryRet;
}
void E2eDelta(i32 id, int f) {
    g_w->commands.push_back("DELTA id=" + std::to_string(id) + " f=" + std::to_string(f));
}
void E2eReq16(i32 p, i32 r, i32 amt, int) {
    g_w->commands.push_back("PAY " + std::to_string(r) + " amt=" + std::to_string(amt));
    (void)p;
}
void E2eArgs26(i32 p, int, float) {
    g_w->commands.push_back("REP " + std::to_string(p));
}
void E2eCoord(i32 a, i32 b, int) {
    g_w->commands.push_back("HILITE " + std::to_string(a) + "->" + std::to_string(b));
}

bool E2ePersonFind(i32 id, bool* present, bool* dirty) {
    // ids 0..2 exist; id0 active, id1 absent+dirty, id2 active.
    if (id == 0) { *present = true;  *dirty = false; return true; }
    if (id == 1) { *present = false; *dirty = true;  return true; }
    if (id == 2) { *present = true;  *dirty = false; return true; }
    return false;
}

float E2eRoll() { return 0.0f; } // deterministic: always win the rivalry roll

OfficeHolder Slot(u8 holder, i32 city, u8 type, u8 state) {
    OfficeHolder s{};
    s.holder = holder; s.city = city; s.type = type; s.state = state;
    return s;
}

} // namespace

TEST(AmtEconomy2E2E, OfficeTurnFlow) {
    World w;
    g_w = &w;

    AmtEconomy2Hooks h;
    h.officeAddTableEntry = E2eAdd;
    h.queueDeltaFlag = E2eDelta;
    h.queueRequest16 = E2eReq16;
    h.queueArgs26 = E2eArgs26;
    h.queueCoord27 = E2eCoord;
    h.personFind = E2ePersonFind;
    h.randFloatScaled = E2eRoll;
    AmtEconomy2SetHooks(h);

    // --- Phase 1: reset office slots --------------------------------------
    OfficeHolder slots[3] = {
        Slot(/*holder*/10, /*city(id)*/0, /*type*/2, /*state*/3), // active -> ADD state1
        Slot(/*holder*/11, /*city(id)*/1, /*type*/2, /*state*/1), // absent+dirty -> ADD state3 + DELTA
        Slot(/*holder*/12, /*city(id)*/2, /*type*/4, /*state*/1), // active, state already 1 -> nothing
    };
    bool dirtyBuildings[2] = {false, true};
    ResetGuildSlotsResult rr = ResetGuildSlots(slots, 3, dirtyBuildings, 2);
    CHECK_EQ(rr.slotAssignsQueued, 1);
    CHECK_EQ(rr.slotVacanciesQueued, 1);
    CHECK_EQ(rr.holderFlagsCommitted, 1);
    CHECK_EQ(rr.buildingFlagsCommitted, 1);

    // --- Phase 2: pick the richest active building ------------------------
    ActiveBuildingCache cache;
    BuildingScanEntry bld[3];
    bld[0] = {/*id*/0, /*type*/2, /*active*/true, /*wealth*/4000};
    bld[1] = {/*id*/1, /*type*/2, /*active*/true, /*wealth*/12000}; // richest
    bld[2] = {/*id*/2, /*type*/2, /*active*/true, /*wealth*/8000};
    i32 chosen = -1, chosenWealth = 0;
    int ok = FindNextActiveBuilding(&cache, /*gen*/100, bld, 3, &chosen, &chosenWealth);
    CHECK_EQ(ok, 1);
    CHECK_EQ(chosen, 1);
    CHECK_EQ(chosenWealth, 12000);

    // --- Phase 3: rivalry raid from the chosen building -------------------
    RivalrySelf self;
    self.cityId = 0;
    self.cityRegion = 0;
    self.workstationSum = 8.0f;
    self.wealth = 1000;
    RivalryRival rivals[2];
    rivals[0].id = 50; rivals[0].type = 1; rivals[0].cityId = 1; rivals[0].cityRegion = 0;
    rivals[0].workForce = 4; rivals[0].wealth = 2000; rivals[0].reputation = 0.5f;
    rivals[1].id = 51; rivals[1].type = 1; rivals[1].cityId = 0; // same city -> skipped
    RivalryResult raid = ComputeBuildingRivalryScore(self, rivals, 2, /*currency*/0);
    CHECK_EQ(raid.rivalsPaid, 1);
    CHECK_EQ(raid.totalPayout, 528);

    // --- Phase 4: highlight fellow guild members --------------------------
    i32 members[3] = {99, -1, 50};
    int hi = HighlightGuildMembers(/*self*/99, /*category*/2, /*arg*/1, members, 3);
    CHECK_EQ(hi, 1); // 99==self skipped, -1 skipped, 50 highlighted

    // --- Phase 5: trigger a vacancy notice --------------------------------
    i32 tr = TriggerOfficeNotice(/*type*/4, slots, 3); // matches slot index 2 (holder 12)
    CHECK_EQ(tr, 1); // addEntryRet

    // The full command sequence should be ordered and complete.
    CHECK(w.commands.size() >= 7);
    CHECK_EQ(w.commands.front(), std::string("ADD h=10 s=1 p=0"));
    // rivalry pay + rep commands appear after the reset/delta commands.
    bool sawPay = false, sawHilite = false;
    for (const auto& c : w.commands) {
        if (c == "PAY 50 amt=528") sawPay = true;
        if (c == "HILITE 99->50") sawHilite = true;
    }
    CHECK(sawPay);
    CHECK(sawHilite);

    AmtEconomy2ResetHooks();
}
