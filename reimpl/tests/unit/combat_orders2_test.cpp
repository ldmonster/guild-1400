#include "test.h"

#include "sim/combat_orders2.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// IsFernTargetClass — the 372/374/350/352 classification.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, FernClassMembership) {
    CHECK(IsFernTargetClass(372));
    CHECK(IsFernTargetClass(374));
    CHECK(IsFernTargetClass(350));
    CHECK(IsFernTargetClass(352));
    CHECK(!IsFernTargetClass(340));
    CHECK(!IsFernTargetClass(0));
    CHECK(!IsFernTargetClass(373));
}

// ---------------------------------------------------------------------------
// BuildDummyTargetNames — FERN/NAHK naming with independent 1-based counters.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, DummyTargetNaming) {
    std::vector<i32> roster   = {10, 11, -1, 12, 13};
    std::vector<i16> defClass = {372, 340, 0, 350, 100};  // FERN,NAHK,(skip),FERN,NAHK
    auto names = BuildDummyTargetNames(roster, "ABC", defClass);
    CHECK_EQ(static_cast<int>(names.size()), 4);
    if (names.size() == 4) {
        CHECK(names[0].isFern);
        CHECK_EQ(names[0].index, 1);
        CHECK_EQ(std::strcmp(names[0].name, "dummy_ABC_FERN_01"), 0);
        CHECK(!names[1].isFern);
        CHECK_EQ(names[1].index, 1);
        CHECK_EQ(std::strcmp(names[1].name, "dummy_ABC_NAHK_01"), 0);
        // index 2 of roster is -1 -> skipped; index 3 is FERN counter 2.
        CHECK(names[2].isFern);
        CHECK_EQ(names[2].index, 2);
        CHECK_EQ(std::strcmp(names[2].name, "dummy_ABC_FERN_02"), 0);
        CHECK(!names[3].isFern);
        CHECK_EQ(names[3].index, 2);
        CHECK_EQ(std::strcmp(names[3].name, "dummy_ABC_NAHK_02"), 0);
    }
}

TEST(CombatOrders2Unit, DummyTargetNegativeClassSkipped) {
    std::vector<i32> roster   = {1, 2};
    std::vector<i16> defClass = {-1, 372};   // -1 == no def -> skipped
    auto names = BuildDummyTargetNames(roster, "X", defClass);
    CHECK_EQ(static_cast<int>(names.size()), 1);
    if (names.size() == 1) {
        CHECK(names[0].isFern);
        CHECK_EQ(names[0].index, 1);
    }
}

// ---------------------------------------------------------------------------
// ResetOrderSlotTables — writes indices 8,16,..,48 of each of the 6 tables.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, SlotTableReset) {
    i32 t0[56] = {}, t1[56] = {}, t2[56] = {}, t3[56] = {}, t4[56] = {}, t5[56] = {};
    i32* tables[6] = {t0, t1, t2, t3, t4, t5};
    ResetOrderSlotTables(tables);
    // index 0 untouched, 8..48 set to -1, others untouched.
    CHECK_EQ(t0[0], 0);
    CHECK_EQ(t0[8], -1);
    CHECK_EQ(t0[16], -1);
    CHECK_EQ(t0[48], -1);
    CHECK_EQ(t0[9], 0);
    CHECK_EQ(t5[40], -1);
    CHECK_EQ(t3[24], -1);
}

// ---------------------------------------------------------------------------
// CountOrderSlotRows — highlight gate + 31/48 bounds.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, OrderSlotRowsHighlightGate) {
    // 4 populated; only those that faction-match (no global highlight) count.
    std::vector<bool> pop  = {true, true, false, true, true};
    std::vector<bool> face = {true, false, true, true, false};
    CHECK_EQ(CountOrderSlotRows(pop, face, false), 2);  // idx0 + idx3
    // With global highlight every populated entry counts (idx2 not populated).
    CHECK_EQ(CountOrderSlotRows(pop, face, true), 4);
}

TEST(CombatOrders2Unit, OrderSlotRowsColumnBound) {
    // 7 populated+matching -> column would reach 48 after 6 rows (col 0,8,..,40
    // then 48), capping at 6.
    std::vector<bool> pop(10, true), face(10, true);
    CHECK_EQ(CountOrderSlotRows(pop, face, false), 6);
}

// ---------------------------------------------------------------------------
// BuildObjectiveIcons — 5-wide grid + filter + running total.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, ObjectiveIconGridLayout) {
    std::vector<ObjectiveEntry> e;
    for (int i = 0; i < 7; ++i) e.push_back({0, 1, 0.0});
    i32 total = 0;
    auto icons = BuildObjectiveIcons(e, 0, 0, 100, 50, total);
    CHECK_EQ(static_cast<int>(icons.size()), 7);
    if (icons.size() == 7) {
        CHECK_EQ(icons[0].x, 100); CHECK_EQ(icons[0].y, 50);
        CHECK_EQ(icons[1].x, 220); CHECK_EQ(icons[1].y, 50);
        CHECK_EQ(icons[4].x, 580); CHECK_EQ(icons[4].y, 50);
        CHECK_EQ(icons[5].x, 100); CHECK_EQ(icons[5].y, 146);  // wraps to row 1
        CHECK_EQ(icons[6].x, 220); CHECK_EQ(icons[6].y, 146);
        CHECK_EQ(icons[0].labelX, 84);  // 100-16
        CHECK_EQ(icons[0].labelY, 35);  // 50-15
    }
}

