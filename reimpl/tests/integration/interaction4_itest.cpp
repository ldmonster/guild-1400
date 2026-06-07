#include "test.h"

// Integration: drive interaction4's RNG-seeded target-search threshold against the
// REAL random sibling (util::RandomModulo @0x58b89c -> crt::RandNext @0x5cb8bc), no
// mock RNG. The game wires VIBE_Math_RandomModulo into FindNearestTarget's
// `RandomModulo(0x24)+35` tie-break seed; we forward the Interaction4Hooks.randomModulo
// slot straight into the real sibling (Srand-seeded for determinism) and assert the
// seed the real LCG produces is the seed that drives the search, then that the chosen
// budget gate behaves consistently with that seed.
#include "sim/interaction4.h"
#include "util/math_random.h"  // REAL reconstructed sibling
#include "crt/rand.h"          // REAL LCG (Srand/RandNext)

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// The randomModulo hook -> real util::RandomModulo (over the real crt LCG).
int RealRandomModulo(u16 n) { return util::RandomModulo(n); }
} // namespace

// With a fixed seed the real LCG produces a deterministic first draw; the seed the
// search loop uses is RandomModulo(0x24)+35. We reproduce the same draw independently
// from the same seed to prove the wiring is the live one.
TEST(Interaction4Itest, ThresholdSeedFromRealLcg) {
    ResetInteraction4Hooks();
    g_i4Hooks.randomModulo = &RealRandomModulo;

    // Independent reference draw from the same seed.
    crt::Srand(12345);
    int expectedMod = crt::RandNext() % 0x24;
    int expectedSeed = expectedMod + 35;

    // Now run the module path from the same seed.
    crt::Srand(12345);
    int seed = FindNearestThresholdSeed();

    CHECK_EQ(seed, expectedSeed);
    CHECK(seed >= 35 && seed <= 70); // RandomModulo(0x24) in [0,35] -> seed [35,70]
}

// End-to-end through the real RNG: the seed determines actionType 50 (<50) vs 30
// (>=50); feed the seed back into FindNearestPick and assert the actionType matches
// the real-RNG-derived seed, and the budget gate (cash*0.95) accepts a small score.
TEST(Interaction4Itest, SeedDrivesActionTypeWithRealRng) {
    ResetInteraction4Hooks();
    g_i4Hooks.randomModulo = &RealRandomModulo;
    // Provide a fixed, small score so only the actionType selection varies.
    g_i4Hooks.scaleByActionType = [](int, int) {
        float f = 10.0f; int x; std::memcpy(&x, &f, sizeof x); return x;
    };

    crt::Srand(999);
    int seed = FindNearestThresholdSeed(); // 35..70, always >= 35

    // FindNearestPick uses bestDistShifted >= 50 -> actionType 30.
    FindTargetResult r = FindNearestPick(seed, 0, 100);
    int expectType = (seed >= 50) ? 30 : 50;
    CHECK_EQ(r.chosenActionType, expectType);
    // score 10 <= budget 95 -> accept (15).
    CHECK_EQ(r.code, 15);
}

// Determinism: same seed -> same sequence of seeds across repeated runs.
TEST(Interaction4Itest, RealRngDeterministic) {
    ResetInteraction4Hooks();
    g_i4Hooks.randomModulo = &RealRandomModulo;

    crt::Srand(42);
    int a0 = FindNearestThresholdSeed();
    int a1 = FindNearestThresholdSeed();

    crt::Srand(42);
    int b0 = FindNearestThresholdSeed();
    int b1 = FindNearestThresholdSeed();

    CHECK_EQ(a0, b0);
    CHECK_EQ(a1, b1);
}
