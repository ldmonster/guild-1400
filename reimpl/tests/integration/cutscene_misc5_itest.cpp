// Integration test: the scene-sync building dispatch is wired against the REAL
// reconstructed sibling VIBE_Building_MapTypeToCategory (guild::sim::
// Building_MapTypeToCategory, gilde.exe 0x5878b0) — no mock. This is exactly the
// live wiring: VIBE_Scene_SyncBuildingEntrance (0x5023b8) and
// VIBE_Scene_ComputeProductionTickRate (0x502198) both call
// Building_MapTypeToCategory(*a1) to classify the building before deciding the
// fee path / production loop. We forward the scene-sync hook into the genuine
// record-driven category mapper (resolving the real type table) and assert the
// cross-module classification the binary would produce.
#include "test.h"

#include "sim/cutscene_misc5.h"
#include "sim/building.h"
#include "sim/building_types.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// Populate the REAL shared building type table (sim/building.cpp's
// g_buildingTypes / g_buildingTypesLoaded) so the genuine
// Building_MapTypeToCategory resolves each type byte through it. We set the
// `kind` byte per type index so the real verbatim switch maps it to a known
// category: kind 7 -> category 4 ("special"); kind 8 -> category 1
// ("production"); kind 10 -> category 0 (storage); kind 4 -> category 8.
void InstallTypes() {
    std::memset(g_buildingTypes, 0, sizeof(g_buildingTypes));
    g_buildingTypes[7].kind  = 7;   // -> category 4
    g_buildingTypes[8].kind  = 8;   // -> category 1
    g_buildingTypes[16].kind = 10;  // storage kind -> category 0
    g_buildingTypes[9].kind  = 4;   // -> category 8 (type byte 9)
    g_buildingTypesLoaded = true;
}

// The scene-sync hook forwarded into the REAL sibling, exactly as the binary
// wires it: the scene functions pass the building type byte (*a1) and the
// mapper resolves it through the type table to a display category.
int RealMapTypeToCategory(u8 typeByte) {
    BuildingRec rec{};
    rec.typeIndex = typeByte;
    return Building_MapTypeToCategory(&rec);
}

int g_lastCat = -999;
int RecordingMap(u8 typeByte) {
    g_lastCat = RealMapTypeToCategory(typeByte);
    return g_lastCat;
}
}  // namespace

TEST(CutsceneMisc5Itest, RealCategoryMapperDrivesEntranceClassification) {
    InstallTypes();

    SceneSyncHooks h{};
    h.mapTypeToCategory = RecordingMap;
    SetSceneSyncHooks(&h);

    // Type byte 9 (Bauplatz): the entrance queues the build fee (product 310)
    // regardless of category, but the category is still computed via the real
    // sibling — assert the genuine mapper ran and produced kind-4's category 8.
    int p = SceneSyncBuildingEntrance(/*typeByte=*/9, /*objId=*/1, /*rate=*/0);
    CHECK_EQ(p, 310);
    CHECK_EQ(g_lastCat, 8);   // kind 4 -> category 8 from the REAL mapper

    // Ordinary building type 7 -> kind 7 -> category 4, standard fee 308.
    int p2 = SceneSyncBuildingEntrance(/*typeByte=*/7, /*objId=*/2, /*rate=*/0);
    CHECK_EQ(p2, 308);
    CHECK_EQ(g_lastCat, 4);   // kind 7 -> category 4

    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5Itest, RealMapperCrossCheckGrid) {
    InstallTypes();
    // Cross-check the kernel's view of "production category" against the genuine
    // record-driven mapper across the populated type bytes.
    struct { u8 typeByte; int wantCat; } cases[] = {
        {7, 4}, {8, 1}, {16, 0}, {9, 8},
    };
    for (auto& c : cases) {
        int cat = RealMapTypeToCategory(c.typeByte);
        CHECK_EQ(cat, c.wantCat);
        // The scene production loop only runs for categories 1 and 4.
        bool hasProductionLoop = (cat == kCategoryProduction || cat == kCategorySpecial);
        bool expect = (c.wantCat == 1 || c.wantCat == 4);
        CHECK_EQ(hasProductionLoop, expect);
    }
}