TEST(CombatOrders2Unit, ObjectiveIconFilterAndTotal) {
    std::vector<ObjectiveEntry> e = {
        {0, 2, 10.0},   // owner 0
        {1, 3, 5.5},    // owner 1
        {0, 1, 100.0},  // owner 0
    };
    // mode 0: all 3, total = 20 + 16(int trunc of 36.5? no: 20 + 16.5=36) = 136.
    i32 total = 0;
    auto icons = BuildObjectiveIcons(e, 0, 0, 0, 0, total);
    CHECK_EQ(static_cast<int>(icons.size()), 3);
    CHECK_EQ(total, 136);  // 20 -> 36 -> 136 (matches python oracle)

    // mode 1 (owner == playerFaction 0): entries 0 and 2.
    total = 0;
    icons = BuildObjectiveIcons(e, 1, 0, 0, 0, total);
    CHECK_EQ(static_cast<int>(icons.size()), 2);
    CHECK_EQ(total, 120);  // 20 + 100

    // mode 2 (owner != playerFaction 0): entry 1 only.
    total = 0;
    icons = BuildObjectiveIcons(e, 2, 0, 0, 0, total);
    CHECK_EQ(static_cast<int>(icons.size()), 1);
    CHECK_EQ(total, 16);  // (int)16.5
}

