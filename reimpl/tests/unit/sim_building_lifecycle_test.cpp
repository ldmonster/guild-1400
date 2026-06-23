// Unit tests for the building LIFECYCLE / PRODUCTION-SLOT / STORAGE module
// (guild::sim) — gilde.exe.
//
// Covers: the production-slot table layout + slot accessors, the schedule-curve
// interpolation inside RunProductionTick (golden vectors), the type-table-driven
// market price (recompute + cache), the value aggregators (stock value, storage
// item worth, room worth), the building remove/cleanup slot-free path, and the
// find-nearest-same-type query.
#include "test.h"

#include "sim/building.h"
#include "sim/building_production.h"
#include "sim/building_storage.h"
#include "sim/building_lifecycle.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {
bool feq(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

// Configure a leaf scene type-def the way ComputeMarketPrice expects (no cache,
// remap to a building type with two output factors).
void SetupLeafGood(int prot, i32 baseValue, u16 divisor, u8 remapType) {
    g_sceneTypes[prot] = SceneTypeDef{};
    g_sceneTypes[prot].kind = 0;
    g_sceneTypes[prot].subtype = 0;
    g_sceneTypes[prot].baseValue = baseValue;
    g_sceneTypes[prot].divisor = divisor;
    g_sceneTypes[prot].cachedPrice = 0;
    g_sceneTypes[prot].compType = 0;     // no components
    g_sceneTypeRemap[prot] = remapType;
}
}  // namespace

// --- Production-slot table geometry: byte-exact 1988-dword / 32-dword strides. -
TEST(SimBuildLifecycle, SlotTableLayout) {
    ResetProductionTables();
    // The per-building stride is 1988 dwords; slot stride 32 dwords; slot[0]
    // begins at dword 24 (record +0x60). Verify the column addressing is exact.
    ProdBuilding b0 = ProdBuildingAt(0);
    ProdBuilding b1 = ProdBuildingAt(1);
    // header inValue is record +0x50 == dword 20.
    b0.inValue() = 1234;
    CHECK_EQ(g_prodStore[20], 1234);
    // slot[0] prot column is record +0x60 == dword 24.
    b0.slotProtPacked(0) = 0x00070000;   // prot 7 in high word
    CHECK_EQ(g_prodStore[24], 0x00070000);
    // slot[1] prot is dword 24+32 == 56.
    b0.slotProtPacked(1) = 0x00080000;
    CHECK_EQ(g_prodStore[56], 0x00080000);
    // building 1's header inValue is dword 1988+20.
    b1.inValue() = 99;
    CHECK_EQ(g_prodStore[1988 + 20], 99);
    // activeFlag is the low byte of column +0xA0 (dword 40) of the slot.
    b0.slotActive(0) = 1;
    CHECK_EQ((int)(g_prodStore[40] & 0xFF), 1);
}

// --- Market price: recompute then cache. ---------------------------------------
TEST(SimBuildLifecycle, MarketPriceRecomputeThenCache) {
    ResetProductionTables();
    g_sceneTypesLoaded = true;
    // type 5 = production building with outputFactor [4,4].
    ResetBuildings();
    g_buildingTypes[5] = BuildingTypeDef{};
    g_buildingTypes[5].outputFactor[0] = 4;
    g_buildingTypes[5].outputFactor[1] = 4;
    g_buildingTypesLoaded = true;

    SetupLeafGood(42, /*baseValue*/ 8000, /*divisor*/ 100, /*remap*/ 5);

    // first call recomputes + caches 27 (golden), returns 876.0888.
    double p1 = Building_ComputeMarketPrice(42, 100);
    CHECK(feq(p1, 876.0888671875, 1e-2));
    CHECK_EQ(g_sceneTypes[42].cachedPrice, 27);
    // second call uses the cache: 32*27*100*0.01 == 864 (golden).
    double p2 = Building_ComputeMarketPrice(42, 100);
    CHECK(feq(p2, 863.99998, 1e-2));
    // subtype 3 multiplies the cached price by 1.5.
    g_sceneTypes[42].subtype = 3;
    double p3 = Building_ComputeMarketPrice(42, 100);
    CHECK(feq(p3, 863.99998 * 1.5, 1e-1));
}

