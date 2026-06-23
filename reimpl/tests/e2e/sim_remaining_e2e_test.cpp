// E2E for the remaining deferred building rule cores: run a production-worth +
// customer-distribution pass and a building-create pass on synthetic state, and
// verify the produced stock/worth/object state and the emitted commands against
// a hand-computed reference.
#include "tests/framework/test.h"

#include "sim/building_stock.h"
#include "sim/building_create2.h"
#include "sim/building.h"
#include "sim/building_value.h"
#include "sim/building_production.h"
#include "sim/types.h"

#include "crt/rand.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// A spy that records every command the rule cores emit, plus serves the customer
// lookups for the distribution pass.
struct SpyStockHooks : IStockHooks {
    struct PriceUpd { i32 id; int field; float value; };
    std::vector<PriceUpd> priceUpdates;
    std::vector<i32>      finalised;
    int                   transfers = 0;
    int                   transferStatus = 0;   // 0 == success
    std::vector<const BuildingSaleRec*> resolveTable;  // id -> rec (by index)

    int QueuePriceUpdate(i32 id, int field, float v) override {
        priceUpdates.push_back({id, field, v});
        return 1;
    }
    int EnqueueGoodsTransfer(i32, i32, i32, int) override {
        ++transfers;
        return transferStatus;
    }
    void FinaliseCustomerUpdate(i32 id) override { finalised.push_back(id); }
    const BuildingSaleRec* ResolveOwnerRecord(i32 id) override {
        for (auto* r : resolveTable)
            if (r && r->id == id) return r;
        return nullptr;
    }
};

}  // namespace

// ---- E2E 1: a production-worth + customer-distribution pass --------------
TEST(SimRemainingE2E, ProductionAndDistribution) {
    ResetStockHooks();
    ResetBuildings();
    SpyStockHooks spy;
    SetStockHooks(&spy);

    g_buildingTypesLoaded = true;   // ComputeItemBaseValue needs a loaded table
    // The per-kind worth columns in ComputeProductionWorth @0x58fe68 gate on the
    // TYPE-DEF +0 kind byte (*v40 = 589*type + base), NOT the building record's +2
    // object kind. Set the typedef kind = 7 so the kind-7 (auction) path runs.
    std::memset(&g_buildingTypes[0], 0, sizeof(g_buildingTypes[0]));
    g_buildingTypes[0].kind = 7;

    // --- production worth on a kind-7 (auction) building ---
    BuildingSaleRec bld;
    std::memset(&bld, 0, sizeof(bld));
    bld.marker = 1;
    bld.kind = 7;
    bld.id = 50;
    bld.quality = 100;       // +61
    bld.worth77 = 11;        // a2[18]
    bld.worth101 = 13;       // a2[13]
    bld.worth105 = 17;       // a2[14]

    ProductionWorth w = BuildingValue_ComputeProductionWorth(&bld, /*type*/0);
    // a2[18] mirrors +77; a2[13]/[14] mirror +101/+105 for kind 7; totals coherent.
    CHECK_EQ(w.col[18], 11);
    CHECK_EQ(w.col[13], 13);
    CHECK_EQ(w.col[14], 17);
    // col[16] == production sub-total (col[3]); col[17] == running total; col[20]
    // == col[17]-col[16].
    CHECK_EQ(w.col[16], w.col[3]);
    CHECK_EQ(w.col[20], w.col[17] - w.col[16]);
    // kind 7 added worth101+worth105 (=30) to the running total beyond production.
    CHECK_EQ(w.col[17] - w.col[16], 13 + 17);

    // --- customer distribution: high price-drift path posts a price update +
    //     a goods transfer, then finalises the customer ---
    BuildingSaleRec seller;
    std::memset(&seller, 0, sizeof(seller));
    seller.marker = 1;
    seller.kind = 4;        // < 10, not storage
    seller.id = 60;
    seller.active = 1;
    seller.state = 1;
    seller.flag458 = 0;
    seller.sceneNodeId = 61;            // customer id
    float p = 5.0f;                      // price > 1.0 -> high path
    std::memcpy(&seller.price, &p, 4);

    BuildingSaleRec customer;
    std::memset(&customer, 0, sizeof(customer));
    customer.marker = 1;
    customer.kind = 7;
    customer.id = 61;
    customer.active = 1;
    BPSetCustomerId(&customer, 0, -1);  // no filled customer slots -> count path
    spy.resolveTable.push_back(&customer);

    guild::crt::Srand(999);
    Building_DistributeGoodsToCustomers(&seller);

    // a price update on field 124 was queued for the seller.
    bool sawPrice124 = false;
    for (auto& u : spy.priceUpdates)
        if (u.id == 60 && u.field == 124) sawPrice124 = true;
    CHECK(sawPrice124);
    // a goods transfer was posted and the customer finalised (status 0 success).
    CHECK_EQ(spy.transfers, 1);
    CHECK(!spy.finalised.empty());
    CHECK_EQ(spy.finalised.back(), 61);

    SetStockHooks(nullptr);
}

// ---- E2E 2: a building-create pass (slot stamp + per-type defaults) -------
TEST(SimRemainingE2E, BuildingCreate) {
    ResetCreateHooks();

    // Synthetic record buffer modelling a freshly-stamped building slot.
    u8 rec[200];
    std::memset(rec, 0, sizeof(rec));
    rec[0] = 30;                 // type byte = 30 (farm/plant -> plant map)

    // install a plant-map allocator (the scene/memory leaf).
    struct MapHooks : ICreateHooks {
        u8 buf[0x600];
        MapHooks() { std::memset(buf, 0, sizeof(buf)); }
        u8* AllocPlantMap() override { return buf; }
    } mh;
    SetCreateHooks(&mh);

    PackedTime now{};
    now.dayBlock = 7;
    now.hour = 12;
    now.minuteByte = 30;
    now.cursor = 5;

    bool changed = Building_ApplyTypeDefaults(rec, 30, 0xFFFF, -1, now,
                                              /*isProduction*/true);
    CHECK(changed);

    // prot 30: plant-map ptr at +113, +48=100, +73=0.8f bits; production -> +48
    //   is overwritten to 5000 first, then prot-30 sets it to 100.
    i32 v;
    std::memcpy(&v, rec + 48, 4);  CHECK_EQ(v, 100);
    std::memcpy(&v, rec + 73, 4);  CHECK_EQ(v, 1061997773);
    std::memcpy(&v, rec + 113, 4); CHECK(v != 0);   // plant map ptr installed

    // the plant map records were initialised: byte +13 == -1, dword +20 == 0.
    CHECK_EQ(mh.buf[13], 0xFF);
    CHECK_EQ(mh.buf[24 + 13], 0xFF);
    std::memcpy(&v, mh.buf + 20, 4); CHECK_EQ(v, 0);

    // type-38 epilogue: +41 = 270.
    std::memset(rec, 0, sizeof(rec));
    rec[0] = 38;
    Building_ApplyTypeDefaults(rec, 38, 0, -1, now, false);
    u16 w; std::memcpy(&w, rec + 41, 2); CHECK_EQ(w, 270);

    // name dedup pick is deterministic vs the RNG reference.
    guild::crt::Srand(4242);
    int ref = guild::crt::RandNext() % static_cast<unsigned short>(7);
    guild::crt::Srand(4242);
    CHECK_EQ(Building_PickUniqueName(7), ref);

    SetCreateHooks(nullptr);
}
