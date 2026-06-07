// Unit tests for the remaining deferred building stock/value/customer/create
// rule cores (src/sim/building_stock.*, building_create2.*).  Each test pins one
// translated function's rule on synthetic state with a python-computed golden.
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

// Build a BuildingStockRec with the decay fields set.
BuildingStockRec MakeStock(u16 stock, float decay, float zero, float full,
                           float cap, int delta, u8 kind = 0, u8 active = 1) {
    BuildingStockRec b;
    std::memset(&b, 0, sizeof(b));
    b.marker = 1;
    b.kind = kind;
    b.active = active;
    b.stock = stock;
    b.decayRate = decay;
    b.zeroFill = zero;
    b.fullFill = full;
    b.fillCap = cap;
    b.delta = delta;
    return b;
}

int truncZero(double x) { return static_cast<int>(static_cast<long long>(x)); }

}  // namespace

// --- ComputeProjectedStock 0x57d1c8 (pure) -------------------------------
TEST(SimRemaining, ProjectedStock) {
    // golden via the same recurrence in C++ (oracle): steps from full while >1.
    auto golden = [](u16 stock, float zero, float full, float cap) {
        int steps = 0;
        for (float i = full; i > 1.0f; ++steps)
            i = i - zero + zero / i;
        float v4 = static_cast<float>(stock);
        float v6 = cap - v4;
        float v5 = (v6 <= 0.0f) ? 0.0f : (cap - v4);
        return truncZero(static_cast<double>(steps + stock) + v5);
    };
    BuildingStockRec b = MakeStock(40, 0.0f, 2.0f, 8.0f, 60.0f, 0);
    CHECK_EQ(Building_ComputeProjectedStock(&b), golden(40, 2.0f, 8.0f, 60.0f));

    BuildingStockRec c = MakeStock(70, 0.0f, 1.5f, 5.0f, 60.0f, 0);  // stock>cap
    CHECK_EQ(Building_ComputeProjectedStock(&c), golden(70, 1.5f, 5.0f, 60.0f));
}

// --- ComputeEfficiencyScore 0x57d3d0 (reuse Max/Cur out) -----------------
TEST(SimRemaining, EfficiencyScore) {
    // Build a BuildingRec (169-byte view) the output helpers read.
    BuildingRec b;
    std::memset(&b, 0, sizeof(b));
    b.typeIndex = 5;
    b.objectKind = 0;
    b.activeFlag = 2;          // > 1 so not gated
    b.fillLevel = 30;
    b.outZeroFill = 2.0f;
    b.outFullFill = 10.0f;
    b.fillCap = 60.0f;
    b.outBonus = 1;

    float maxOut = Building_ComputeMaxOutput(&b);
    float curOut = Building_ComputeCurrentOutput(&b);
    float golden = static_cast<float>(maxOut * 0.0010000000474974513f * 0.2
                                      + (curOut / maxOut) * 0.8);
    float got = Building_ComputeEfficiencyScore(&b);
    CHECK(std::fabs(got - golden) < 1e-5f);

    // inactive (active<=1) -> 0.
    b.activeFlag = 1;
    CHECK_EQ(Building_ComputeEfficiencyScore(&b), 0.0f);
}

// --- FindMatchingSupplier 0x57dcb4 (pure Person scan) --------------------
TEST(SimRemaining, FindMatchingSupplier) {
    std::vector<BuildingSaleRec> arr(768);
    std::memset(arr.data(), 0, arr.size() * sizeof(BuildingSaleRec));

    BuildingSaleRec self;
    std::memset(&self, 0, sizeof(self));
    self.kind = 0;    // not 6/7
    self.id = 100;

    // a sale building (kind 6) that lists self (id 100) as a customer, < 5 custs.
    arr[5].kind = 6;
    arr[5].id = 5;
    BPSetCustomerId(&arr[5], 0, 100);   // customer slot @+104 == selfId
    BPSetCustomerId(&arr[5], 1, -1);
    // owner pre-check columns at +92/+96/+100 must NOT equal selfId (else skip).
    { i32 z = 0;
      std::memcpy(reinterpret_cast<u8*>(&arr[5]) + 92, &z, 4);
      std::memcpy(reinterpret_cast<u8*>(&arr[5]) + 96, &z, 4);
      std::memcpy(reinterpret_cast<u8*>(&arr[5]) + 100, &z, 4); }

    const BuildingSaleRec* sup = Building_FindMatchingSupplier(arr.data(), &self);
    CHECK(sup == &arr[5]);

    // self is a sale building -> always nullptr.
    self.kind = 6;
    CHECK(Building_FindMatchingSupplier(arr.data(), &self) == nullptr);

    // no match.
    self.kind = 0;
    self.id = 999;
    CHECK(Building_FindMatchingSupplier(arr.data(), &self) == nullptr);
}

