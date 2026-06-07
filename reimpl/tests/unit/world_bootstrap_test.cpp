// Unit tests for the world/city/universe bootstrap slice.
//   * City_LoadDefinitionIni: load a city .ini (in-memory source) -> 756-byte
//     CityRecord field checks.
//   * Universe_SwitchActiveSlot: slot switch saves/restores render state + active id.
//   * WorldSetup: WorldCountActiveObjects name lookup + WorldSetupInit globals.
#include "tests/framework/test.h"

#include <cstring>

#include "world/city.h"
#include "world/city_load.h"
#include "sim/universe.h"
#include "world/world_setup.h"

using namespace guild;

// ---------------------------------------------------------------------------
// A mock city .ini text + in-memory file source.
// ---------------------------------------------------------------------------
namespace {
const char* kCityIni =
    "[A - ALLGEMEIN]\n"
    "Stadtname=Koeln\n"
    "KartenPosition=120,240\n"
    "Glaube=2\n"
    "HistorieStart=1410\n"
    "HistorieEnde=1480\n"
    "Land=3\n"
    "Sprache=1\n"
    "KartenOffset=7,9\n"
    "MaxPlayer=6\n"
    "NachbarStadt=Bonn Aachen\n"
    "[A - EINWOHNER]\n"
    "Einwohner_0=1400,1000\n"
    "Einwohner_1=1450,2000\n"
    "[A - PRUNK]\n"
    "Prunk_0=1400,5\n"
    "[B - WETTER]\n"
    "Regenwahrscheinlichkeit=10,20,30,40\n"
    "Schneewahrscheinlichkeit=1,2,3,4\n"
    "Zufrierenwahrscheinlichkeit=55\n"
    "[C - PRIVILEGIEN]\n"
    "Privilegien=1,0,1,0,1,0,1,0,1,0,1\n"
    "[F - GESETZE]\n"
    "Verfassungsgesetze=11,12,13\n"
    "Finanzgesetze=21,22\n"
    "Strafgesetze=31\n"
    "Gildengesetze=41\n"
    "Kirchengesetze=51\n"
    "DiebeRaeubergesetze=61,62\n";

struct MapSource : world::ICityFileSource {
    char* Load(const char* path) override {
        // Only the home-city path resolves; neighbour cities (Bonn/Aachen) have no
        // file and return null (the loader must tolerate that).
        if (std::strcmp(path, "gamedata/cities/Koeln.ini") == 0) {
            std::size_t n = std::strlen(kCityIni);
            char* b = new char[n + 1];
            std::memcpy(b, kCityIni, n + 1);
            return b;
        }
        return nullptr;
    }
    void Free(char* b) override { delete[] b; }
};
} // namespace

TEST(WorldBootstrap, CityLoadFields) {
    std::memset(&world::g_cities[0], 0, sizeof(world::g_cities[0]));
    MapSource src;
    int ok = world::CityLoadDefinitionIni(src, "Koeln", 0);
    CHECK_EQ(ok, 1);

    const world::CityRecord& c = world::g_cities[0];
    // Stadtname is widened ASCII -> UTF-16.
    CHECK_EQ(c.name[0], (u16)'K');
    CHECK_EQ(c.name[1], (u16)'o');
    CHECK_EQ(c.name[4], (u16)'n');
    CHECK_EQ(c.name[5], (u16)0);
    CHECK_EQ(c.mapPos[0], 120);
    CHECK_EQ(c.mapPos[1], 240);
    CHECK_EQ((int)c.faith, 2);
    CHECK_EQ(c.historyStart, 1410);
    CHECK_EQ(c.historyEnd, 1480);
    CHECK_EQ((int)c.land, 3);
    CHECK_EQ((int)c.language, 1);
    CHECK_EQ(c.mapOffset[0], 7);
    CHECK_EQ(c.mapOffset[1], 9);
    CHECK_EQ((int)c.maxPlayer, 6);

    CHECK_EQ(c.einwohner[0].year, 1400);
    CHECK_EQ(c.einwohner[0].value, 1000);
    CHECK_EQ(c.einwohner[1].year, 1450);
    CHECK_EQ(c.einwohner[1].value, 2000);
    CHECK_EQ(c.prunk[0].year, 1400);
    CHECK_EQ(c.prunk[0].value, 5);

    CHECK_EQ(c.rainProb[0], 10);
    CHECK_EQ(c.rainProb[3], 40);
    CHECK_EQ(c.snowProb[2], 3);
    CHECK_EQ(c.freezeProb, 55);

    CHECK_EQ((int)c.privileges[0], 1);
    CHECK_EQ((int)c.privileges[1], 0);
    CHECK_EQ((int)c.privileges[10], 1);

    CHECK_EQ((int)c.verfassung[0], 11);
    CHECK_EQ((int)c.verfassung[2], 13);
    CHECK_EQ((int)c.finanz[1], 22);
    CHECK_EQ((int)c.straf[0], 31);
    CHECK_EQ((int)c.gilde[0], 41);
    CHECK_EQ((int)c.kirche[0], 51);
    CHECK_EQ((int)c.diebeRaeuber[1], 62);
}

