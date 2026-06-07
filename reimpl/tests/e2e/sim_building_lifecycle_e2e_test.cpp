// End-to-end flow for the building LIFECYCLE / PRODUCTION / STORAGE module
// (guild::sim) — gilde.exe.
//
// Stands up a production building with a storage room, drives several production
// ticks against a schedule curve (workers supply output via the production
// hook), accumulates the slot output + smoothed input over time, then values the
// resulting stock and verifies it against a hand-computed reference. Finally
// removes the building and confirms the slot-free + counter bookkeeping.
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

// A production hook: each slot's worker output is a fixed per-slot rate; the
// stored quantity ramps up as production accumulates.
struct ProdSim : IProductionHooks {
    int workerOutput = 0;
    int stored = -1;        // -1 == no work stored (yield falls back to market)
    int syncs = 0, randomizes = 0;
    int SlotWorkerOutput(int, int) override { return workerOutput; }
    int SlotStoredQuantity(int, int) override { return stored; }
    void SyncProductionState(int) override { ++syncs; }
    void RandomizeStockTransforms() override { ++randomizes; }
};
}  // namespace

TEST(SimBuildLifecycleE2E, ProduceStoreValueRemove) {
    // ---- World setup: a leaf good (prot 42) priced via the type table. --------
    ResetProductionTables();
    ResetBuildings();
    ResetBuildingPersons();
    g_sceneTypesLoaded = true;

    g_buildingTypes[5] = BuildingTypeDef{};
    g_buildingTypes[5].outputFactor[0] = 4;
    g_buildingTypes[5].outputFactor[1] = 4;
    g_buildingTypes[5].roomWorthMul = 1;
    g_buildingTypes[5].roomList[0] = 60;   // storage room prot
    g_buildingTypes[5].roomList[1] = 0;
    g_buildingTypesLoaded = true;

    // good 42 -> base 8000 / div 100, remaps to building type 5.
    g_sceneTypes[42] = SceneTypeDef{};
    g_sceneTypes[42].baseValue = 8000;
    g_sceneTypes[42].divisor = 100;
    g_sceneTypeRemap[42] = 5;
    // storage room 60 = kind 2.
    g_sceneTypes[60] = SceneTypeDef{};
    g_sceneTypes[60].kind = 2;

    // ---- The production building (index 0) with one active slot for good 42. --
    const int kBuilding = 0;
    SetPlayerBuildingIndex(kBuilding);   // this building recomputes live output
    SetStandaloneFlag(-1);               // standalone -> post-tick sync fires

    ProdBuilding pb = ProdBuildingAt(kBuilding);
    pb.inScale() = 1000;
    pb.outScale() = 1000;
    pb.slotProtPacked(0) = (42 << 16);   // slot 0 produces good 42
    pb.slotActive(0) = 1;
    // slot capacity / output / price bands (slot+0x20/+0x24/+0x38).
    pb.slotSmoothIn(0) = 10.0f;          // capacity band
    pb.slotF24(0) = 40.0f;               // output band
    pb.slotYield(0) = 50.0f;             // current price

    // schedule: input curve ramps (0,0)(10,1000)(20,2000)(30,2000).
    ProdSchedule* sc = ProdScheduleAt(kBuilding);
    sc->hasProduction = 1;
    sc->input[0] = {0, 0};
    sc->input[1] = {10, 1000};
    sc->input[2] = {20, 2000};
    sc->input[3] = {30, 2000};
    sc->output[0] = {0, 100};
    sc->output[1] = {100, 100};

    ProdSim hooks;
    hooks.workerOutput = 80;   // each tick the workers contribute 80 raw output
    hooks.stored = 5;          // 5 units currently in the work slot
    SetProductionHooks(&hooks);

    // ---- Run three production ticks at increasing times. ----------------------
    // Hand reference for the slot OUTPUT (ComputeSlotOutput): activeFlag set,
    // workerOutput 80 -> v13 = trunc(80*0.5)=40; num=slotSmoothIn, den=slotF24.
    //   first tick: smoothIn still 10 -> 40/10*40 = 160; but the tick overwrites
    //   smoothIn AFTER computing output, so we capture the running output.
    SetStandaloneFlag(-1);
    int outputs[3];
    int times[3] = {5, 15, 25};   // nowDay keys hitting different curve segments
    for (int t = 0; t < 3; ++t) {
        Building_RunProductionTick(kBuilding, times[t], 0);
        outputs[t] = pb.slotOutComp(0);
    }

    // Reference: each tick first LERPS smoothIn toward the slot input (building 0
    // -> input=trunc(0.5)=0), then ComputeSlotOutput reads den/num*v13 where
    //   v13 = trunc(workerOutput*0.5) = trunc(80*0.5) = 40,
    //   num = smoothIn (post-lerp), den = slotF24 = 40,
    // then (slot active) smoothIn is forced to the computed output. So:
    //   tick0: smoothIn 10 -> lerp 5; out = 40/5*40 = 320; smoothIn -> 320.
    //   tick1: smoothIn 320 -> lerp 160; out = 40/160*40 = 10; smoothIn -> 10.
    //   tick2: smoothIn 10 -> lerp 5; out = 40/5*40 = 320; smoothIn -> 320.
    CHECK_EQ(outputs[0], 320);
    CHECK_EQ(outputs[1], 10);
    CHECK_EQ(outputs[2], 320);
    CHECK(feq(pb.slotSmoothIn(0), 320.0));

    // standalone post-tick sync fired once per tick.
    CHECK_EQ(hooks.syncs, 3);
    CHECK_EQ(hooks.randomizes, 3);

    // input curve interpolation: nowKey 5 -> mid 500, 15/25 -> end value 2000.
    CHECK_EQ(pb.inValue(), 2000);

    // ---- Value the resulting stock. -------------------------------------------
    struct StoreSim : IStorageHooks {
        std::vector<StorageItem> items;
        std::vector<StorageItem> StorageItems(const BuildingRec*) override { return items; }
        std::vector<StorageItem> RoomItems(const BuildingRec*, i16) override { return items; }
        i32 OwnerWealth(i32) override { return 50000; }
    } store;
    // the building produced & stored 30 units of good 42.
    store.items.push_back(StorageItem{42, 100, 30});
    SetStorageHooks(&store);

    BuildingRec brec{};
    brec.typeIndex = 5;

    // Warm the market-price cache so the value refs all use the cached price.
    Building_ComputeMarketPrice(42, 100);            // recompute + cache
    double mp = Building_ComputeMarketPrice(42, 100); // cached
    int sumWorth = BuildingValue_SumStorageItemWorth(&brec);
    CHECK_EQ(sumWorth, (int)mp);

    // stock value: ownerShare = clamp(50000)*0.04 = 2000;
    //   stockWorth = trunc(price(=863.99) * max(1, trunc(30*0.3=8.999)=8)) =
    //   trunc(6911.99) = 6911 (hand-computed reference).
    StockValue sv = BuildingValue_ComputeStockValue(&brec, 1, 11);
    CHECK_EQ(sv.ownerShare, 2000);
    CHECK_EQ(sv.stockWorth, 6911);
    CHECK(mp > 863.0 && mp < 864.0);

    // room worth: v19 = 3840*1 + price; result = mul*0.01*v19.
    int v19 = 3840 + (int)mp;
    int rw = BuildingValue_ComputeRoomWorth(&brec, 5);
    CHECK_EQ(rw, (int)((double)5 * 0.009999999776482582 * (double)v19));
    CHECK(mp > 0.0);

    SetStorageHooks(nullptr);

    // ---- Tear down: remove the building, freeing its person slot. -------------
    SetBuildCounter(1);
    struct LifeSim : ILifecycleHooks {
        int freed = 0;
        void FreeChildList(int) override { ++freed; }
    } life;
    SetLifecycleHooks(&life);

    BuildingPersonRec* r = BuildingPersonAt(0);
    r->marker = 0x0005;
    r->kind = 6;
    r->activeFlag = 1;

    Building_RemoveAndCleanup(0, /*freeSlot*/ true);
    CHECK_EQ((int)r->kind, 15);
    CHECK_EQ((int)r->marker, -1);
    CHECK_EQ(BuildCounter(), 0);
    CHECK_EQ(life.freed, 1);

    SetLifecycleHooks(nullptr);
    SetProductionHooks(nullptr);
}
