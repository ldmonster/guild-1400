#include "test.h"

#include <cstring>
#include <vector>

#include "sim/production_slots.h"
#include "sim/building.h"                 // real g_buildingTypes / BuildingTypeDef
#include "sim/building_production.h"      // real Building_ComputeMarketPrice / scene tables
#include "sim/inventory.h"               // real InventoryGetSlotCapacity (capacity oracle)

using namespace guild;
using namespace guild::sim;

// Cross-module integration: drive the production-slot rules against REAL sibling
// state — the actual 589-stride g_buildingTypes table (weapon-compat), the real
// SceneTypeDef-driven Building_ComputeMarketPrice price model (worth math via the
// DEFAULT hook), and the real InventoryGetSlotCapacity table as a capacity oracle.

namespace {
// Helper: set an ItemRec's unaligned +0x0E count and read its slot capacity via
// the real inventory.cpp accessor (the table CollectProductionSlots inlines).
int RealSlotCapacity(i16 type, i32 level) {
    ItemRec rec{};
    rec.type = type;
    // count dword lives at +0x0E (unaligned) — write it the way inventory.cpp reads.
    std::memcpy(reinterpret_cast<unsigned char*>(&rec) + 0x0E, &level, sizeof(level));
    return InventoryGetSlotCapacity(&rec);
}
}  // namespace

// ===========================================================================
// Weapon compatibility against the REAL g_buildingTypes table.
// ===========================================================================
TEST(ProdSlotsItest, WeaponCompatRealTypeTable) {
    g_buildingTypes[12].kind = 4;    // a ranged-weapon type
    g_buildingTypes[13].kind = 19;   // a shield type
    g_buildingTypes[14].kind = 16;   // an armour type
    g_buildingTypesLoaded = true;

    // Ranged slot 370 requires kind 4.
    CHECK(InventoryIsWeaponSlotCompatible(true, 12, 370, true) == true);
    CHECK(InventoryIsWeaponSlotCompatible(true, 13, 370, true) == false);
    // Shield slot 344 requires kind 19.
    CHECK(InventoryIsWeaponSlotCompatible(true, 13, 344, true) == true);
    CHECK(InventoryIsWeaponSlotCompatible(true, 12, 344, true) == false);
    // Armour slot 374 requires kind 16.
    CHECK(InventoryIsWeaponSlotCompatible(true, 14, 374, true) == true);
    CHECK(InventoryIsWeaponSlotCompatible(true, 12, 374, true) == false);
}

// ===========================================================================
// CollectProductionSlots capacity totals must agree, slot-for-slot, with the
// REAL InventoryGetSlotCapacity table when the root node is not high-cap.
// ===========================================================================
TEST(ProdSlotsItest, CapacityMatchesRealInventoryTable) {
    // Use the default hooks so worth runs through the REAL market price model.
    SetProductionSlotHooks(nullptr);
    // Make sure the scene type table is in a defined (empty) state so the price
    // model is deterministic for this run.
    ResetProductionTables();

    std::vector<ProdSlotNode> nodes = {{100, 1}, {101, 2}, {102, 3}, {103, 5}};
    ProdSlotCollect out;
    int rc = InventoryCollectProductionSlots(nodes, out);
    CHECK_EQ(rc, 1);
    CHECK_EQ(out.count, 4);

    // Root type 100 != 477 -> each slot's capacity == real per-level table.
    int expectTotal = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        int want = RealSlotCapacity(nodes[i].type, nodes[i].level);
        // CollectProductionSlots keys capacity on the ROOT type; for a non-highcap
        // root that is exactly the per-level branch, matching the table for any type.
        CHECK_EQ(out.caps[i], want);
        expectTotal += want;
    }
    CHECK_EQ(out.capTotal, expectTotal);
    CHECK_EQ(out.levTotal, 1 + 2 + 3 + 5);
}

// ===========================================================================
// Worth runs through the REAL Building_ComputeMarketPrice (default hook). The
// model returns >= 0 and the accumulated worth equals sum(price*level).
// ===========================================================================
TEST(ProdSlotsItest, WorthViaRealPriceModel) {
    SetProductionSlotHooks(nullptr);     // default hook -> real price model
    ResetProductionTables();

    std::vector<ProdSlotNode> nodes = {{200, 2}, {201, 3}};
    ProdSlotCollect out;
    InventoryCollectProductionSlots(nodes, out);

    double want = Building_ComputeMarketPrice(200, 100) * 2.0
                + Building_ComputeMarketPrice(201, 100) * 3.0;
    // exact: same calls, same operands.
    CHECK(out.worth == want);
}
