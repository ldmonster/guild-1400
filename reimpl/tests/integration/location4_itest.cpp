#include "test.h"

// Integration: drive location4's dialog bodies against the REAL reconstructed
// location3 sibling. location4's dialogs internally call location3's slot/selection
// scanners (FirstOccupiedSlot / SlotTableFull / CollectOccupiedIds /
// CollectSelectionIds, all defined in src/world/location3.cpp) — exactly the shared
// primitives the live binary reuses across every VIBE_Location_* dialog body. We
// link the real location3.cpp (NOT a mock), compute the expected batch by calling
// those real scanners directly here, and assert location4's dialog queues the
// IDENTICAL ids/count/capacity-gate. This is the real cross-module contract.
#include "world/location4.h"
#include "world/location3.h"   // REAL sibling: the shared collectors + GUI hooks

#include <climits>
#include <tuple>
#include <vector>

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

struct Wire {
    LocationDialogHooks  gui{};
    LocationDialog4Hooks ex{};
    int batchCode = -1, batchCount = -1;
    std::vector<std::int32_t> batchIds;
    std::vector<std::int32_t> frames; std::size_t fi = 0;
    static Wire* g;
    Wire() {
        g = this;
        gui.openForm = [](const char*) -> std::int32_t { return 1; };
        gui.destroyForm = [](std::int32_t) {};
        gui.frameStep = [](std::int32_t) -> std::int32_t {
            if (g->fi >= g->frames.size()) return INT32_MIN;
            return g->frames[g->fi++]; };
        gui.queueBatch = [](int code, int count, const std::int32_t* ids, int n) {
            g->batchCode = code; g->batchCount = count; g->batchIds.assign(ids, ids + n); };
        gui.playFavorVoice = [] {};
        gui.showMessage = [](int) {};
        ex.checkSkillRequirement = [](int) -> bool { return true; };
        ex.requestBuildOp = [](std::int32_t, int) {};
        ex.evaluateViolation = [](int,int,std::int32_t,std::int32_t,int) {};
        ex.countExistingHandlers = []() -> int { return 0; };
        SetLocationDialogHooks(&gui);
        SetLocationDialog4Hooks(&ex);
    }
    ~Wire() { SetLocationDialogHooks(nullptr); SetLocationDialog4Hooks(nullptr); }
    void confirm() { frames = {1}; fi = 0; }
};
Wire* Wire::g = nullptr;

} // namespace

// Kidnap queues exactly what the REAL location3 CollectOccupiedIds produces.
TEST(Location4Itest, KidnapBatchMatchesRealLocation3Collector) {
    Wire w; w.confirm();
    auto table = Slots({0,1,1,0,1,1,0,1}, {10,11,12,13,14,15,16,17});

    // Oracle: call the REAL sibling scanner directly (uncapped, as kidnap does).
    std::vector<std::int32_t> expected;
    int expectedCount = CollectOccupiedIds(table, kMaxSlots, expected);

    auto o = ThiefKidnapDialog(true, false, false, 1, 99, table);

    CHECK(o.committed);
    CHECK_EQ(o.count, expectedCount);
    CHECK_EQ(w.batchCount, expectedCount);
    CHECK_EQ(w.batchIds.size(), expected.size());
    if (w.batchIds.size() == expected.size())
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK_EQ(w.batchIds[i], expected[i]);
}

// Spy honours the cap-8 of the REAL collector identically.
TEST(Location4Itest, SpyBatchMatchesRealCollectorCap8) {
    Wire w; w.confirm();
    std::vector<std::uint8_t> occ(12, 1);
    std::vector<std::int32_t> id;
    for (int i = 0; i < 12; ++i) id.push_back(400 + i);
    auto table = Slots(occ, id);

    std::vector<std::int32_t> expected;
    int expectedCount = CollectOccupiedIds(table, 8, expected);   // real cap-8

    auto o = ThiefSpyBuildingDialog(true, true, 0, 100, table);

    CHECK_EQ(o.count, expectedCount);
    CHECK_EQ(expectedCount, 8);
    CHECK_EQ(w.batchIds.size(), 8u);
    if (w.batchIds.size() == expected.size())
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK_EQ(w.batchIds[i], expected[i]);
}

// Customs selection collection matches the REAL CollectSelectionIds (cap 6).
TEST(Location4Itest, CustomsSelectionMatchesRealCollector) {
    Wire w; w.confirm();
    auto notFull = Slots({1,0}, {1,2});
    auto sel = Sel({{true,true,71},{true,false,72},{true,true,73},
                    {true,true,74},{true,true,75},{true,true,76},
                    {true,true,77},{true,true,78}});

    std::vector<std::int32_t> expected;
    int expectedCount = CollectSelectionIds(sel, 6, expected);    // real cap-6

    auto o = GuardCustomsDialog(true, notFull, sel);

    CHECK_EQ(o.count, expectedCount);
    CHECK_EQ(expectedCount, 6);
    if (w.batchIds.size() == expected.size())
        for (std::size_t i = 0; i < expected.size(); ++i)
            CHECK_EQ(w.batchIds[i], expected[i]);
}

// Capacity gate: location4 uses the REAL location3 SlotTableFull verdict.
TEST(Location4Itest, CapacityGateUsesRealSlotTableFull) {
    Wire w;
    auto empty = Slots(std::vector<std::uint8_t>(10, 0), std::vector<std::int32_t>(10, 0));
    CHECK(SlotTableFull(empty));                 // real sibling says full

    auto o = GuardCustomsDialog(true, empty, Sel({{true,true,1}}));
    CHECK(!o.opened);                            // location4 honoured the gate
    CHECK_EQ(w.batchCode, -1);
}
