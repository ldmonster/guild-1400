// tests/unit/multi_city_test.cpp — MULTI-CITY robustness, SYNTHETIC tier.
//
// Drives several synthetic "cities" (distinct seeded entity rosters in the live sim
// arrays, no shipped assets) through the SAME exercise pipeline the real path uses
// (play::ExerciseLiveWorld: render one frame, run one game-day, fold the full-world
// digest before/after). Asserts:
//   * each pseudo-city's witness is DETERMINISTIC (byte-identical on rerun),
//   * distinct pseudo-cities produce DISTINCT witnesses (no cross-talk: the full
//     reset between loads means city B never inherits city A's globals),
//   * a city with ZERO objects is handled gracefully (renders a clean frame, runs a
//     day, no crash).
#include "test.h"

#include "play/multi_city.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

constexpr int W = 96, H = 72;

// Seed a synthetic "city": fully reset the live world (exactly as the real path does
// between loads), then populate `objects` alive object records derived from `seed`.
// Returns the exercise witness over that live world.
CityWitness ExerciseSyntheticCity(std::uint32_t seed, int objects, int persons) {
    ResetLiveWorldForCity(seed);
    for (int i = 0; i < objects && i < kObjectCapacity; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = (i32)(1000 + i * 7 + (seed & 0x3F));
    }
    for (int i = 0; i < persons && i < kPersonCapacity; ++i) {
        g_persons[i].marker = 0;
        g_persons[i].id = (i32)(5000 + i + (seed & 0xF));
        g_personIds[i] = g_persons[i].id;
    }
    g_personArrayLoaded = persons > 0;
    g_sceneArrayLoaded  = true;

    shim::MemoryGraphicsDevice dev;
    dev.init(W, H, 16, false);
    return ExerciseLiveWorld(seed, (std::uint32_t)objects, (std::uint32_t)persons,
                             /*nodeCount=*/0, W, H, &dev);
}

} // namespace

// ---------------------------------------------------------------------------
// Each synthetic city's witness is deterministic across reruns.
// ---------------------------------------------------------------------------
TEST(MultiCityUnit, SyntheticCityDeterministic) {
    CityWitness a1 = ExerciseSyntheticCity(0x1111, 6, 1);
    CityWitness a2 = ExerciseSyntheticCity(0x1111, 6, 1);

    CHECK(a1.loaded);
    CHECK(a1.rendered);
    CHECK(a1.nonClearPx > 0);
    CHECK(a1.dayRan);
    CHECK(a1.dayStepsRun > 0);
    CHECK(a1.simEvolved());            // the day evolved the world
    CHECK_EQ(a1.hashBefore, a2.hashBefore);
    CHECK_EQ(a1.hashAfter,  a2.hashAfter);
    CHECK(a1.ok());
}

// ---------------------------------------------------------------------------
// Distinct cities -> distinct witnesses, with NO cross-talk: exercising city B
// after city A does not change city A's reproducible witness.
// ---------------------------------------------------------------------------
TEST(MultiCityUnit, DistinctCitiesNoCrossTalk) {
    // Three distinct rosters.
    CityWitness a = ExerciseSyntheticCity(0x1111, 6, 1);
    CityWitness b = ExerciseSyntheticCity(0x2222, 9, 2);
    CityWitness c = ExerciseSyntheticCity(0x3333, 3, 0);

    // Distinct before-hashes (different rosters fold to different worlds).
    CHECK(a.hashBefore != b.hashBefore);
    CHECK(b.hashBefore != c.hashBefore);
    CHECK(a.hashBefore != c.hashBefore);
    // ... and distinct after-day hashes.
    CHECK(a.hashAfter != b.hashAfter);
    CHECK(b.hashAfter != c.hashAfter);

    // Now re-run city A AFTER B and C have dirtied the globals: the full pre-load
    // reset means A reproduces its ORIGINAL witness exactly (zero leakage).
    CityWitness a_again = ExerciseSyntheticCity(0x1111, 6, 1);
    CHECK_EQ(a.hashBefore, a_again.hashBefore);
    CHECK_EQ(a.hashAfter,  a_again.hashAfter);
}

// ---------------------------------------------------------------------------
// A city with ZERO objects is handled gracefully: it renders a clean (all-clear)
// frame and runs a day without crashing. nonClear is 0; the run is deterministic.
// ---------------------------------------------------------------------------
TEST(MultiCityUnit, ZeroObjectCityGraceful) {
    CityWitness z1 = ExerciseSyntheticCity(0x4444, 0, 0);
    CityWitness z2 = ExerciseSyntheticCity(0x4444, 0, 0);

    CHECK(z1.loaded);
    CHECK(z1.rendered);                 // present() still succeeds on a clear frame
    CHECK_EQ(z1.sceneObjects, 0);       // nothing drawn
    CHECK_EQ(z1.nonClearPx, 0);         // all background
    CHECK(z1.dayRan);                   // a day still runs over an empty world
    CHECK_EQ(z1.hashBefore, z2.hashBefore);   // deterministic
    CHECK_EQ(z1.hashAfter,  z2.hashAfter);
    // The day may or may not evolve a truly-empty world (RNG + clock advance), but it
    // must not crash and must be reproducible. We assert reproducibility above; the
    // empty-world evolution is exercised by the populated cities.
}
