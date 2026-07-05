#include "test.h"

#include "sim/production_slots.h"
#include "sim/building.h"   // g_buildingTypes

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Mock hooks: deterministic market price = type id, scriptable QueryFind /
// grid-slot / time-diff so the pure rules are golden-vector testable without the
// scene tree or the live clock.
// ---------------------------------------------------------------------------
namespace {
struct MockHooks : IProductionSlotHooks {
    // QueryObjectNode scripting
    bool nodeFound = true;
    i32  nodeOwner = 0;
    i32  nodeActive = 1;
    i16  nodeType = 100;
    // FindGridSlot scripting
    bool slotFound = true;
    i32  slotId = 0;
    int  diff = 0;
    i32  lastEmitId = -1; i16 lastEmitType = -1; int emitCount = 0;
    i32  lastNotifyId = -1; int notifyCount = 0;

    double MarketPrice(i16 type) override { return static_cast<double>(type); }
    bool QueryObjectNode(int, i16, i32* o, i32* a, i16* t) override {
        if (o) *o = nodeOwner;
        if (a) *a = nodeActive;
        if (t) *t = nodeType;
        return nodeFound;
    }
    bool FindGridSlot(i16, i32* outId) override {
        if (outId) *outId = slotId;
        return slotFound;
    }
    int DiffMinutes(const GameTime&, const GameTime&) override { return diff; }
    void EmitProductionFinished(i32 id, i16 t) override {
        lastEmitId = id; lastEmitType = t; ++emitCount;
    }
    void NotifyProductReady(i32 id, i16) override {
        lastNotifyId = id; ++notifyCount;
    }
};
struct HookGuard {
    HookGuard(IProductionSlotHooks* h) { SetProductionSlotHooks(h); }
    ~HookGuard() { SetProductionSlotHooks(nullptr); }
};
}  // namespace

// ===========================================================================
// (A) Weapon-slot compatibility — pure golden vectors.
// ===========================================================================
TEST(ProdSlots, WeaponKindOk_Ranged) {
    CHECK(InventoryWeaponSlotKindOk(370, 4) == true);
    CHECK(InventoryWeaponSlotKindOk(370, 3) == false);
    CHECK(InventoryWeaponSlotKindOk(372, 4) == true);
    CHECK(InventoryWeaponSlotKindOk(372, 19) == false);
}
TEST(ProdSlots, WeaponKindOk_ShieldArmour) {
    CHECK(InventoryWeaponSlotKindOk(344, 19) == true);
    CHECK(InventoryWeaponSlotKindOk(344, 4) == false);
    CHECK(InventoryWeaponSlotKindOk(352, 19) == true);
    CHECK(InventoryWeaponSlotKindOk(366, 16) == true);
    CHECK(InventoryWeaponSlotKindOk(366, 4) == false);
    CHECK(InventoryWeaponSlotKindOk(374, 16) == true);
}
TEST(ProdSlots, WeaponKindOk_NonWeaponSlotAlwaysOk) {
    // A slot outside the three families is compatible regardless of kind.
    CHECK(InventoryWeaponSlotKindOk(100, 0) == true);
    CHECK(InventoryWeaponSlotKindOk(100, 4) == true);
}
TEST(ProdSlots, WeaponCompatible_FullForm) {
    // Populate the type table: held node type 7 -> kind 4 (ranged-compatible).
    g_buildingTypes[7].kind = 4;
    g_buildingTypes[8].kind = 16;
    CHECK(InventoryIsWeaponSlotCompatible(true, 7, 370, true) == true);
    CHECK(InventoryIsWeaponSlotCompatible(true, 8, 370, true) == false);
    CHECK(InventoryIsWeaponSlotCompatible(true, 8, 366, true) == true);  // armour
    // No held node -> never compatible.
    CHECK(InventoryIsWeaponSlotCompatible(false, 7, 370, true) == false);
    // Type table not loaded -> kind reads 0 -> ranged slot fails.
    CHECK(InventoryIsWeaponSlotCompatible(true, 7, 370, false) == false);
}

// ===========================================================================
// (B) IsObjectSlotActive / IsProductionSlotMatch — hook-driven decisions.
// ===========================================================================
TEST(ProdSlots, ObjectSlotActive) {
    MockHooks h; HookGuard g(&h);
    h.nodeFound = true; h.slotFound = true; h.nodeActive = 1;
    CHECK(InventoryIsObjectSlotActive(123, 477) == true);
    h.nodeActive = 0;
    CHECK(InventoryIsObjectSlotActive(123, 477) == false);  // *(node+33)==0
    h.nodeActive = 1; h.slotFound = false;
    CHECK(InventoryIsObjectSlotActive(123, 477) == false);  // no grid slot
    h.slotFound = true; h.nodeFound = false;
    CHECK(InventoryIsObjectSlotActive(123, 477) == false);  // no node
}
TEST(ProdSlots, ProductionSlotMatch_OwnerEqualsSlotId) {
    MockHooks h; HookGuard g(&h);
    h.nodeFound = true; h.slotFound = true; h.nodeActive = 1;
    h.nodeOwner = 55; h.slotId = 55;
    CHECK(InventoryIsProductionSlotMatch(200) == true);    // owner==slotId
    h.slotId = 56;
    CHECK(InventoryIsProductionSlotMatch(200) == false);   // mismatch
    h.slotId = 55; h.nodeActive = 0;
    CHECK(InventoryIsProductionSlotMatch(200) == false);   // inactive
}

