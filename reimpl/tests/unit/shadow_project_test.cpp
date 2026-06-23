#include "test.h"

#include "render/shadow_project.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

// ===========================================================================
// VIBE_Shadow_RetOne / GetDefaultCallback — trivial leaves.
// ===========================================================================
TEST(ShadowProjectUnit, RetOneAndCallback) {
    CHECK_EQ(RetOne(), 1);
    int (*cb)() = GetDefaultCallback();
    CHECK(cb == &RetOne);
    CHECK_EQ(cb(), 1);
}

// ===========================================================================
// VIBE_Shadow_ResetCasterTransforms (0x5f4880) — clears cached transform of
// every OCCUPIED slot only, leaves empty slots untouched, returns 512.
// ===========================================================================
TEST(ShadowProjectUnit, ResetCasterTransforms) {
    ShadowCasterSlot t[kCasterTableSlots];
    t[0].entry = 1; t[0].cachedDir = {1, 2, 3}; t[0].cachedHeight = 9.0f;
    t[1].entry = 0; t[1].cachedDir = {4, 5, 6};          // empty -> untouched
    t[2].entry = 7; t[2].cachedDir = {7, 8, 9}; t[2].cachedHeight = 1.0f;
    t[3].entry = 0; t[3].cachedDir = {0, 0, 0};

    int r = ResetCasterTransforms(t, /*enabled=*/true);
    CHECK_EQ(r, 512);                                    // loop terminator

    // Occupied slots cleared (dir only; height preserved by the original).
    CHECK(Near(t[0].cachedDir.x, 0) && Near(t[0].cachedDir.y, 0) && Near(t[0].cachedDir.z, 0));
    CHECK(Near(t[0].cachedHeight, 9.0f));
    CHECK(Near(t[2].cachedDir.x, 0) && Near(t[2].cachedDir.y, 0) && Near(t[2].cachedDir.z, 0));
    // Empty slot keeps its (garbage) dir.
    CHECK(Near(t[1].cachedDir.x, 4.0f));
}

// Disabled gate (+529 & 4 clear): nothing is touched, returns 0.
TEST(ShadowProjectUnit, ResetCasterTransformsDisabled) {
    ShadowCasterSlot t[kCasterTableSlots];
    t[0].entry = 1; t[0].cachedDir = {1, 2, 3};
    int r = ResetCasterTransforms(t, /*enabled=*/false);
    CHECK_EQ(r, 0);
    CHECK(Near(t[0].cachedDir.x, 1.0f));                 // untouched
}

// ===========================================================================
// Directional projection (0x5f3721): v' = v + t*dir, t = (v.y-g)/-dir.y.
// Golden vectors computed with float32 oracle.
// ===========================================================================
TEST(ShadowProjectUnit, ProjectVertexDirectional) {
    ShadowVec3 dir{1.0f, -2.0f, 0.0f};
    ShadowVec3 a = ProjectVertexDirectional({8.0f, 5.0f, 4.0f}, dir, 0.0f);
    CHECK(Near(a.x, 10.5f) && Near(a.y, 0.0f) && Near(a.z, 4.0f));
    ShadowVec3 b = ProjectVertexDirectional({16.0f, 5.0f, 14.0f}, dir, 0.0f);
    CHECK(Near(b.x, 18.5f) && Near(b.y, 0.0f) && Near(b.z, 14.0f));
    ShadowVec3 c = ProjectVertexDirectional({2.0f, 5.0f, 14.0f}, dir, 0.0f);
    CHECK(Near(c.x, 4.5f) && Near(c.y, 0.0f) && Near(c.z, 14.0f));
    // The projected Y always lands exactly on the ground plane.
    ShadowVec3 d = ProjectVertexDirectional({3.0f, 12.0f, -7.0f}, dir, 2.0f);
    CHECK(Near(d.y, 2.0f));
}