// Record stays exactly 756 bytes (the static_assert in types.h guards it, but
// re-check at runtime that the loader writes within bounds).
TEST(WorldBootstrap, CityRecordSize) {
    CHECK_EQ((int)sizeof(world::CityRecord), 756);
    CHECK_EQ((int)sizeof(sim::UniverseRecord), 984);
}

TEST(WorldBootstrap, DistrictCoord) {
    std::memset(world::g_districtCoords, 0, sizeof(world::g_districtCoords));
    world::g_districtCoords[5].a = 0x1234;
    world::g_districtCoords[5].b = 0x5678;
    i32 out[2] = {-1, -1};
    CHECK_EQ(world::CityGetDistrictCoord(5, out), 1);
    CHECK_EQ(out[0], 0x1234);
    CHECK_EQ(out[1], 0x5678);
    // out of range -> 0, out untouched
    out[0] = 99; out[1] = 98;
    CHECK_EQ(world::CityGetDistrictCoord(72, out), 0);
    CHECK_EQ(out[0], 99);
    CHECK_EQ(world::CityGetDistrictCoord(-1, out), 0);
}

// ---------------------------------------------------------------------------
// Universe slot switching.
// ---------------------------------------------------------------------------
TEST(WorldBootstrap, UniverseSwitchSavesAndRestores) {
    sim::ResetUniverse();
    // Seed live render state for slot 0.
    sim::g_render.objListHead = 0xAAAA0001;
    sim::g_render.fov0 = 1.5f;
    sim::g_render.fogColor = 0x00112233;
    sim::g_render.floor = 0xF100;

    // Switch to slot 3 (quiet -> no fog/terrain leaves needed).
    CHECK(sim::UniverseSwitchActiveSlot(3, /*quiet=*/true));
    CHECK_EQ(sim::g_activeUniverseId, 3);
    CHECK(sim::g_activeUniverseRecord == &sim::g_universeSlots[3]);
    // Slot 0's record captured the old live state.
    CHECK_EQ(sim::g_universeSlots[0].objListHead, 0xAAAA0001u);
    CHECK_EQ(sim::g_universeSlots[0].fogColor, 0x00112233u);
    CHECK_EQ(sim::g_universeSlots[0].floor, 0xF100u);
    // First visit to slot 3 seeded its camera node (near=4/far=15).
    CHECK_EQ(sim::g_universeSlots[3].nearPlane, 4u);
    CHECK_EQ(sim::g_universeSlots[3].farPlane, 15u);

    // Mutate live state on slot 3, then go back to slot 0.
    sim::g_render.fov0 = 9.0f;
    CHECK(sim::UniverseSwitchActiveSlot(0, /*quiet=*/true));
    CHECK_EQ(sim::g_activeUniverseId, 0);
    // Slot 0's saved fov0 (1.5) restored into the live globals.
    CHECK(sim::g_render.fov0 == 1.5f);
    CHECK_EQ(sim::g_render.objListHead, 0xAAAA0001u);
    // Slot 3's record captured the mutation.
    CHECK(sim::g_universeSlots[3].fov0 == 9.0f);
}