// ---------------------------------------------------------------------------
// BuildUnitRosterPanel — column spacing + cell positions + dead branch.
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, RosterPanelLayout) {
    std::vector<i32> roster = {0, -1, 2, -1, -1, 5, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<bool> alive = {true, false, false, false, false, true, false, true};
    int colW = 0;
    auto cells = BuildUnitRosterPanel(roster, 4, 800, 10, 20, alive, colW);
    // count populated = 4 (0,2,5,7) -> colW = (800-48)/5 = 150.
    CHECK_EQ(colW, 150);
    CHECK_EQ(static_cast<int>(cells.size()), 4);
    if (cells.size() == 4) {
        // i=0: x=150+150*(0%4)+10=160; y=100*(0/4)+20=20
        CHECK_EQ(cells[0].x, 160); CHECK_EQ(cells[0].y, 20);
        CHECK(!cells[0].dead);     // alive[0]=true
        // i=2: x=150+150*2+10=460; y=20
        CHECK_EQ(cells[1].x, 460); CHECK_EQ(cells[1].y, 20);
        CHECK(cells[1].dead);      // alive[2]=false
        // i=5: x=150+150*(5%4=1)+10=310; y=100*(5/4=1)+20=120
        CHECK_EQ(cells[2].x, 310); CHECK_EQ(cells[2].y, 120);
        CHECK(!cells[2].dead);     // alive[5]=true
        // i=7: x=150+150*(7%4=3)+10=610; y=100*(7/4=1)+20=120
        CHECK_EQ(cells[3].x, 610); CHECK_EQ(cells[3].y, 120);
        CHECK_EQ(cells[3].healthBarY, 180);  // y+60
        CHECK_EQ(cells[3].labelX, 574);      // x-36
        CHECK_EQ(cells[3].labelY, 103);      // y-17
    }
}

// ---------------------------------------------------------------------------
// HudSliderCenterX
// ---------------------------------------------------------------------------
TEST(CombatOrders2Unit, HudSliderCenter) {
    CHECK_EQ(HudSliderCenterX(800), 200);   // (800-400)/2
    CHECK_EQ(HudSliderCenterX(1024), 312);  // (1024-400)/2
    CHECK_EQ(HudSliderCenterX(400), 0);
}

// ---------------------------------------------------------------------------
// UpdateUnitOrders driver — slot iteration, prune, gate, case dispatch.
// ---------------------------------------------------------------------------
namespace {
// A recording hook set for the order driver.
struct DriverRecorder {
    std::vector<std::pair<int, OrderEmit>> emissions;
    bool freeTileAvailable = false;
    bool onTile = false;
    bool alive = true;
    bool hasDef = false;
    i16  defClass = 0;
    int  roll = 0;
};
DriverRecorder* g_rec = nullptr;

int RecPacketStatus(i32) { return 0; }
OrderDriverHooks::UnitInfo RecResolve(i32, u8) {
    OrderDriverHooks::UnitInfo u;
    u.alive = g_rec->alive; u.hasDef = g_rec->hasDef; u.defClass = g_rec->defClass;
    return u;
}
bool RecOnTile(const OrderSlot&, float) { return g_rec->onTile; }
bool RecFindFree(i32, i32, i32& ox, i32& oz) {
    if (g_rec->freeTileAvailable) { ox = 99; oz = 88; return true; }
    return false;
}
int RecRoll(u16) { return g_rec->roll; }
void RecEmit(int idx, OrderEmit op) { g_rec->emissions.push_back({idx, op}); }

OrderDriverHooks MakeHooks() {
    OrderDriverHooks h;
    h.packetStatus = &RecPacketStatus;
    h.resolveUnit  = &RecResolve;
    h.onTargetTile = &RecOnTile;
    h.findFreeTile = &RecFindFree;
    h.randomModulo = &RecRoll;
    h.emit         = &RecEmit;
    return h;
}
OrderSlot MakeSlot(i32 id, u8 state, i32 packetId = 1) {
    OrderSlot s;
    s.unitId = id; s.state = state; s.packetId = packetId; s.phase = 0;
    s.tileX = 1; s.tileZ = 2; s.tileAux = 3; s.moved = 0; s.firing = 0;
    s.hitFlag = 0; s.predictedDamage = 0;
    return s;
}
} // namespace

TEST(CombatOrders2Unit, DriverHaltNoOp) {
    DriverRecorder rec; g_rec = &rec;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderMove)};
    int worked = UpdateUnitOrders(slots, /*globalHalt=*/true);
    CHECK_EQ(worked, 0);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 0);
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverSkipsIdleAndEmpty) {
    DriverRecorder rec; g_rec = &rec;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    std::vector<OrderSlot> slots = {
        MakeSlot(5, kOrderIdle),    // state 0 -> skip
        MakeSlot(-1, kOrderMove),   // empty -> skip
    };
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 0);
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverDeadUnitPruneResetsAndBails) {
    DriverRecorder rec; g_rec = &rec;
    rec.alive = false; rec.hasDef = true;   // dead + def -> prune + return
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    std::vector<OrderSlot> slots = {
        MakeSlot(5, kOrderMove),
        MakeSlot(6, kOrderMove),    // never reached (driver returns at slot 0)
    };
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 0);
    // slot 0 reset to (-1, idle); slot 1 untouched (still state move).
    CHECK_EQ(slots[0].unitId, -1);
    CHECK_EQ(static_cast<int>(slots[0].state), 0);
    CHECK_EQ(slots[1].unitId, 6);
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverMoveCaseFindsFreeTileEmitsPath) {
    DriverRecorder rec; g_rec = &rec;
    rec.freeTileAvailable = true;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderMove, /*packetId=*/1)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    // phase set to 1, packetId now pending (1), Op85 then Op78 emitted.
    CHECK_EQ(static_cast<int>(slots[0].phase), 1);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 2);
    if (rec.emissions.size() == 2) {
        CHECK(rec.emissions[0].second == OrderEmit::Op85Anim);
        CHECK(rec.emissions[1].second == OrderEmit::Op78Path);
    }
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverMovePhaseSetOnTileReachesObjective) {
    DriverRecorder rec; g_rec = &rec;
    rec.onTile = true;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    OrderSlot s = MakeSlot(5, kOrderMove, 1);
    s.phase = 1;   // already walking
    std::vector<OrderSlot> slots = {s};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    // LABEL_202: Op85 anim6 then Op80 sync.
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 2);
    if (rec.emissions.size() == 2) {
        CHECK(rec.emissions[0].second == OrderEmit::Op85Anim);
        CHECK(rec.emissions[1].second == OrderEmit::Op80Sync);
    }
    CHECK_EQ(slots[0].packetId, -1);   // reset after gate
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverGateBlocksWhenPacketPending) {
    DriverRecorder rec; g_rec = &rec;
    rec.freeTileAvailable = true;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    // packetId != 1 and packetStatus()==0 -> gated, no work.
    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderMove, /*packetId=*/77)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 0);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 0);
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverWareCollectPhases) {
    DriverRecorder rec; g_rec = &rec;
    rec.freeTileAvailable = true;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    // warePhase 1 -> Op81 ware + Op78 path.
    OrderSlot s = MakeSlot(5, kOrderWareCollect, 1);
    s.hitFlag = (1 << 16);   // WarePhase() == 1 (byte +34 aliases hitFlag byte 2)
    std::vector<OrderSlot> slots = {s};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 2);
    if (rec.emissions.size() == 2) {
        CHECK(rec.emissions[0].second == OrderEmit::Op81Ware);
        CHECK(rec.emissions[1].second == OrderEmit::Op78Path);
    }
    SetOrderDriverHooks(nullptr);
}

TEST(CombatOrders2Unit, DriverStandUpOnce) {
    DriverRecorder rec; g_rec = &rec;
    OrderDriverHooks h = MakeHooks();
    SetOrderDriverHooks(&h);
    std::vector<OrderSlot> slots = {MakeSlot(5, kOrderStandUp, 1)};
    int worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(worked, 1);
    CHECK_EQ(static_cast<int>(slots[0].phase), 1);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 1);
    if (rec.emissions.size() == 1)
        CHECK(rec.emissions[0].second == OrderEmit::Op85Anim);
    // A second tick with phase already set emits nothing more.
    rec.emissions.clear();
    slots[0].packetId = 1;
    worked = UpdateUnitOrders(slots, false);
    CHECK_EQ(static_cast<int>(rec.emissions.size()), 0);
    SetOrderDriverHooks(nullptr);
}
