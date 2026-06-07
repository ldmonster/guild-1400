// tests/e2e/save_roundtrip_e2e_test.cpp — P3 / M3 save round-trip, E2E tier
// (GUARDED on the shipped real .cty assets).
//
// Load + round-trip EACH of the 5 shipped cities through the genuine reconstructed
// serializers and prove the M3 invariant per city:
//
//   load <CITY>.cty -> HashFullWorld (h1) -> SAVE (S1) -> zero+RELOAD ->
//   HashFullWorld (h2) -> assert h1 == h2; SAVE again (S2) -> assert S1 == S2.
//
// Reports the per-city table (city, objects, persons, scene-tiles, h1==h2, bytes
// stable). Skips cleanly when the shipped assets are absent (honors GUILD_GAME_DIR).
#include "test.h"

#include "app/real_boot.h"
#include "io/vfs.h"
#include "play/save_roundtrip.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    return fs.exists("Gilde.INI") &&
           fs.exists("Resources/gamedata/Cities/AUGSBURG.cty");
}

// Resolve the real on-disk .cty path for a city, probing both the lowercase `.cty`
// extension (AUGSBURG/BERLIN/DRESDEN/HANNOVER) and the uppercase `.CTY` (KOELN) —
// the install ships KOELN.CTY while RealCityPath only emits ".cty".
std::string ResolveCityPath(shim::IFileSystem& fs, const std::string& city) {
    std::string base = "Resources/gamedata/Cities/" + city;
    if (fs.exists((base + ".cty").c_str())) return base + ".cty";
    if (fs.exists((base + ".CTY").c_str())) return base + ".CTY";
    return std::string();
}

} // namespace

TEST(SaveRoundtripE2E, AllShippedCities_RoundTrip) {
    if (!RealAssetsPresent()) {
        std::printf("[SKIP] SaveRoundtripE2E: real assets not present at %s\n",
                    GameDir().c_str());
        return;
    }

    const char* kCities[] = {"AUGSBURG", "BERLIN", "DRESDEN", "HANNOVER", "KOELN"};
    const std::uint32_t kSeed = 0x5A7E10AD;

    shim::DiskFileSystem fs(GameDir());

    // Mount the real game assets so the VFS resolves the .cty paths (and the
    // transparent gzip backing applies to the gzip-framed city seeds).
    app::RealGameAssets assets =
        app::MountRealGameAssets(&fs, GameDir(), "Gilde.INI", {}, /*caseInsensitive=*/false);
    CHECK(assets.vfsBound);
    if (!assets.vfsBound) { io::VfsShutdown(); return; }

    std::printf("\n  %-10s %8s %8s %8s  %-8s %-8s\n",
                "CITY", "OBJECTS", "PERSONS", "TILES", "h1==h2", "S1==S2");
    std::printf("  -------------------------------------------------------------\n");

    int loadedCities = 0;
    for (const char* city : kCities) {
        std::string path = ResolveCityPath(fs, city);
        if (path.empty()) {
            std::printf("  %-10s  (.cty not found)\n", city);
            CHECK(false);   // a shipped city must resolve
            continue;
        }

        RoundTripResult r = RoundTripCity(&fs, path, kSeed);

        std::printf("  %-10s %8u %8u %8u  %-8s %-8s  (h1=%016llx h2=%016llx, %zu B)\n",
                    city, r.objectCount, r.personCount, r.sceneTiles,
                    r.hashEquivalent() ? "yes" : "NO",
                    r.saveBytesStable ? "yes" : "NO",
                    (unsigned long long)r.hashAfterLoad,
                    (unsigned long long)r.hashAfterReload,
                    r.saveBytes1);

        CHECK(r.loaded);             // the city loaded
        if (!r.loaded) continue;
        ++loadedCities;
        CHECK(r.saved);              // it serialized
        CHECK(r.reloaded);           // it reloaded
        CHECK(r.hashEquivalent());   // h1 == h2 — the save preserved the whole world
        CHECK(r.saveBytesStable);    // S1 == S2 — the save format is a fixed point
        CHECK(r.ok());
    }

    std::printf("  -------------------------------------------------------------\n");
    std::printf("  %d / 5 cities loaded\n\n", loadedCities);
    CHECK_EQ(loadedCities, 5);

    io::VfsShutdown();
}