TEST(WorldBootstrap, UniverseSwitchBounds) {
    sim::ResetUniverse();
    CHECK(!sim::UniverseSwitchActiveSlot(64, false));   // >= 64 rejected
    CHECK(!sim::UniverseSwitchActiveSlot(1000, false));
    // same-slot switch is a no-op success
    CHECK(sim::UniverseSwitchActiveSlot(0, true));
    CHECK_EQ(sim::g_activeUniverseId, 0);
}

TEST(WorldBootstrap, UniverseFrustumRecompute) {
    sim::ResetUniverse();
    // Put known values in slot 2 so the load recomputes the frustum scalar.
    sim::g_universeSlots[2].fov0 = 10.0f;     // *0.59
    sim::g_universeSlots[2].clipNear = 20.0f; // *0.30
    sim::g_universeSlots[2].fov1 = 30.0f;     // *0.11
    sim::g_universeSlots[2].objListHead = 1;  // already-initialized -> no camera seed
    CHECK(sim::UniverseSwitchActiveSlot(2, true));
    float want = 10.0f * 0.59f + 20.0f * 0.30f + 30.0f * 0.11f;
    CHECK(sim::g_render.frustum > want - 0.001f && sim::g_render.frustum < want + 0.001f);
}

TEST(WorldBootstrap, UniverseCreateDefaultCameras) {
    sim::ResetUniverse();
    sim::g_extraCameras = 0;
    sim::UniverseCreateDefaultCameras();
    CHECK(sim::g_megaCam != 0);
    CHECK_EQ(sim::g_camOben, 0u);   // not spawned without the extra-camera flag
    sim::g_extraCameras = 1;
    sim::UniverseCreateDefaultCameras();
    CHECK(sim::g_camOben != 0);
    CHECK(sim::g_camVorne != 0);
    CHECK(sim::g_camSeite != 0);
    sim::g_extraCameras = 0;
}

// ---------------------------------------------------------------------------
// World setup.
// ---------------------------------------------------------------------------
TEST(WorldBootstrap, WorldCountActiveObjectsLookup) {
    // Build a tiny 65-byte type-def table with names at +1.
    constexpr int stride = 65, count = 4;
    static unsigned char tbl[stride * count];
    std::memset(tbl, 0, sizeof(tbl));
    auto setName = [&](int i, const char* n) {
        std::strcpy(reinterpret_cast<char*>(tbl + stride * i + 1), n);
    };
    setName(0, "ob_NIX");
    setName(1, "ob_GOLDGULDEN");
    setName(2, "ob_WEIN");
    setName(3, "ob_BIER");
    CHECK_EQ(world::WorldCountActiveObjects("ob_GOLDGULDEN", tbl, stride, count), 1);
    // case-insensitive (A-Z)
    CHECK_EQ(world::WorldCountActiveObjects("OB_WEIN", tbl, stride, count), 2);
    CHECK_EQ(world::WorldCountActiveObjects("ob_BIER", tbl, stride, count), 3);
    // miss -> 0
    CHECK_EQ(world::WorldCountActiveObjects("ob_MISSING", tbl, stride, count), 0);
}

TEST(WorldBootstrap, WorldSetupInitGlobals) {
    sim::ResetUniverse();
    world::g_worldBuildingBase = reinterpret_cast<void*>(0x1234);
    world::g_worldLoaded = true;
    world::WorldSetupInit(/*capDivisor=*/123.0f);
    CHECK(world::g_worldBuildingBase == nullptr);
    CHECK(!world::g_worldLoaded);
    // economy table seeded.
    CHECK(world::g_capDivisor == 123.0f);
    CHECK_EQ((int)world::g_goods[3].driftWeight, 300);
    CHECK_EQ((int)world::g_goods[3].cap, 150);
    // universe reset re-spawned the default cameras.
    CHECK(sim::g_megaCam != 0);
    CHECK_EQ(sim::g_render.nearPlane, 4u);
    CHECK_EQ(sim::g_render.farPlane, 15u);
}