// ===========================================================================
// Point-light projection (0x5f3ce3): ray from light through v hits y==g.
// ===========================================================================
TEST(ShadowProjectUnit, ProjectVertexPoint) {
    ShadowVec3 L{10.0f, 20.0f, 8.0f};
    ShadowVec3 a = ProjectVertexPoint({8.0f, 5.0f, 4.0f}, L, 0.0f);
    CHECK(Near(a.x, 7.33333349f) && Near(a.y, 0.0f) && Near(a.z, 2.66666651f));
    ShadowVec3 b = ProjectVertexPoint({12.0f, 5.0f, 4.0f}, L, 0.0f);
    CHECK(Near(b.x, 12.66666698f) && Near(b.y, 0.0f) && Near(b.z, 2.66666651f));
    // A vertex already at light height projects to ... ground (t=(v.y-g)/-(L.y-v.y)).
    ShadowVec3 c = ProjectVertexPoint({0.0f, 4.0f, 0.0f}, L, 4.0f);
    CHECK(Near(c.y, 4.0f));   // t=0 -> stays put, already on plane
}

// ===========================================================================
// WAVE-10 HARDENING — degenerate light directions. The projection divides by
// -dir.y (directional) / -d.y (point); a light whose ray is PARALLEL to the
// ground (dir.y == 0) yields an IEEE inf/nan, which is well-defined float
// behavior (NOT UB) — the original FPU produced the same. These tests pin that
// the degenerate path is finite-free of integer UB and that a zero direction
// vector does not trap. (The engine gates these out upstream via the bounds
// guard ShadowBoundsAcceptable; the math itself must not crash.)
// ===========================================================================
TEST(ShadowProjectUnit, DegenerateDirectionalLightDir) {
    // dir.y == 0: ray parallel to ground -> division by zero -> ±inf, no UB.
    ShadowVec3 dirFlat{1.0f, 0.0f, 0.0f};
    ShadowVec3 r = ProjectVertexDirectional({2.0f, 6.0f, 1.0f}, dirFlat, 0.0f);
    // x/z become inf (t = (6-0)/0 = +inf); y = t*0 + 6 = nan. We only assert no
    // crash/UB; the values are the same inf/nan the original FPU yields.
    CHECK(std::isinf(r.x) || std::isnan(r.x));

    // Fully-zero direction vector: t = (v.y-g)/-0 = -inf/+inf, components nan/inf.
    ShadowVec3 zero{0.0f, 0.0f, 0.0f};
    ShadowVec3 z = ProjectVertexDirectional({3.0f, 4.0f, 5.0f}, zero, 0.0f);
    CHECK(std::isnan(z.x) || std::isinf(z.x));
}

TEST(ShadowProjectUnit, DegeneratePointLightCoincident) {
    // Point light coincident with the vertex in Y (L.y == v.y): d.y == 0 ->
    // t = (L.y - g)/-0 -> inf; components inf/nan. No UB, matches the FPU.
    ShadowVec3 L{0.0f, 5.0f, 0.0f};
    ShadowVec3 r = ProjectVertexPoint({2.0f, 5.0f, 1.0f}, L, 0.0f);
    CHECK(std::isinf(r.x) || std::isnan(r.x));

    // Light exactly at the vertex (zero ray): d == 0 -> 0/0 = nan everywhere.
    ShadowVec3 r2 = ProjectVertexPoint({2.0f, 5.0f, 1.0f}, {2.0f, 5.0f, 1.0f}, 0.0f);
    CHECK(std::isnan(r2.x) || std::isinf(r2.x));
}

