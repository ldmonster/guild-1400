// e2e: drive a thief-guild crime session across location4's dialog bodies — a
// player visits the dungeon, bribes the jailer, then runs a kidnap and a spy
// operation, observing the command batches / law side-effects the way the live
// contact-loop dispatch would. One shared hook recorder spans the whole flow.
#include "world/location4.h"

#include <climits>
#include <tuple>
#include <vector>

#include "test.h"

using namespace guild::world;

namespace {

SlotTableView Slots(std::vector<std::uint8_t> occ, std::vector<std::int32_t> id) {
    SlotTableView t; t.occupied = std::move(occ); t.id = std::move(id); return t;
}
SelectionTable Sel(std::initializer_list<std::tuple<bool,bool,std::int32_t>> rows) {
    SelectionTable s{}; int i = 0;
    for (auto& r : rows) { if (i >= kSelectionSlots) break;
        s[i].present = std::get<0>(r); s[i].active = std::get<1>(r);
        s[i].id = std::get<2>(r); ++i; }
    return s;
}

struct Session {
    LocationDialogHooks  gui{};
    LocationDialog4Hooks ex{};
    // running tallies across the whole session
    int totalBatches = 0;
    int lastBatchCode = -1, lastBatchCount = -1;
    std::vector<std::int32_t> lastIds;
    int voiceCalls = 0;
    int violations = 0;
    int bribes = 0;
    std::vector<std::int32_t> frames; std::size_t fi = 0;

    static Session* g;
    Session() {
        g = this;
        gui.openForm = [](const char*) -> std::int32_t { return 1; };
        gui.destroyForm = [](std::int32_t) {};
        gui.frameStep = [](std::int32_t) -> std::int32_t {
            if (g->fi >= g->frames.size()) return INT32_MIN;
            return g->frames[g->fi++]; };
        gui.queueBatch = [](int code, int count, const std::int32_t* ids, int n) {
            ++g->totalBatches; g->lastBatchCode = code; g->lastBatchCount = count;
            g->lastIds.assign(ids, ids + n); };
        gui.playFavorVoice = [] { ++g->voiceCalls; };
        gui.showMessage = [](int) {};
        ex.checkSkillRequirement = [](int) -> bool { return true; };
        ex.requestBuildOp = [](std::int32_t, int) {};
        ex.evaluateViolation = [](int,int,std::int32_t,std::int32_t,int) { ++g->violations; };
        ex.countExistingHandlers = []() -> int { return 0; };
        ex.enqueueBribe = [](std::int32_t,std::int32_t,int) { ++g->bribes; };
        ex.selectedBuilding = []() -> std::int32_t { return -1; };
        ex.queueInfoRequest = [](std::int32_t,std::int32_t) {};
        ex.dragSlotCount = []() -> int { return 0; };
        ex.commitTrainingItem = [](int) {};
        SetLocationDialogHooks(&gui);
        SetLocationDialog4Hooks(&ex);
    }
    ~Session() { SetLocationDialogHooks(nullptr); SetLocationDialog4Hooks(nullptr); }
    void confirmOnce() { frames = {1}; fi = 0; }
};
Session* Session::g = nullptr;

} // namespace

// A full crime spree: bribe -> kidnap -> spy, all confirmed, asserting the
// per-step batches accumulate and the kidnap fires its law side-effect once.
TEST(Location4E2E, CrimeSpreeAccumulatesBatches) {
    Session s;

    // Step 1: bribe the jailer.
    s.confirmOnce();
    auto bribe = DungeonBribeDialog(true, true, false, /*jailer=*/12, /*city=*/4, /*amount=*/999);
    CHECK(bribe.committed);
    CHECK_EQ(bribe.action, 57);
    CHECK_EQ(s.bribes, 1);
    CHECK_EQ(s.totalBatches, 1);

    // Step 2: kidnap — 3 occupied slots -> a 3-id batch + 1 law violation.
    s.confirmOnce();
    auto kid = ThiefKidnapDialog(true, false, false, /*perp=*/12, /*cityFirstId=*/500,
                                 Slots({1,0,1,1}, {7,8,9,10}));
    CHECK(kid.committed);
    CHECK_EQ(kid.action, 60);
    CHECK_EQ(kid.count, 3);
    CHECK_EQ(s.violations, 1);
    CHECK_EQ(s.totalBatches, 2);
    CHECK_EQ(s.lastBatchCount, 3);

    // Step 3: spy on a building — cap 8, 5 occupied -> 5-id batch.
    s.confirmOnce();
    auto spy = ThiefSpyBuildingDialog(true, true, /*occupied=*/0, /*capacity=*/100,
                                      Slots({1,1,0,1,1,1}, {1,2,3,4,5,6}));
    CHECK(spy.committed);
    CHECK_EQ(spy.action, 64);
    CHECK_EQ(spy.count, 5);
    CHECK_EQ(s.totalBatches, 3);
    if (s.lastIds.size() == 5u) {
        CHECK_EQ(s.lastIds[0], 1);
        CHECK_EQ(s.lastIds[4], 6);
    }

    // Across the spree: kidnap + spy ack with a favor-voice line (bribe does NOT
    // play one in the original 0x523d1c); exactly 1 law violation (kidnap only).
    CHECK_EQ(s.voiceCalls, 2);
    CHECK_EQ(s.violations, 1);
}

// Guard side of the same town: customs then detain, both confirmed.
TEST(Location4E2E, GuardSweepCustomsThenDetain) {
    Session s;
    auto notFull = Slots({1,0}, {1,2});

    s.confirmOnce();
    auto customs = GuardCustomsDialog(true, notFull,
        Sel({{true,true,21},{true,true,22},{true,true,23}}));
    CHECK_EQ(customs.action, 101);
    CHECK_EQ(customs.count, 3);
    CHECK_EQ(s.lastBatchCode, 101);

    s.confirmOnce();
    auto detain = GuardDetainDialog(true, false, notFull,
        Sel({{true,false,30},{true,true,31}}));
    CHECK(detain.committed);
    CHECK_EQ(detain.action, 100);
    CHECK_EQ(detain.count, 1);            // single, first active (31)
    if (!s.lastIds.empty()) CHECK_EQ(s.lastIds[0], 31);

    CHECK_EQ(s.totalBatches, 2);
}
