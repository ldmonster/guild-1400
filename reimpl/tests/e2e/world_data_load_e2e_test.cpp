// End-to-end: load the REAL shipped A_Geb.dat / A_Obj.dat through the
// reconstructed loader + DiskFileSystem and verify the building/object type
// tables populate with the right record count and sane decoded fields.
// Guarded: if the asset folder is absent the test passes trivially.
#include "test.h"

#include "world/data_load.h"
#include "sim/building.h"
#include "sim/building_types.h"
#include "sim/building_production.h"
#include "shim_impl/disk_filesystem.h"

#include <cstring>

using namespace guild;

static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool assetsPresent() {
    shim::DiskFileSystem fs(kRoot);
    return fs.exists("data/A_Geb.dat") && fs.exists("data/A_Obj.dat");
}

constexpr int GEB = 589;
constexpr int OBJ = 65;

TEST(WorldDataLoadE2E, LoadsRealGebAndObjTables) {
    if (!assetsPresent()) { CHECK(true); return; }      // skipped: no assets
    shim::DiskFileSystem fs(kRoot);
    sim::ResetBuildings();

    int rc = world::WorldLoadBuildingAndObjectData(&fs, "data/");
    CHECK_EQ(rc, 0);
    CHECK(sim::g_buildingTypesLoaded);
    CHECK(sim::g_sceneTypesLoaded);

    const u8* geb = reinterpret_cast<const u8*>(sim::g_buildingTypes);
    const u8* obj = reinterpret_cast<const u8*>(sim::g_sceneTypes);

    // Record 0 of each file is the "Null" placeholder (kind 0, name "Null").
    CHECK_EQ((int)geb[0], 0);
    CHECK(std::strcmp((const char*)geb + 1, "Null") == 0);
    CHECK_EQ((int)obj[0], 0);
    CHECK(std::strcmp((const char*)obj + 1, "Null") == 0);

    // Known building records from the real A_Geb.dat (recovered by inspection):
    //   rec1 "Arbeiterunterkunft" kind 1, rec4 "Haus" kind 2, rec7 "Palazzo" kind 2.
    CHECK_EQ((int)geb[1 * GEB + 0], 1);
    CHECK(std::strcmp((const char*)geb + 1 * GEB + 1, "Arbeiterunterkunft") == 0);
    CHECK_EQ((int)geb[4 * GEB + 0], 2);
    CHECK(std::strcmp((const char*)geb + 4 * GEB + 1, "Haus") == 0);
    CHECK(std::strcmp((const char*)geb + 7 * GEB + 1, "Palazzo") == 0);

    // Known object records from the real A_Obj.dat:
    //   obj1 "Kanalisation", obj4 "Offenes_Feuer" kind 1.
    CHECK(std::strcmp((const char*)obj + 1 * OBJ + 1, "Kanalisation") == 0);
    CHECK_EQ((int)obj[4 * OBJ + 0], 1);
    CHECK(std::strcmp((const char*)obj + 4 * OBJ + 1, "Offenes_Feuer") == 0);

    // Every loaded building/object record name should be a printable C string
    // within its stride (no run-off): sample-check a spread of records.
    for (int i = 0; i < world::kBuildingTypeLoadCount; i += 9) {
        const char* nm = (const char*)geb + i * GEB + 1;
        std::size_t len = 0;
        while (len < (std::size_t)(GEB - 2) && nm[len]) ++len;
        CHECK(len < (std::size_t)(GEB - 2));
    }

    // The derived remap table (byte_13CE862) must be populated for all 731 types
    // (every entry resolved away from the 72 sentinel via propagation or fallback,
    // OR legitimately left at a small record index). Just assert it ran and the
    // low entries mirror into the shared consumer table.
    bool anyNonZero = false;
    for (int i = 0; i < 731; ++i)
        if (world::WorldTypeRemapAt(i) != 0) { anyNonZero = true; break; }
    CHECK(anyNonZero);
    // shared 256-wide consumer table mirrors the low part.
    for (int i = 0; i < sim::kSceneTypeRemap; ++i)
        CHECK_EQ((int)sim::g_sceneTypeRemap[i], (int)world::WorldTypeRemapAt(i));
}