// ===========================================================================
// (C) CollectProductionSlots — capacity/level/worth accumulation.
// ===========================================================================
// 0x5922d4 (disasm-proven): nodes[0] is the ROOT work-product node — it is NOT
// a slot itself (the loop iterates its children, QueryFind(v4[5],1,5) at
// 0x59231d) and BOTH capacity branches read the ROOT's level ([ebx+0Eh] at
// 0x59234d / 0x5923e1), so every slot gets the same capacity. (The old pins
// assumed per-slot levels and a root-included slot set.)
TEST(ProdSlots, CollectProduction_RootNotHighCap) {
    MockHooks h; HookGuard g(&h);
    ProdSlotCollect out;
    std::vector<ProdSlotNode> nodes = {{10, 2}, {477, 3}, {20, 1}};
    int rc = InventoryCollectProductionSlots(nodes, out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out.count, 2);              // slots = children only
    // root {10,2}: type != 477, level 2 -> cap 20*2 = 40 for EVERY slot.
    CHECK_EQ(out.capTotal, 80);
    CHECK_EQ(out.levTotal, 4);           // per-slot levels 3 + 1
    CHECK_EQ(out.types.size(), static_cast<size_t>(2));
    CHECK_EQ(out.caps[0], 40);
    CHECK_EQ(out.caps[1], 40);
    // worth (int, per-iteration trunc), price==type: 477*3=1431; +20*1 -> 1451
    CHECK(out.worth == 1451.0);
}
TEST(ProdSlots, CollectProduction_RootHighCap) {
    MockHooks h; HookGuard g(&h);
    ProdSlotCollect out;
    // root {477,3} -> EVERY slot uses 5*ROOTlevel+10 = 25 (0x592353).
    std::vector<ProdSlotNode> nodes = {{477, 3}, {10, 2}, {20, 4}};
    int rc = InventoryCollectProductionSlots(nodes, out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out.count, 2);
    CHECK_EQ(out.caps[0], 25);
    CHECK_EQ(out.caps[1], 25);
    CHECK_EQ(out.capTotal, 50);
    CHECK_EQ(out.levTotal, 6);           // 2 + 4
    CHECK(out.worth == 100.0);           // 10*2=20; +20*4 -> 100
}
TEST(ProdSlots, CollectProduction_NoRoot) {
    MockHooks h; HookGuard g(&h);
    ProdSlotCollect out;
    std::vector<ProdSlotNode> nodes;
    CHECK_EQ(InventoryCollectProductionSlots(nodes, out), 0);
    CHECK_EQ(out.count, 0);
}

// ===========================================================================
// (D) TickProductionTimers — timer decrement + completion fire.
// ===========================================================================
TEST(ProdSlots, TimerStillRunning) {
    MockHooks h; HookGuard g(&h);
    h.diff = 30;  // 30 minutes elapsed
    ProductionOrder o;
    o.active = true; o.timerMinutes = 100; o.personId = 7; o.productType = 42;
    GameTime now{};
    ProductionTickResult r = InventoryTickProductionOrder(o, now, true);
    CHECK(r.finished == false);
    CHECK_EQ(o.timerMinutes, 70);  // 100 - 30
    CHECK(o.active == true);
    CHECK_EQ(h.emitCount, 0);
}
TEST(ProdSlots, TimerFinishes_OwnerEmits) {
    MockHooks h; HookGuard g(&h);
    h.diff = 50;
    ProductionOrder o;
    o.active = true; o.timerMinutes = 30; o.personId = 9; o.productType = 42;
    GameTime now{};
    ProductionTickResult r = InventoryTickProductionOrder(o, now, /*ownerTurn*/ true);
    CHECK(r.finished == true);
    CHECK(r.emitted == true);
    CHECK(o.active == false);          // active flag cleared
    CHECK_EQ(o.timerMinutes, -20);     // 30 - 50
    CHECK_EQ(h.emitCount, 1);
    CHECK_EQ(h.lastEmitId, 9);
    CHECK_EQ(h.lastEmitType, static_cast<i16>(42));
}
TEST(ProdSlots, TimerFinishes_NonOwnerNoEmit) {
    MockHooks h; HookGuard g(&h);
    h.diff = 50;
    ProductionOrder o;
    o.active = true; o.timerMinutes = 0; o.personId = 9;
    GameTime now{};
    ProductionTickResult r = InventoryTickProductionOrder(o, now, /*ownerTurn*/ false);
    CHECK(r.finished == true);
    CHECK(r.emitted == false);   // non-owner does not emit the command
    CHECK_EQ(h.emitCount, 0);
    CHECK(o.active == false);
}
TEST(ProdSlots, TimerFinishes_NotifyReady) {
    MockHooks h; HookGuard g(&h);
    h.diff = 10;
    ProductionOrder o;
    o.active = true; o.timerMinutes = 5; o.personId = 3; o.notifyReady = true;
    GameTime now{};
    InventoryTickProductionOrder(o, now, true);
    CHECK_EQ(h.notifyCount, 1);
    CHECK_EQ(h.lastNotifyId, 3);
}
TEST(ProdSlots, TimerInactiveSkipped) {
    MockHooks h; HookGuard g(&h);
    h.diff = 999;
    ProductionOrder o;
    o.active = false; o.timerMinutes = 5;
    GameTime now{};
    ProductionTickResult r = InventoryTickProductionOrder(o, now, true);
    CHECK(r.finished == false);
    CHECK_EQ(o.timerMinutes, 5);  // untouched
}