// --- SumWorkstationCount 0x5905dc (pure type-table scan) -----------------
TEST(SimRemaining, SumWorkstationCount) {
    ResetBuildings();
    g_buildingTypesLoaded = true;
    BuildingTypeDef& td = g_buildingTypes[3];
    std::memset(&td, 0, sizeof(td));
    u8* base = reinterpret_cast<u8*>(&td);
    // room words at +35, category bytes at +419, worker counts at +483.
    u16 r0 = 12; std::memcpy(base + 35 + 0, &r0, 2);
    u16 r1 = 7;  std::memcpy(base + 35 + 2, &r1, 2);
    u16 r2 = 9;  std::memcpy(base + 35 + 4, &r2, 2);
    base[419 + 0] = 2; base[483 + 0] = 4;   // cat 2, 4 workers
    base[419 + 1] = 2; base[483 + 1] = 3;   // cat 2, 3 workers
    base[419 + 2] = 5; base[483 + 2] = 9;   // cat 5, not counted

    CHECK_EQ(Building_SumWorkstationCount(3, 2), 7);   // 4 + 3
    CHECK_EQ(Building_SumWorkstationCount(3, 5), 9);
    CHECK_EQ(Building_SumWorkstationCount(3, 1), 0);
}

// --- SumFlaggedSlotsWorth 0x5913e0 + ComputeSalePrice 0x591480 -----------
TEST(SimRemaining, FlaggedSlotsAndSalePrice) {
    ResetBuildings();
    ResetProductionTables();
    ResetStockHooks();
    g_buildingTypesLoaded = true;
    BuildingTypeDef& td = g_buildingTypes[4];
    std::memset(&td, 0, sizeof(td));
    td.roomWorthMul = 2;        // base = 3840 * 2 = 7680
    // one flagged room (bit 0x8000), kind 9.
    td.roomList[0] = static_cast<u16>(0x8000 | 9);
    td.roomList[1] = 0;

    // make ComputeMarketPrice(9,100) return a fixed value via the scene cache.
    g_sceneTypesLoaded = true;
    SceneTypeDef& sc = g_sceneTypes[9];
    std::memset(&sc, 0, sizeof(sc));
    sc.cachedPrice = 100;       // -> price = 32*100*100*kMP_Qty (some constant)
    sc.subtype = 0;

    double mp = Building_ComputeMarketPrice(9, 100);
    int goldenWorth = truncZero(mp + 7680.0);
    CHECK_EQ(Building_SumFlaggedSlotsWorth(4), goldenWorth);

    // sale price: ((4 - taxTier)*0.1 + 0.8) * worth * qmul.  Default taxTier 4.
    int goldenSale = truncZero((static_cast<double>(4 - 4) * 0.10000000149011612f
                                + 0.800000011920929f)
                               * static_cast<double>(goldenWorth) * 1.0f);
    CHECK_EQ(Building_ComputeSalePrice(4, 0), goldenSale);

    // quality 14 -> 0.85 discount.
    int goldenSale14 = truncZero((0.800000011920929f)
                                 * static_cast<double>(goldenWorth) * 0.85000002f);
    CHECK_EQ(Building_ComputeSalePrice(4, 14), goldenSale14);
}

// --- AdjustStockAndNotify 0x57d5b4 (math + gates) ------------------------
TEST(SimRemaining, AdjustStock) {
    ResetStockHooks();
    // storage kind (10) -> -1.
    BuildingStockRec s = MakeStock(10, 0, 1, 5, 20, 0, /*kind*/10);
    CHECK_EQ(Building_AdjustStockAndNotify(&s, 3), -1.0f);

    // inactive -> -1.
    BuildingStockRec t = MakeStock(10, 0, 1, 5, 20, 0, 0, /*active*/0);
    CHECK_EQ(Building_AdjustStockAndNotify(&t, 3), -1.0f);

    // normal: lerp output + delta + add, floored.
    BuildingStockRec b = MakeStock(10, 0, 2.0f, 10.0f, 20.0f, 1, 0, 1);
    double stock = 10.0;
    float v19 = (10.0f - 2.0f) * static_cast<float>(stock) / 20.0f + 2.0f;  // 6.0
    double total = static_cast<double>(1) + v19 + 5.0;
    float golden = (total <= 0.0) ? 0.0f : static_cast<float>(total);
    CHECK(std::fabs(Building_AdjustStockAndNotify(&b, 5) - golden) < 1e-4f);
}

