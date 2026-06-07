// tests/e2e/multi_city_e2e_test.cpp — MULTI-CITY robustness, GUARDED e2e tier.
//
// Exercise ALL FIVE shipped cities (AUGSBURG, BERLIN, DRESDEN, HANNOVER, KOELN) in
// ONE process, sequentially, through play::LoadAndExerciseCity (mount + load +
// render one real frame + run one real game-day + fold the full-world digest before/
// after). The real test is loading multiple real cities in one process: it proves
//   * each city loads with sane (non-zero) counts,
//   * each renders a non-empty frame (real WorldRenderer over the loaded objects),
//   * each simulates a deterministic game-day (full-world hash evolves; the same
//     city + seed reproduce the witness byte-for-byte on rerun),
//   * loading city N+1 is UNAFFECTED by city N — zero global leakage between loads
//     (re-running an earlier city after the others reproduces its original witness).
//
// Skips cleanly when the shipped city assets are absent (honors GUILD_GAME_DIR). Any
// city that fails to load or render is reported precisely (the loop never hides it).
#include "test.h"

#include "play/multi_city.h"
#include "io/vfs.h"
#include "shim_impl/disk_filesystem.h"
#include "shim_impl/filedump_graphics.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

constexpr int W = 128, H = 96;
constexpr std::uint32_t kSeed = 0xC17E5;

std::string GameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// The five shipped city stems (the on-disk extension casing varies: KOELN.CTY is
// upper-cased, the rest are .cty). We resolve the real path per stem below.
const char* kCityStems[] = {"AUGSBURG", "BERLIN", "DRESDEN", "HANNOVER", "KOELN"};
constexpr int kNumCities = 5;

// Resolve the real VFS path for a city stem, honoring the shipped extension casing.
// Returns "" if neither casing exists.
std::string ResolveCityPath(shim::DiskFileSystem& fs, const char* stem) {
    std::string lower = std::string("Resources/gamedata/Cities/") + stem + ".cty";
    std::string upper = std::string("Resources/gamedata/Cities/") + stem + ".CTY";
    if (fs.exists(lower.c_str())) return lower;
    if (fs.exists(upper.c_str())) return upper;
    return std::string();
}

bool RealAssetsPresent() {
    shim::DiskFileSystem fs(GameDir());
    if (!fs.exists("Gilde.INI")) return false;
    // Require all five present for the full-coverage e2e.
    for (int i = 0; i < kNumCities; ++i)
        if (ResolveCityPath(fs, kCityStems[i]).empty()) return false;
    return true;
}