// Empty / single-vertex meshes: ProjectMeshToGround must not read past the span
// and returns the seeded (empty) bounds for n==0.
TEST(ShadowProjectUnit, ProjectMeshEmptyAndSingle) {
    ShadowVec3 dir{0.0f, -1.0f, 0.0f};
    ShadowVec3 out[1];

    // n == 0: no projection, bounds stay at their 1e10/-1e10 seeds.
    ShadowBounds b0 = ProjectMeshToGround(nullptr, 0, nullptr, dir, true, 0.0f);
    CHECK(b0.minX > 1e9f && b0.maxX < -1e9f);   // untouched seeds

    // n == 1: single vertex; bounds collapse to that point's XZ.
    ShadowVec3 one[1] = {{3.0f, 6.0f, 7.0f}};
    ShadowBounds b1 = ProjectMeshToGround(one, 1, out, dir, true, 0.0f);
    CHECK(Near(out[0].y, 0.0f));
    CHECK(Near(b1.minX, 3.0f) && Near(b1.maxX, 3.0f));
    CHECK(Near(b1.minZ, 7.0f) && Near(b1.maxZ, 7.0f));
}

// ===========================================================================
// ComputeCasterHeight (0x5f34c0) — the 3-priority resolution.
// ===========================================================================
TEST(ShadowProjectUnit, ComputeCasterHeightPriorities) {
    ShadowCasterSlot t[kCasterTableSlots];
    ShadowSamplePoint samples[8] = {{7.0f},{3.5f},{9.0f},{2.0f},{5.0f},{8.0f},{4.0f},{6.0f}};

    CasterHeightQuery q;
    q.originY = 10.0f; q.planeY = 3.0f; q.baseOffset = 1.5f;
    q.onGround = false; q.samples = samples; q.sampleCount = 8;

    // Priority 3 (no override, not on-ground): min sample (2.0) + base (1.5).
    CHECK(Near(ComputeCasterHeight(t, /*enabled=*/true, q), 3.5f));

    // Priority 2 (on-ground): originY - planeY + base = 10 - 3 + 1.5 = 8.5.
    q.onGround = true;
    CHECK(Near(ComputeCasterHeight(t, true, q), 8.5f));

    // Priority 1 (cached dir override): slot with non-zero dir -> its height,
    // overriding everything (even on-ground).
    t[2].entry = 5; t[2].cachedDir = {0.0f, 1.0f, 0.0f}; t[2].cachedHeight = 42.0f;
    CHECK(Near(ComputeCasterHeight(t, true, q), 42.0f));

    // A zero-length cached dir does NOT count as an override.
    ShadowCasterSlot t2[kCasterTableSlots];
    t2[0].entry = 9; t2[0].cachedDir = {0.0f, 0.0f, 0.0f}; t2[0].cachedHeight = 99.0f;
    q.onGround = false;
    CHECK(Near(ComputeCasterHeight(t2, true, q), 3.5f));   // falls through to min

    // No table (samples == nullptr) -> 0 (the v18||!(a1+492) early return).
    q.samples = nullptr;
    CHECK(Near(ComputeCasterHeight(t2, true, q), 0.0f));
}

// Edge: sampleCount == 0 in the priority-3 path. The min-fold loop runs zero
// times, so v16 stays at the 1e10 seed and the result is 1e10 + baseOffset.
// (Faithful to the original's `v16 = v17 = 1e10` init when the loop is empty.)
TEST(ShadowProjectUnit, ComputeCasterHeightZeroSamples) {
    ShadowCasterSlot t[kCasterTableSlots];        // all empty -> no override
    ShadowSamplePoint samples[1] = {{1.0f}};
    CasterHeightQuery q;
    q.originY = 0.0f; q.planeY = 0.0f; q.baseOffset = 2.0f;
    q.onGround = false; q.samples = samples; q.sampleCount = 0;
    float h = ComputeCasterHeight(t, /*enabled=*/true, q);
    CHECK(h > 9.0e9f);                              // 1e10 + 2.0 (no min taken)

    // enabled == false skips the override scan but priority 2/3 still run.
    q.sampleCount = 1;
    float h2 = ComputeCasterHeight(t, /*enabled=*/false, q);
    CHECK(Near(h2, 3.0f));                          // min(1.0) + base(2.0)
}