// --- Schedule-curve interpolation (golden) via the production tick. ------------
TEST(SimBuildLifecycle, ProductionTickInterpolation) {
    ResetProductionTables();
    g_sceneTypesLoaded = true;

    ProdSchedule* sc = ProdScheduleAt(0);
    sc->hasProduction = 1;
    // 4-key input curve: (2,100)(10,300)(20,500)(30,900) — active count 3.
    const ScheduleKey in[4] = {{2,100},{10,300},{20,500},{30,900}};
    for (int i = 0; i < 4; ++i) sc->input[i] = in[i];
    // output curve flat at value 1000 across one segment.
    sc->output[0] = {0,1000}; sc->output[1] = {100,1000};

    // input scale 1000 (per-mille pass-through), output scale 1000.
    ProdBuilding pb = ProdBuildingAt(0);
    pb.inScale() = 1000;
    pb.outScale() = 1000;
    // no production slots active (all prot 0).
    SetPlayerBuildingIndex(-1);
    SetStandaloneFlag(0);   // skip post-tick sync hooks

    // nowKey=5, dayByte=0 -> 'mid' segment -> 175 (golden).
    Building_RunProductionTick(0, /*nowDay*/ 5, /*nowMinute*/ 0);
    CHECK_EQ(pb.inValue(), 175);

    // nowKey=2, dayByte=6 -> 'eq' interp -> 112 (golden).
    pb.inScale() = 1000;
    Building_RunProductionTick(0, 2, 6);
    CHECK_EQ(pb.inValue(), 112);

    // nowKey=0 -> 'k0' -> 100.
    pb.inScale() = 1000;
    Building_RunProductionTick(0, 0, 0);
    CHECK_EQ(pb.inValue(), 100);

    // nowKey beyond last segment -> end value 900.
    pb.inScale() = 1000;
    Building_RunProductionTick(0, 25, 0);
    CHECK_EQ(pb.inValue(), 900);

    // input scale 500 (per-mille) halves the value: 175 -> 87.
    pb.inScale() = 500;
    Building_RunProductionTick(0, 5, 0);
    CHECK_EQ(pb.inValue(), 87);
}

// --- FindSlotByProt / RecalcAllProduction. -------------------------------------
TEST(SimBuildLifecycle, FindSlotByProtAndRecalc) {
    ResetProductionTables();
    g_sceneTypesLoaded = true;
    ProdBuilding pb = ProdBuildingAt(0);
    pb.slotProtPacked(3) = (475 << 16);
    i32* slot = Building_FindSlotByProt(0, 475);
    CHECK(slot == &pb.slotFieldC(3));
    CHECK(Building_FindSlotByProt(0, 999) == nullptr);

    // RecalcAllProduction walks while hasProduction is set (up to 4).
    ProdScheduleAt(0)->hasProduction = 1;
    ProdScheduleAt(1)->hasProduction = 1;
    ProdScheduleAt(2)->hasProduction = 0;
    for (int b = 0; b < 2; ++b) {
        ProdScheduleAt(b)->input[0] = {0, 100};
        ProdScheduleAt(b)->input[1] = {10, 100};
        ProdBuildingAt(b).inScale() = 1000;
    }
    SetStandaloneFlag(0);
    Building_RecalcAllProduction(5, 0);
    CHECK_EQ(ProdBuildingAt(0).inValue(), 100);
    CHECK_EQ(ProdBuildingAt(1).inValue(), 100);
    // building 2 was not flagged -> untouched (0).
    CHECK_EQ(ProdBuildingAt(2).inValue(), 0);
}

// --- Stock value + storage item worth (hooked scene-item walk). ---------------
namespace {
struct MockStorage : IStorageHooks {
    std::vector<StorageItem> items;
    i32 wealth = 0;
    std::vector<StorageItem> StorageItems(const BuildingRec*) override { return items; }
    std::vector<StorageItem> RoomItems(const BuildingRec*, i16) override { return items; }
    i32 OwnerWealth(i32) override { return wealth; }
};
}  // namespace

TEST(SimBuildLifecycle, StockValueAndStorageWorth) {
    ResetProductionTables();
    ResetBuildings();
    g_sceneTypesLoaded = true;
    g_buildingTypes[5] = BuildingTypeDef{};
    g_buildingTypes[5].outputFactor[0] = 4;
    g_buildingTypes[5].outputFactor[1] = 4;
    g_buildingTypesLoaded = true;
    SetupLeafGood(42, 8000, 100, 5);

    MockStorage hooks;
    hooks.wealth = 100000;
    hooks.items.push_back(StorageItem{42, 100, /*count*/ 50});
    SetStorageHooks(&hooks);

    BuildingRec b{};
    b.typeIndex = 5;

    // Warm the market-price cache so every measured call uses the cached value
    // (first call recomputes 876 + caches 27; thereafter returns ~863.9999 which
    // truncates to 863).
    Building_ComputeMarketPrice(42, 100);            // warm: recompute + cache
    double mp = Building_ComputeMarketPrice(42, 100); // measured: cached
    CHECK_EQ((int)mp, 863);

    // SumStorageItemWorth: cached market price for the one stored item, truncated.
    int worth = BuildingValue_SumStorageItemWorth(&b);
    CHECK_EQ(worth, (int)mp);

    // ComputeStockValue: ownerShare = clamp(100000,2.56e6)*0.04 (kind!=2) = 4000.
    //   stockWorth = trunc(price(=863.99) * max(1, trunc(50*0.3=14.999)=14)) =
    //   trunc(12095.99) = 12095 (hand-computed; the binary-0.3 rounding floors
    //   the count multiplier to 14).
    StockValue sv = BuildingValue_ComputeStockValue(&b, /*ownerId*/ 1, /*kind*/ 11);
    CHECK_EQ(sv.ownerShare, 4000);
    CHECK_EQ(sv.stockWorth, 12095);

    SetStorageHooks(nullptr);
}

