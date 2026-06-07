// tests/integration/multi_city_itest.cpp — MULTI-CITY robustness, INTEGRATION tier.
//
// Two small real-FORMAT "cities" (alive object records seeded directly into the live
// sim arrays, no shipped assets needed) exercised back-to-back in ONE process
// through play::ExerciseLiveWorld (the same render + one-day + full-world-digest
// pipeline the real loader path drives). Asserts each:
//   * loads (live world populated),
//   * renders a non-empty frame (nonClear px > 0),
//   * simulates (the full-world hash evolves across the day),
//   * is deterministic on rerun,
// and that the TWO cities produce DIFFERENT witnesses with NO cross-talk: re-running
// city 1 after city 2 reproduces city 1's original witness byte-for-byte (zero
// leftover global state between loads — the failure mode this whole wave guards).
#include "test.h"

#include "play/multi_city.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace guild;
using namespace guild::play;
using namespace guild::sim;

namespace {

constexpr int W = 128, H = 96;

// Seed a small real-FORMAT city (distinct alive object roster) into the live arrays
// after a FULL reset, then render+sim it and return the witness plus the frame bytes.
CityWitness ExerciseFormatCity(std::uint32_t seed, int baseId, int objects,
                               std::vector<std::uint8_t>& frameOut) {
    ResetLiveWorldForCity(seed);
    for (int i = 0; i < objects && i < kObjectCapacity; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = (i32)(baseId + i * 5);
    }
    g_sceneNodeCount = 0;

    shim::MemoryGraphicsDevice dev;
    dev.init(W, H, 16, false);
    CityWitness w = ExerciseLiveWorld(seed, (std::uint32_t)objects, 0, 0, W, H, &dev);
    frameOut = dev.lastPresented();
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// Two real-format cities, exercised in one process: each loads/renders/sims
// deterministically, the two differ, and there is no cross-talk.
// ---------------------------------------------------------------------------
TEST(MultiCityIntegration, TwoCitiesNoCrossTalk) {
    std::vector<std::uint8_t> f1, f2;

    // City 1 then City 2. Distinct object COUNTS (both below the renderer's 8-object
    // draw cap) so the drawn object SET genuinely differs and the frames differ at the
    // pixel level — the renderer caps at 8 and backfills, so two cities that BOTH
    // exceed the cap can render identically even though their worlds (hashes) differ.
    CityWitness c1 = ExerciseFormatCity(0xC1700, /*baseId=*/300, /*objects=*/4, f1);
    CityWitness c2 = ExerciseFormatCity(0xC2700, /*baseId=*/700, /*objects=*/7, f2);

    std::printf("[multi-city-itest] c1: obj=%u nonClear=%d steps=%d before=%llu after=%llu\n",
                c1.objectCount, c1.nonClearPx, c1.dayStepsRun,
                (unsigned long long)c1.hashBefore, (unsigned long long)c1.hashAfter);
    std::printf("[multi-city-itest] c2: obj=%u nonClear=%d steps=%d before=%llu after=%llu\n",
                c2.objectCount, c2.nonClearPx, c2.dayStepsRun,
                (unsigned long long)c2.hashBefore, (unsigned long long)c2.hashAfter);

    // Each loaded, rendered a non-empty frame, and simulated.
    CHECK(c1.loaded);  CHECK(c2.loaded);
    CHECK(c1.rendered); CHECK(c2.rendered);
    CHECK(c1.nonClearPx > 0); CHECK(c2.nonClearPx > 0);
    CHECK(c1.dayRan); CHECK(c2.dayRan);
    CHECK(c1.simEvolved()); CHECK(c2.simEvolved());
    CHECK(c1.ok()); CHECK(c2.ok());

    // The two cities are genuinely different worlds.
    CHECK(c1.hashBefore != c2.hashBefore);
    CHECK(c1.hashAfter  != c2.hashAfter);
    CHECK(f1 != f2);   // different object rosters -> different frames

    // NO CROSS-TALK: re-run city 1 (after city 2 dirtied every global) and city 2,
    // each reproduces its ORIGINAL witness + frame byte-for-byte.
    std::vector<std::uint8_t> f1b, f2b;
    CityWitness c1b = ExerciseFormatCity(0xC1700, 300, 4, f1b);
    CityWitness c2b = ExerciseFormatCity(0xC2700, 700, 7, f2b);

    CHECK_EQ(c1.hashBefore, c1b.hashBefore);
    CHECK_EQ(c1.hashAfter,  c1b.hashAfter);
    CHECK_EQ(c2.hashBefore, c2b.hashBefore);
    CHECK_EQ(c2.hashAfter,  c2b.hashAfter);
    CHECK(f1 == f1b);
    CHECK(f2 == f2b);
}
