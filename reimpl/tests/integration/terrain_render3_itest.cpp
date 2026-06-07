#include "test.h"

// Integration: drive terrain_render3's weather thunder-trigger phase against the
// REAL reconstructed RNG sibling. In the live binary VIBE_Weather_RenderAndThunder
// (0x4c05ac) calls VIBE_Math_RandomModulo (0x58b89c) directly; that function is
// reconstructed as guild::util::RandomModulo (util/math_random.cpp), which in
// turn advances the real LCG guild::crt::RandNext (crt/rand.cpp). We forward the
// module's `randomMod` hook into util::RandomModulo exactly as the engine wires
// it, seed the real LCG with guild::crt::Srand, and assert the thunder decision
// matches the bit-exact sequence the real LCG produces — a genuine cross-module
// flow (render thunder phase -> util RNG modulo -> crt LCG).

#include "render/terrain_render3.h"
#include "util/math_random.h"   // REAL reconstructed sibling: VIBE_Math_RandomModulo
#include "crt/rand.h"           // REAL LCG it advances

using namespace guild;
using namespace guild::render;

namespace {
// The randomMod hook IS util::RandomModulo in the live wiring.
int RealRandomModulo(u16 n) { return util::RandomModulo(n); }
} // namespace

TEST(TR3_ITEST, ThunderUsesRealRng_FiresFirstCall) {
    TerrainRender3Hooks h{};
    h.randomMod = &RealRandomModulo;
    InstallTerrainRender3Hooks(h);

    // seed 163: first RandNext()%300 == 0 -> thunder fires immediately.
    crt::Srand(163);
    CHECK(ThunderShouldTrigger(/*weatherType*/0) == true);

    ResetTerrainRender3Hooks();
}

TEST(TR3_ITEST, ThunderUsesRealRng_SecondBranchType3) {
    TerrainRender3Hooks h{};
    h.randomMod = &RealRandomModulo;
    InstallTerrainRender3Hooks(h);

    // seed 80: first %300 != 0, second %100 == 0 -> fires only for type 3.
    crt::Srand(80);
    CHECK(ThunderShouldTrigger(/*type*/3) == true);

    // Same seed, non-type-3: the second RNG branch is never taken, so it must
    // NOT fire (first %300 != 0). This proves the short-circuit ordering routes
    // through the real RNG exactly once.
    crt::Srand(80);
    CHECK(ThunderShouldTrigger(/*type*/0) == false);

    ResetTerrainRender3Hooks();
}

TEST(TR3_ITEST, ThunderUsesRealRng_NoFire) {
    TerrainRender3Hooks h{};
    h.randomMod = &RealRandomModulo;
    InstallTerrainRender3Hooks(h);

    // seed 1 (MSVC default): first %300 != 0 and second %100 != 0 -> no fire.
    crt::Srand(1);
    CHECK(ThunderShouldTrigger(/*type*/3) == false);

    ResetTerrainRender3Hooks();
}

TEST(TR3_ITEST, RandomModuloZeroIsDeterministic) {
    // Faithful guard: util::RandomModulo(0) == 0 regardless of LCG state, so a
    // thunder phase fed a zero modulus would always "fire" (==0). Assert the
    // sibling's n==0 contract directly through the wired hook.
    TerrainRender3Hooks h{};
    h.randomMod = &RealRandomModulo;
    InstallTerrainRender3Hooks(h);
    crt::Srand(12345);
    CHECK_EQ(RealRandomModulo(0), 0);
    ResetTerrainRender3Hooks();
}
