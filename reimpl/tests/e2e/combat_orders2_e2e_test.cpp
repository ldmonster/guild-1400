#include "test.h"

#include "sim/combat_orders2.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

// E2E: a battle-screen build + order-driver flow across combat_orders2 functions.
// Build the deployment dummy names, lay out the roster + objective panels, then
// run the per-slot order driver to completion, mirroring how the combat screen
// composes these stages in one frame.

namespace {
struct E2ERecorder {
    std::vector<std::pair<int, OrderEmit>> emissions;
    bool freeTile = false;
    bool onTile = false;
};
E2ERecorder* g_e = nullptr;

OrderDriverHooks::UnitInfo E2EResolve(i32 id, u8) {
    OrderDriverHooks::UnitInfo u;
    u.alive = (id != 0);   // unit id 0 == dead (prune trigger)
    u.hasDef = true;
    return u;
}
bool E2EOnTile(const OrderSlot&, float) { return g_e->onTile; }
bool E2EFindFree(i32, i32, i32& ox, i32& oz) {
    if (g_e->freeTile) { ox = 7; oz = 9; return true; }
    return false;
}
void E2EEmit(int idx, OrderEmit op) { g_e->emissions.push_back({idx, op}); }
} // namespace

TEST(CombatOrders2E2E, BattleScreenComposeAndDrive) {
    // --- 1. dummy-target naming for the deployment screen --------------------
    std::vector<i32> roster   = {100, 101, 102, 103};
    std::vector<i16> defClass = {372, 340, 350, 999};  // FERN, NAHK, FERN, NAHK
    auto names = BuildDummyTargetNames(roster, "S1", defClass);
    CHECK_EQ(static_cast<int>(names.size()), 4);
    if (names.size() == 4) {
        CHECK(names[0].isFern);  CHECK_EQ(names[0].index, 1);  // FERN_01
        CHECK(!names[1].isFern); CHECK_EQ(names[1].index, 1);  // NAHK_01
        CHECK(names[2].isFern);  CHECK_EQ(names[2].index, 2);  // FERN_02
        CHECK(!names[3].isFern); CHECK_EQ(names[3].index, 2);  // NAHK_02
    }

    // --- 2. roster panel layout for those 4 units ----------------------------
    std::vector<i32> panelRoster(16, -1);
    panelRoster[0] = 100; panelRoster[1] = 101; panelRoster[2] = 102; panelRoster[3] = 103;
    std::vector<bool> alive = {true, true, false, true};
    int colW = 0;
    auto cells = BuildUnitRosterPanel(panelRoster, 4, 800, 0, 0, alive, colW);
    CHECK_EQ(colW, 150);   // (800-48)/(4+1)
    CHECK_EQ(static_cast<int>(cells.size()), 4);
    if (cells.size() == 4) CHECK(cells[2].dead);  // unit 102 dead

    // --- 3. objective icons (mode 0, all) ------------------------------------
    std::vector<ObjectiveEntry> objs = {{0, 2, 50.0}, {1, 1, 30.0}};
    i32 total = 0;
    auto icons = BuildObjectiveIcons(objs, 0, 0, 16, 8, total);
    CHECK_EQ(static_cast<int>(icons.size()), 2);
    CHECK_EQ(total, 130);  // 100 + 30

    // --- 4. drive the order slots --------------------------------------------
    E2ERecorder rec; g_e = &rec;
    OrderDriverHooks h;
    h.resolveUnit  = &E2EResolve;
    h.onTargetTile = &E2EOnTile;
    h.findFreeTile = &E2EFindFree;
    h.emit         = &E2EEmit;
    SetOrderDriverHooks(&h);

    std::vector<OrderSlot> slots(3);
    // slot 0: move, free tile available -> issues path
    slots[0].unitId = 100; slots[0].state = kOrderMove; slots[0].packetId = 1; slots[0].phase = 0;
    // slot 1: stand -> anim7 + sync
    slots[1].unitId = 101; slots[1].state = kOrderStand; slots[1].packetId = 1;
    // slot 2: escape, phase set, on tile -> objective reached
    slots[2].unitId = 102; slots[2].state = kOrderEscape; slots[2].packetId = 1; slots[2].phase = 1;

    rec.freeTile = true; rec.onTile = true;
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 3);

    // Emission tally per slot.
    int op78 = 0, op80 = 0, op85 = 0;
    for (auto& e : rec.emissions) {
        if (e.second == OrderEmit::Op78Path) ++op78;
        else if (e.second == OrderEmit::Op80Sync) ++op80;
        else if (e.second == OrderEmit::Op85Anim) ++op85;
    }
    // slot0 move: Op85+Op78; slot1 stand: Op85+Op80; slot2 escape reached: Op85+Op80.
    CHECK_EQ(op85, 3);
    CHECK_EQ(op78, 1);
    CHECK_EQ(op80, 2);
    CHECK_EQ(static_cast<int>(slots[0].phase), 1);   // move began walking

    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2E2E, DeadUnitHaltsDriverMidRoster) {
    E2ERecorder rec; g_e = &rec;
    OrderDriverHooks h;
    h.resolveUnit  = &E2EResolve;   // id 0 -> dead
    h.onTargetTile = &E2EOnTile;
    h.findFreeTile = &E2EFindFree;
    h.emit         = &E2EEmit;
    SetOrderDriverHooks(&h);

    std::vector<OrderSlot> slots(2);
    slots[0].unitId = 0; slots[0].state = kOrderMove; slots[0].packetId = 1;  // dead
    slots[1].unitId = 5; slots[1].state = kOrderStand; slots[1].packetId = 1;

    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 0);                 // pruned at slot 0, returns
    CHECK_EQ(slots[0].unitId, -1);
    CHECK_EQ(static_cast<int>(slots[0].state), 0);
    CHECK_EQ(slots[1].unitId, 5);        // never processed
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 0);
    SetOrderDriverHooks(nullptr);
}
