// End-to-end: mount a city .ini in a mock VFS, load it through the VFS path,
// switch the active universe slot, init the world state, and verify the city
// record + universe/world state against a reference.
#include "tests/framework/test.h"

#include <cstring>
#include <string>

#include "shim/IFileSystem.h"
#include "io/vfs.h"
#include "world/city.h"
#include "world/city_load.h"
#include "sim/universe.h"
#include "world/world_setup.h"

using namespace guild;

// ---------------------------------------------------------------------------
// Minimal in-memory mock filesystem: one path -> one blob.
// ---------------------------------------------------------------------------
namespace {
struct MemFile : shim::IFile {
    std::string data;
    std::int64_t pos = 0;
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data.size() - static_cast<std::size_t>(pos);
        std::size_t k = n < avail ? n : avail;
        std::memcpy(dst, data.data() + pos, k);
        pos += static_cast<std::int64_t>(k);
        return k;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        if (whence == 0) pos = off;
        else if (whence == 1) pos += off;
        else pos = static_cast<std::int64_t>(data.size()) + off;
        return pos;
    }
    std::int64_t tell() override { return pos; }
    std::int64_t size() override { return static_cast<std::int64_t>(data.size()); }
};

struct MockFs : shim::IFileSystem {
    std::string path;
    std::string blob;
    shim::IFile* open(const char* p, const char* mode) override {
        if (mode && mode[0] == 'w') return nullptr;
        if (path == p) {
            auto* f = new MemFile();
            f->data = blob;
            return f;
        }
        return nullptr;
    }
    void close(shim::IFile* f) override { delete f; }
    bool exists(const char* p) override { return path == p; }
};

const char* kCityIni =
    "[A - ALLGEMEIN]\n"
    "Stadtname=Hamburg\n"
    "KartenPosition=300,400\n"
    "Glaube=1\n"
    "HistorieStart=1400\n"
    "HistorieEnde=1500\n"
    "Land=2\n"
    "Sprache=0\n"
    "KartenOffset=5,6\n"
    "MaxPlayer=4\n"
    "[A - EINWOHNER]\n"
    "Einwohner_0=1400,800\n"
    "[B - WETTER]\n"
    "Regenwahrscheinlichkeit=12,13,14,15\n"
    "Zufrierenwahrscheinlichkeit=70\n"
    "[F - GESETZE]\n"
    "Strafgesetze=7,8,9\n";
} // namespace

TEST(WorldBootstrapE2E, FullBringup) {
    // --- 1. mount the city .ini in the mock VFS --------------------------------
    MockFs fs;
    fs.path = "gamedata/cities/Hamburg.ini";
    fs.blob = kCityIni;
    CHECK(io::VfsInit(&fs, /*caseInsensitive=*/false));

    // clean world/universe state
    std::memset(&world::g_cities[0], 0, sizeof(world::g_cities[0]));
    sim::ResetUniverse();

    // --- 2. load the city from the VFS ----------------------------------------
    int ok = world::CityLoadDefinitionIniFromVfs("Hamburg", 0);
    CHECK_EQ(ok, 1);

    const world::CityRecord& c = world::g_cities[0];
    CHECK_EQ(c.name[0], (u16)'H');
    CHECK_EQ(c.name[6], (u16)'g');
    CHECK_EQ(c.mapPos[0], 300);
    CHECK_EQ(c.mapPos[1], 400);
    CHECK_EQ((int)c.faith, 1);
    CHECK_EQ(c.historyStart, 1400);
    CHECK_EQ(c.historyEnd, 1500);
    CHECK_EQ((int)c.land, 2);
    CHECK_EQ((int)c.maxPlayer, 4);
    CHECK_EQ(c.einwohner[0].year, 1400);
    CHECK_EQ(c.einwohner[0].value, 800);
    CHECK_EQ(c.rainProb[0], 12);
    CHECK_EQ(c.rainProb[3], 15);
    CHECK_EQ(c.freezeProb, 70);
    CHECK_EQ((int)c.straf[0], 7);
    CHECK_EQ((int)c.straf[2], 9);
    CHECK_EQ(c.mapOffset[0], 5);
    CHECK_EQ(c.mapOffset[1], 6);

    // --- 3. switch the active universe slot -----------------------------------
    sim::g_render.objListHead = 0xCAFE0000;  // mark slot 0's live state
    CHECK(sim::UniverseSwitchActiveSlot(2, /*quiet=*/true));
    CHECK_EQ(sim::g_activeUniverseId, 2);
    CHECK(sim::g_activeUniverseRecord == &sim::g_universeSlots[2]);
    // slot 0 captured the marker.
    CHECK_EQ(sim::g_universeSlots[0].objListHead, 0xCAFE0000u);

    // --- 4. init the world state ----------------------------------------------
    world::WorldSetupInit(/*capDivisor=*/500.0f);
    CHECK(world::g_capDivisor == 500.0f);
    CHECK(!world::g_worldLoaded);
    CHECK(world::g_worldTypeBase == nullptr);
    // WorldSetupInit ran the universe reset over the active slot (now slot 2):
    // default cameras respawned, near/far defaults set.
    CHECK(sim::g_megaCam != 0);
    CHECK_EQ(sim::g_render.nearPlane, 4u);
    CHECK_EQ(sim::g_render.farPlane, 15u);
    // economy parameter table seeded (good 3 defaults).
    CHECK_EQ((int)world::g_goods[3].driftWeight, 300);
    CHECK_EQ((int)world::g_goods[3].contribWeight, 5);
    CHECK_EQ((int)world::g_goods[3].cap, 150);

    // --- 5. district-coord accessor against a reference -----------------------
    std::memset(world::g_districtCoords, 0, sizeof(world::g_districtCoords));
    world::g_districtCoords[10].a = 42;
    world::g_districtCoords[10].b = 84;
    i32 coord[2] = {0, 0};
    CHECK_EQ(world::CityGetDistrictCoord(10, coord), 1);
    CHECK_EQ(coord[0], 42);
    CHECK_EQ(coord[1], 84);

    io::VfsShutdown();
}