// --- Room worth: storage rooms accumulate item value, scaled by mul & roomMul. -
TEST(SimBuildLifecycle, RoomWorth) {
    ResetProductionTables();
    ResetBuildings();
    g_sceneTypesLoaded = true;
    g_buildingTypes[5] = BuildingTypeDef{};
    g_buildingTypes[5].outputFactor[0] = 4;
    g_buildingTypes[5].outputFactor[1] = 4;
    g_buildingTypes[5].roomWorthMul = 2;     // base = 3840*2 = 7680
    g_buildingTypes[5].roomList[0] = 60;     // one room prot
    g_buildingTypes[5].roomList[1] = 0;
    g_buildingTypesLoaded = true;
    // room 60 = storage kind 2; item 42 inside.
    g_sceneTypes[60] = SceneTypeDef{};
    g_sceneTypes[60].kind = 2;
    SetupLeafGood(42, 8000, 100, 5);

    MockStorage hooks;
    hooks.items.push_back(StorageItem{42, 100, 10});
    SetStorageHooks(&hooks);

    BuildingRec b{};
    b.typeIndex = 5;
    // Warm the cache, then v19 = 3840*2=7680 + cached price(42@100). The room
    // item's qtyByte (100) drives the market-price arg. result = mul*0.01*v19.
    Building_ComputeMarketPrice(42, 100);   // warm cache -> 864
    double price = Building_ComputeMarketPrice(42, 100);
    int v19 = 7680 + (int)price;
    int expected = (int)((double)10 * 0.009999999776482582 * (double)v19);
    CHECK_EQ(BuildingValue_ComputeRoomWorth(&b, 10), expected);

    SetStorageHooks(nullptr);
}

// --- Remove/cleanup frees the person slot + decrements the build counter. ------
namespace {
struct MockLifecycle : ILifecycleHooks {
    int released = 0, freedChild = 0, destroyedChar = 0, decremented = 0;
    int routesCleared = 0, rivalNotified = 0;
    float dist = 0.0f;
    void ReleaseOccupantHoldings(int) override { ++released; }
    int ClearTradeRoutes(i32) override { return routesCleared; }
    void NotifyRivalEvent(int, int) override { ++rivalNotified; }
    void FreeChildList(int) override { ++freedChild; }
    void DestroyCharacter(i32) override { ++destroyedChar; }
    void DecrementTypeActiveCount(i32) override { ++decremented; }
    float BuildingDistanceSq(int, int o) override { return dist * (o + 1); }
};
}  // namespace

TEST(SimBuildLifecycle, RemoveAndCleanupFreesSlot) {
    ResetBuildingPersons();
    SetBuildCounter(3);
    MockLifecycle hooks;
    hooks.routesCleared = 1;   // triggers rival notify
    SetLifecycleHooks(&hooks);

    BuildingPersonRec* r = BuildingPersonAt(10);
    r->marker = 0x0042;        // alive (raw type 0x42 in low byte)
    r->kind = 6;               // not destroyed
    r->activeFlag = 1;
    r->charHandle = 555;
    r->typeRecord = 0x1000;

    Building_RemoveAndCleanup(10, /*freeSlot*/ true);

    CHECK_EQ((int)r->kind, 15);          // marked destroyed
    CHECK_EQ((int)r->activeFlag, 0);
    CHECK_EQ((int)r->marker, -1);        // slot freed
    CHECK_EQ(r->charHandle, 0);
    CHECK_EQ(r->typeRecord, 0);
    CHECK_EQ(hooks.released, 1);
    CHECK_EQ(hooks.rivalNotified, 1);
    CHECK_EQ(hooks.freedChild, 1);
    CHECK_EQ(hooks.destroyedChar, 1);
    CHECK_EQ(hooks.decremented, 1);
    CHECK_EQ(BuildCounter(), 2);         // decremented from 3

    // idempotent: a slot already at kind 15 skips the destroy phase.
    hooks.released = 0;
    Building_RemoveAndCleanup(10, /*freeSlot*/ false);
    CHECK_EQ(hooks.released, 0);

    SetLifecycleHooks(nullptr);
}