// --- SyncStockLevel 0x57d0f8 projection math -----------------------------
TEST(SimRemaining, SyncStockLevel) {
    ResetStockHooks();
    // newStock <= cap -> returns fullFill.
    BuildingStockRec b = MakeStock(0, 0, 0.5f, 8.0f, 60.0f, 0);
    CHECK(std::fabs(Building_SyncStockLevel(&b, 40) - 8.0f) < 1e-5f);

    // newStock > cap -> project forward (cap=10, newStock=14 -> 4 steps).
    BuildingStockRec c = MakeStock(0, 0, 0.5f, 8.0f, 10.0f, 0);
    int n = 14 - 10;
    float v7 = 8.0f;
    for (int i = 0; i < n; ++i) v7 = v7 - 0.5f + 0.5f / v7;
    CHECK(std::fabs(Building_SyncStockLevel(&c, 14) - v7) < 1e-4f);
}

// --- PickUniqueName 0x586fb8 (name dedup RNG) ----------------------------
TEST(SimRemaining, PickUniqueName) {
    CHECK_EQ(Building_PickUniqueName(0), 0);
    CHECK_EQ(Building_PickUniqueName(1), 0);   // RandNext()%1 == 0
    // determinism: replicate RandNext()%n.
    guild::crt::Srand(12345);
    int n = 5;
    int expect = guild::crt::RandNext() % static_cast<unsigned short>(n);
    guild::crt::Srand(12345);
    CHECK_EQ(Building_PickUniqueName(n), expect);
}

// --- ApplyTypeDefaults 0x586fb8 switch(prot) -----------------------------
TEST(SimRemaining, ApplyTypeDefaults) {
    u8 rec[200];

    // prot 15 -> LABEL_92: +101=10,+105=3,+109=5.
    std::memset(rec, 0, sizeof(rec));
    PackedTime now{};
    Building_ApplyTypeDefaults(rec, 15, 0, -1, now, /*isProduction*/false);
    i32 v;
    std::memcpy(&v, rec + 101, 4); CHECK_EQ(v, 10);
    std::memcpy(&v, rec + 105, 4); CHECK_EQ(v, 3);
    std::memcpy(&v, rec + 109, 4); CHECK_EQ(v, 5);

    // prot 11 -> +101=-1.
    std::memset(rec, 0, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 11, 0, -1, now, false);
    std::memcpy(&v, rec + 101, 4); CHECK_EQ(v, -1);

    // prot 71 -> +48=80000.
    std::memset(rec, 0, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 71, 0, -1, now, false);
    std::memcpy(&v, rec + 48, 4); CHECK_EQ(v, 80000);

    // prot 31 -> +48=100, +73=0.8f bits.
    std::memset(rec, 0, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 31, 0, -1, now, false);
    std::memcpy(&v, rec + 48, 4); CHECK_EQ(v, 100);
    std::memcpy(&v, rec + 73, 4); CHECK_EQ(v, 1061997773);

    // prot 53 -> clear +101/+109.
    std::memset(rec, 0xFF, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 53, 0, -1, now, false);
    std::memcpy(&v, rec + 101, 4); CHECK_EQ(v, 0);
    std::memcpy(&v, rec + 109, 4); CHECK_EQ(v, 0);

    // isProduction -> +48 = 5000 (prot with no other +48 write, e.g. prot 6).
    std::memset(rec, 0, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 6, 0xFFFF, -1, now, /*isProduction*/true);
    std::memcpy(&v, rec + 48, 4); CHECK_EQ(v, 5000);
    // prot 6 ownerWord 0xFFFF -> LABEL_89 +101=-1.
    std::memcpy(&v, rec + 101, 4); CHECK_EQ(v, -1);

    // prot 6 with owner -> +101 = ownerPersonId.
    std::memset(rec, 0, sizeof(rec));
    Building_ApplyTypeDefaults(rec, 6, 2, 777, now, false);
    std::memcpy(&v, rec + 101, 4); CHECK_EQ(v, 777);
}