// Exercise one city by stem into a FileDump device (dumps a BMP). Returns the witness.
CityWitness ExerciseCity(const char* stem, const char* dumpPrefix,
                         std::vector<std::uint8_t>& frameOut) {
    shim::DiskFileSystem fs(GameDir());
    std::string path = ResolveCityPath(fs, stem);
    shim::FileDumpGraphicsDevice dev;
    dev.init(W, H, 16, false);
    dev.configureDump("/tmp", dumpPrefix, shim::FileDumpGraphicsDevice::kBmp);
    CityWitness w = LoadAndExerciseCity(&fs, GameDir(), path, kSeed, W, H, &dev);
    frameOut = dev.lastPresented();
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// All five shipped cities: load, render, sim, deterministic, zero cross-talk.
// ---------------------------------------------------------------------------
TEST(MultiCityE2E, AllFiveCitiesLoadRenderSimNoLeak) {
    if (!RealAssetsPresent()) {
        std::printf("  [skip] MultiCityE2E: real game dir / cities absent (%s)\n",
                    GameDir().c_str());
        CHECK(true);
        return;
    }
    std::printf("[multi-city-e2e] asset dir: %s\n", GameDir().c_str());

    CityWitness witnesses[kNumCities];
    std::vector<std::uint8_t> frames[kNumCities];

    // --- exercise all five sequentially in one process -----------------------
    for (int i = 0; i < kNumCities; ++i) {
        std::string prefix = std::string("guild_mc_") + kCityStems[i];
        witnesses[i] = ExerciseCity(kCityStems[i], prefix.c_str(), frames[i]);
    }

    // --- the per-city table ---------------------------------------------------
    std::printf("\n[multi-city-e2e] === 5-CITY TABLE ===\n");
    std::printf("%-10s %7s %7s %6s %9s %10s %s\n",
                "city", "objects", "persons", "nodes", "nonClear", "evolved", "hashB!=hashA");
    for (int i = 0; i < kNumCities; ++i) {
        const CityWitness& w = witnesses[i];
        std::printf("%-10s %7u %7u %6u %9d %10s %d  (B=%llu A=%llu)\n",
                    kCityStems[i], w.objectCount, w.personCount, w.nodeCount,
                    w.nonClearPx, (w.loaded ? "yes" : "LOAD-FAIL"),
                    (int)w.simEvolved(),
                    (unsigned long long)w.hashBefore,
                    (unsigned long long)w.hashAfter);
    }
    std::printf("\n");

    // --- per-city assertions (report any failure precisely) ------------------
    for (int i = 0; i < kNumCities; ++i) {
        const CityWitness& w = witnesses[i];
        if (!w.loaded)
            std::printf("[multi-city-e2e] *** %s FAILED TO LOAD (%s)\n",
                        kCityStems[i], w.cityPath.c_str());
        CHECK(w.loaded);
        if (!w.loaded) continue;
        // Sane counts: a shipped city has objects to render and at least the
        // city-record preamble. (We don't pin exact counts per city — the point is
        // generality — but every shipped city has > 0 objects.)
        CHECK(w.objectCount > 0u);
        if (!w.rendered)
            std::printf("[multi-city-e2e] *** %s FAILED TO RENDER\n", kCityStems[i]);
        CHECK(w.rendered);
        CHECK(w.nonClearPx > 0);          // the real meshes/objects painted pixels
        CHECK(w.dayRan);
        CHECK(w.dayStepsRun > 0);
        if (!w.simEvolved())
            std::printf("[multi-city-e2e] *** %s sim did NOT evolve (B==A==%llu)\n",
                        kCityStems[i], (unsigned long long)w.hashBefore);
        CHECK(w.simEvolved());            // the game-day evolved the world
        CHECK(w.ok());
    }

    // --- the cities are genuinely DIFFERENT worlds (distinct witnesses) -------
    for (int i = 0; i < kNumCities; ++i)
        for (int j = i + 1; j < kNumCities; ++j) {
            CHECK(witnesses[i].hashBefore != witnesses[j].hashBefore);
            CHECK(witnesses[i].hashAfter  != witnesses[j].hashAfter);
        }

    // --- ZERO GLOBAL LEAKAGE: re-run the FIRST city after all five dirtied every
    //     global, and it reproduces its ORIGINAL witness + frame byte-for-byte.
    //     Also re-run the LAST city, proving the determinism holds either direction.
    std::vector<std::uint8_t> reFrame0, reFrameN;
    CityWitness re0 = ExerciseCity(kCityStems[0], "guild_mc_re0", reFrame0);
    CityWitness reN = ExerciseCity(kCityStems[kNumCities - 1], "guild_mc_reN", reFrameN);
    std::printf("[multi-city-e2e] rerun %s: B=%llu A=%llu | rerun %s: B=%llu A=%llu\n",
                kCityStems[0], (unsigned long long)re0.hashBefore,
                (unsigned long long)re0.hashAfter,
                kCityStems[kNumCities - 1], (unsigned long long)reN.hashBefore,
                (unsigned long long)reN.hashAfter);

    CHECK_EQ(witnesses[0].hashBefore, re0.hashBefore);
    CHECK_EQ(witnesses[0].hashAfter,  re0.hashAfter);
    CHECK(frames[0] == reFrame0);
    CHECK_EQ(witnesses[kNumCities - 1].hashBefore, reN.hashBefore);
    CHECK_EQ(witnesses[kNumCities - 1].hashAfter,  reN.hashAfter);
    CHECK(frames[kNumCities - 1] == reFrameN);

    io::VfsShutdown();
}
