// tests/unit/playable_slice_test.cpp — the slice STEP SEQUENCING on a SYNTHETIC
// world (no assets, no device). Proves the five-step loop behaves as required:
//   * the two RENDER steps are PURE — they do not mutate the live world
//     (HashFullWorld() is unchanged across a render),
//   * the COMMAND step and the GAME-DAY step DO mutate the world,
//   * the world genuinely CHANGED end to end (load hash != post-day hash),
//   * the whole sequence is DETERMINISTIC: same seed + same inputs -> identical
//     per-step hashes across reruns.
#include "test.h"

#include "play/playable_slice.h"
#include "play/world_digest.h"
#include "sim/entity.h"

#include <cstdio>

using namespace guild;
using namespace guild::play;

namespace {

SliceClick DefaultClick() {
    SliceClick c;
    c.sx = 0.0f; c.sy = 0.0f;          // 0,0 -> aim at the first live object
    c.pickRadius = 64.0f;
    c.mode = CursorMode::kConquer;      // WARE conquer order (kind 6) — enqueues +
                                        // applies, mutating the picked object record
    c.attackAllowed = false;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// The five-step sequence: render steps pure, command/day mutate, world changed.
// ---------------------------------------------------------------------------
TEST(PlayableSliceUnit, StepSequencingProducesExpectedMutationPattern) {
    SliceStepHash h[5];
    int n = RunSliceStepsSynthetic(/*seed=*/0x1234, /*persons=*/3, /*objects=*/5,
                                   DefaultClick(), /*econSeed=*/0xABCD,
                                   /*fbW=*/96, /*fbH=*/72, h, 5);
    CHECK_EQ(n, 5);
    if (n != 5) return;

    for (int i = 0; i < 5; ++i)
        std::printf("[slice-unit] step %d hashAfter=%llu mutated=%d\n",
                    (int)h[i].step, (unsigned long long)h[i].hashAfter,
                    (int)h[i].mutated);

    // kLoad seeded a non-empty world.
    CHECK(h[0].hashAfter != 0u);

    // kRender1 is PURE: render must not change world state.
    CHECK_EQ(h[1].step, SliceStep::kRender1);
    CHECK(!h[1].mutated);
    CHECK_EQ(h[1].hashAfter, h[0].hashAfter);

    // kCommand mutated the world (the click order landed on a live object).
    CHECK_EQ(h[2].step, SliceStep::kCommand);
    CHECK(h[2].mutated);

    // kDay mutated the world (economy passes + RNG + the per-day witness).
    CHECK_EQ(h[3].step, SliceStep::kDay);
    CHECK(h[3].mutated);

    // kRender2 is PURE.
    CHECK_EQ(h[4].step, SliceStep::kRender2);
    CHECK(!h[4].mutated);
    CHECK_EQ(h[4].hashAfter, h[3].hashAfter);

    // End-to-end: the world CHANGED between frame 1 and frame 2.
    CHECK(h[4].hashAfter != h[0].hashAfter);
}

// ---------------------------------------------------------------------------
// Determinism: two reruns produce identical per-step hashes.
// ---------------------------------------------------------------------------
TEST(PlayableSliceUnit, StepSequenceIsDeterministicAcrossReruns) {
    SliceStepHash a[5], b[5];
    int na = RunSliceStepsSynthetic(0x55AA, 4, 6, DefaultClick(), 0x99, 80, 60, a, 5);
    int nb = RunSliceStepsSynthetic(0x55AA, 4, 6, DefaultClick(), 0x99, 80, 60, b, 5);
    CHECK_EQ(na, 5);
    CHECK_EQ(nb, 5);
    if (na != 5 || nb != 5) return;

    for (int i = 0; i < 5; ++i) {
        CHECK_EQ(a[i].hashAfter, b[i].hashAfter);
        CHECK_EQ((int)a[i].mutated, (int)b[i].mutated);
    }
}

// ---------------------------------------------------------------------------
// A different seed yields a different world (the hash actually tracks the seed).
// ---------------------------------------------------------------------------
TEST(PlayableSliceUnit, DifferentSeedYieldsDifferentWorld) {
    SliceStepHash a[5], b[5];
    RunSliceStepsSynthetic(0x1111, 3, 5, DefaultClick(), 0x7, 64, 48, a, 5);
    RunSliceStepsSynthetic(0x2222, 3, 5, DefaultClick(), 0x7, 64, 48, b, 5);
    // The seeded object ids differ, so the loaded-world hash differs.
    CHECK(a[0].hashAfter != b[0].hashAfter);
    sim::ResetEntityArrays();
}