// --- FindNearestSameType picks the closest matching non-production building. ---
TEST(SimBuildLifecycle, FindNearestSameType) {
    ResetBuildingPersons();
    ResetBuildings();
    // type 0x20 => kind 4 (some non-production, non-storage category).
    g_buildingTypes[0x20] = BuildingTypeDef{};
    g_buildingTypes[0x20].kind = 4;
    // type 0x30 => kind 11 (production) — must be skipped.
    g_buildingTypes[0x30] = BuildingTypeDef{};
    g_buildingTypes[0x30].kind = 11;
    g_buildingTypesLoaded = true;

    MockLifecycle hooks;
    hooks.dist = 5.0f;          // distSq = 5*(otherSlot+1)
    SetLifecycleHooks(&hooks);

    // self at slot 0 (raw type 0x20).
    BuildingPersonAt(0)->marker = 0x0020;
    BuildingPersonAt(0)->kind = 4;
    // candidate slot 1 (type 0x20, kind 4) — closest (distSq=5*2=10).
    BuildingPersonAt(1)->marker = 0x0020;
    BuildingPersonAt(1)->kind = 4;
    // candidate slot 2 (type 0x20, kind 4) — farther (distSq=5*3=15).
    BuildingPersonAt(2)->marker = 0x0020;
    BuildingPersonAt(2)->kind = 4;
    // candidate slot 3 (type 0x30 production) — skipped.
    BuildingPersonAt(3)->marker = 0x0030;
    BuildingPersonAt(3)->kind = 11;

    int best = Building_FindNearestSameType(0, /*typeCode*/ 4);
    CHECK_EQ(best, 1);

    // typeCode that no candidate's kind matches -> 0.
    CHECK_EQ(Building_FindNearestSameType(0, 9), 0);

    SetLifecycleHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Wave-12 hardening: roomList walks at the 64-entry boundary, bad type, and
// out-of-range person-slot indices. ASAN+UBSAN must stay clean.
// ---------------------------------------------------------------------------
TEST(SimBuildLifecycleHarden, RoomWorthFullUnterminatedList) {
    ResetProductionTables();
    ResetBuildings();
    g_sceneTypesLoaded = true;
    g_buildingTypes[6] = BuildingTypeDef{};
    g_buildingTypes[6].roomWorthMul = 1;
    // Fill ALL 64 room entries with a non-storage prot (no terminating 0). The
    // walk must stop at index 63 and never read roomList[64] (OOB).
    for (int i = 0; i < 64; ++i) g_buildingTypes[6].roomList[i] = 50; // kind 0
    g_buildingTypesLoaded = true;
    g_sceneTypes[50] = SceneTypeDef{};
    g_sceneTypes[50].kind = 0;     // not storage -> no item walk

    MockStorage hooks;             // empty
    SetStorageHooks(&hooks);
    BuildingRec b{}; b.typeIndex = 6;
    // No storage rooms -> v19 stays 3840*1; result = mul*0.01*3840.
    int v19 = 3840 * 1;
    int expected = (int)((double)5 * 0.009999999776482582 * (double)v19);
    CHECK_EQ(BuildingValue_ComputeRoomWorth(&b, 5), expected);
    SetStorageHooks(nullptr);
}

TEST(SimBuildLifecycleHarden, RoomWorthUnloadedType) {
    ResetProductionTables();
    ResetBuildings();
    g_buildingTypesLoaded = false;     // table unloaded -> td null
    BuildingRec b{}; b.typeIndex = 200;
    CHECK_EQ(BuildingValue_ComputeRoomWorth(&b, 10), 0);   // no OOB on bad type
}

TEST(SimBuildLifecycleHarden, PersonSlotIndexBounds) {
    ResetBuildingPersons();
    // out-of-range slot indices -> nullptr, no OOB access.
    CHECK(BuildingPersonAt(-1) == nullptr);
    CHECK(BuildingPersonAt(kBuildingSlots) == nullptr);
    CHECK(BuildingPersonAt(kBuildingSlots + 1000) == nullptr);
    CHECK(BuildingPersonAt(0) != nullptr);
    CHECK(BuildingPersonAt(kBuildingSlots - 1) != nullptr);

    // Remove/cleanup on an out-of-range slot is a safe no-op.
    Building_RemoveAndCleanup(-5, true);
    Building_RemoveAndCleanup(kBuildingSlots + 1, true);
    // FindNearestSameType on an out-of-range self slot -> 0 (self null).
    CHECK_EQ(Building_FindNearestSameType(kBuildingSlots, 4), 0);
}
