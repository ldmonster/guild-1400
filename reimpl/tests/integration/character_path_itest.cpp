#include "test.h"

// Integration: the character_path leaves run against REAL reconstructed siblings,
// not mocks:
//   * FindNearbyWide's proximity test is forwarded to the real
//     guild::util::VectorWithinTolerance (util/math.cpp, VIBE_Math_VectorWithinTolerance
//     @0x5caa4c) — the live wiring's box test.
//   * the per-turn AI walk-step clamp is fed by the real RNG chain
//     guild::util::RandomModulo -> guild::crt::RandNext (the ANSI LCG), and we
//     assert NearestTargetWalkSteps agrees bit-for-bit with the value obtained by
//     clamping a replay of the same real RNG draw.
#include "sim/character_path.h"
#include "sim/character_query.h"
#include "util/math.h"            // real VectorWithinTolerance
#include "util/math_random.h"     // real RandomModulo
#include "crt/rand.h"             // real RandNext / Srand

#include <cstring>
#include <cstdint>

using namespace guild;
using namespace guild::sim;

namespace {
// Forward the hook into the real util::VectorWithinTolerance (it takes non-const
// pointers; the box test never mutates, so the const_cast is safe and matches the
// live call shape).
bool RealVecTol(const float* a, const float* b, float tol) {
    return util::VectorWithinTolerance(const_cast<float*>(a), const_cast<float*>(b), tol);
}
int RealRandomModulo(u16 n) { return util::RandomModulo(n); }

struct Arena {
    LiveActor recs[4];
    MeshHandle meshes[4];
    Universe uni;
    Arena() { std::memset(recs, 0, sizeof(recs)); std::memset(meshes, 0, sizeof(meshes)); }
};
}  // namespace

// FindNearbyWide agrees with the real VectorWithinTolerance on which peers fall
// inside the 300-unit box (cross-module flow, real sibling).
TEST(CharPathIntegration, FindNearbyWideUsesRealVectorTolerance) {
    ResetCharacterQuery();
    CharacterPathHooks h = CharacterPathGetHooks();
    h.vectorWithinTolerance = RealVecTol;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    Arena arena;
    LiveActor& self = arena.recs[0];
    self.mesh = &arena.meshes[0];
    self.mesh->pos[0] = 0; self.mesh->pos[1] = 0; self.mesh->pos[2] = 0;
    self.universe = &arena.uni; self.universeId = 1; self.groupId = 0;

    // Peer just inside the box on every axis (|299| <= 300).
    LiveActor& inBox = arena.recs[1];
    inBox.mesh = &arena.meshes[1];
    inBox.mesh->pos[0] = 299; inBox.mesh->pos[1] = -299; inBox.mesh->pos[2] = 299;
    inBox.universe = &arena.uni; inBox.universeId = 1; inBox.groupId = 0;

    // Peer just outside on one axis (|301| > 300).
    LiveActor& outBox = arena.recs[2];
    outBox.mesh = &arena.meshes[2];
    outBox.mesh->pos[0] = 301; outBox.mesh->pos[1] = 0; outBox.mesh->pos[2] = 0;
    outBox.universe = &arena.uni; outBox.universeId = 1; outBox.groupId = 0;

    g_live[0] = &self;
    g_live[1] = &inBox;
    g_live[2] = &outBox;

    LiveActor* out[kNearbyWideMax];
    int n = FindNearbyWide(&self, out);
    // The real per-component box test admits inBox only.
    CHECK_EQ(n, 1);
    if (n >= 1) CHECK_EQ((void*)out[0], (void*)&inBox);

    // Cross-check directly against the sibling: the real test agrees with our count.
    bool realIn  = util::VectorWithinTolerance(self.mesh->pos, inBox.mesh->pos, kNearbyWideTol);
    bool realOut = util::VectorWithinTolerance(self.mesh->pos, outBox.mesh->pos, kNearbyWideTol);
    CHECK(realIn);
    CHECK(!realOut);

    CharacterPathSetHooks(&prev);
    ResetCharacterQuery();
}

// The walk-step clamp run over the real RNG matches a replay of the same LCG draw.
TEST(CharPathIntegration, WalkStepsOverRealRng) {
    CharacterPathHooks h = CharacterPathGetHooks();
    h.randomModulo = RealRandomModulo;
    CharacterPathHooks prev = CharacterPathSetHooks(&h);

    // Seed the real LCG, draw one RandomModulo, and treat it as a synthetic
    // distance, then clamp it the way FindNearestTarget does (debugSpeed 0).
    crt::Srand(12345);
    int dist = static_cast<int>(static_cast<u16>(util::RandomModulo(0x40)));  // 0..63
    int viaModule = NearestTargetWalkSteps(dist, 0);

    // Replay: re-seed, draw the same value, clamp by the same rule independently.
    crt::Srand(12345);
    int distReplay = static_cast<int>(static_cast<u16>(util::RandomModulo(0x40)));
    CHECK_EQ(dist, distReplay);               // deterministic LCG
    int start = (distReplay < 0 ? -distReplay : distReplay) / 3 + 1;
    int lo = 15, hi = 35;
    int v = start & 0xFF;
    if (v >= hi) v = hi;
    int expect;
    if (v <= lo) expect = lo;
    else { expect = hi; if (v < hi) expect = v; }
    expect &= 0xFF;
    CHECK_EQ(viaModule, expect);

    CharacterPathSetHooks(&prev);
}
