#include "render/morph_blend_walk.h"

#include "tests/framework/test.h"

#include <cmath>

// =============================================================================
// MorphBlendWalk — golden tests for the morph-active vertex-blend walk of
// VIBE_Mesh_InterpolateMorphVertices @0x5c953c (render::AccumulateMorphBlend):
// the single-layer blend (set), multi-layer accumulate, the clip/weight gate, and
// the layer-weight scaling. Per-vertex blend math:
//   out[a] = (p0[a]*scale0[a]*lw + bias0[a]*lw)*w0 + (p1[a]*scale1[a]*lw + bias1[a]*lw)*w1
// No assets.
// =============================================================================
using namespace guild;
using guild::render::Vertex;
using guild::render::MorphKeyframe;
using guild::render::MorphLayer;
using guild::render::AccumulateMorphBlend;

namespace {
bool feq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
} // namespace

// (a) Single layer SETS the vertex from the 50/50 blend of two keyframes.
TEST(MorphBlendWalk, SingleLayerSet) {
    u8 pts0[3] = {10, 20, 30};
    u8 pts1[3] = {40, 50, 60};
    MorphLayer ly;
    ly.kf0.scale[0] = ly.kf0.scale[1] = ly.kf0.scale[2] = 1.0f;
    ly.kf1.scale[0] = ly.kf1.scale[1] = ly.kf1.scale[2] = 1.0f;
    ly.kf0.points = pts0; ly.kf1.points = pts1;
    ly.layerWeight = 1.0f; ly.w0 = 0.5f; ly.w1 = 0.5f;

    Vertex verts[1]{};
    int n = AccumulateMorphBlend(verts, 1, &ly, 1);
    CHECK_EQ(n, 1);
    // x = (10*1)*0.5 + (40*1)*0.5 = 25 ; y = 10+25 = 35 ; z = 15+30 = 45.
    CHECK(feq(verts[0].x, 25.0f));
    CHECK(feq(verts[0].y, 35.0f));
    CHECK(feq(verts[0].z, 45.0f));
}

// (b) Bias + layer-weight scaling.
TEST(MorphBlendWalk, BiasAndLayerWeight) {
    u8 p[3] = {100, 0, 0};
    MorphLayer ly;
    ly.kf0.scale[0] = 2.0f; ly.kf0.bias[0] = 5.0f;
    ly.kf1.scale[0] = 2.0f; ly.kf1.bias[0] = 5.0f;
    ly.kf0.points = p; ly.kf1.points = p;
    ly.layerWeight = 0.5f;       // scales scale AND bias
    ly.w0 = 1.0f; ly.w1 = 0.0f;  // only keyframe 0 contributes

    Vertex verts[1]{};
    AccumulateMorphBlend(verts, 1, &ly, 1);
    // x = (100 * (2*0.5) + (5*0.5)) * 1.0 = 100*1.0 + 2.5 = 102.5.
    CHECK(feq(verts[0].x, 102.5f));
}

// (c) Two layers: first SETS, second ACCUMULATES.
TEST(MorphBlendWalk, MultiLayerAccumulate) {
    u8 a[3] = {10, 0, 0};
    u8 b[3] = {4, 0, 0};
    MorphLayer ls[2];
    for (auto& l : ls) {
        l.kf0.scale[0] = 1.0f; l.kf1.scale[0] = 1.0f;
        l.layerWeight = 1.0f; l.w0 = 1.0f; l.w1 = 0.0f;
    }
    ls[0].kf0.points = a; ls[0].kf1.points = a;   // layer 0 -> x = 10
    ls[1].kf0.points = b; ls[1].kf1.points = b;   // layer 1 -> +4
    Vertex verts[1]{};
    int n = AccumulateMorphBlend(verts, 1, ls, 2);
    CHECK_EQ(n, 2);
    CHECK(feq(verts[0].x, 14.0f));   // 10 (set) + 4 (accumulate)
}

// (d) Gate: a zero-weight or point-less layer is skipped (not counted, not applied).
TEST(MorphBlendWalk, ClipWeightGate) {
    u8 p[3] = {10, 0, 0};
    MorphLayer ls[3];
    // layer 0: zero weight -> skipped.
    ls[0].kf0.points = p; ls[0].kf1.points = p; ls[0].layerWeight = 0.0f;
    // layer 1: no points -> skipped.
    ls[1].layerWeight = 1.0f;
    // layer 2: valid -> sets x = 10.
    ls[2].kf0.scale[0] = 1.0f; ls[2].kf1.scale[0] = 1.0f;
    ls[2].kf0.points = p; ls[2].kf1.points = p;
    ls[2].layerWeight = 1.0f; ls[2].w0 = 1.0f; ls[2].w1 = 0.0f;

    Vertex verts[1]{};
    int n = AccumulateMorphBlend(verts, 1, ls, 3);
    CHECK_EQ(n, 1);                  // only layer 2 applied
    CHECK(feq(verts[0].x, 10.0f));
}

// ===========================================================================
// W11-ANIM hardening — degenerate morph-walk inputs (ASAN/UBSAN).
// ===========================================================================

// Null vertex array / zero count / null layers / zero layerCount: the guard at the
// top returns 0 without dereferencing anything.
TEST(MorphBlendWalkEdge, NullAndZeroGuards) {
    MorphLayer ly{};
    Vertex v{};
    CHECK_EQ(AccumulateMorphBlend(nullptr, 1, &ly, 1), 0);
    CHECK_EQ(AccumulateMorphBlend(&v, 0, &ly, 1), 0);
    CHECK_EQ(AccumulateMorphBlend(&v, -3, &ly, 1), 0);    // negative count
    CHECK_EQ(AccumulateMorphBlend(&v, 1, nullptr, 1), 0);
    CHECK_EQ(AccumulateMorphBlend(&v, 1, &ly, 0), 0);
    CHECK_EQ(AccumulateMorphBlend(&v, 1, &ly, -2), 0);    // negative layerCount
}

// A layer whose keyframe points pointers are null (no clip) is skipped — the engine's
// clip gate — so a NULL-points layer never reads through the dangling pointer.
TEST(MorphBlendWalkEdge, NullPointsLayerSkipped) {
    MorphLayer ly{};                  // kf0.points / kf1.points default null
    ly.layerWeight = 1.0f;
    Vertex verts[2]{};
    verts[0].x = 7; verts[0].y = 8; verts[0].z = 9;   // must stay untouched
    CHECK_EQ(AccumulateMorphBlend(verts, 2, &ly, 1), 0);  // nothing applied
    CHECK(std::fabs(verts[0].x - 7.0f) < 1e-6f);
}

// Zero layer weight is gated out (the +64 & 0x7FFFFFFF == 0 test) even with valid
// point buffers, so the points are not read at all.
TEST(MorphBlendWalkEdge, ZeroWeightLayerSkipped) {
    u8 pts0[6] = {1,2,3, 4,5,6};
    u8 pts1[6] = {7,8,9, 10,11,12};
    MorphLayer ly{};
    ly.kf0.points = pts0; ly.kf1.points = pts1;
    ly.layerWeight = 0.0f;            // gated out
    Vertex verts[2]{};
    CHECK_EQ(AccumulateMorphBlend(verts, 2, &ly, 1), 0);
}

// Points buffers sized EXACTLY count*3: every vertex read stays in bounds. ASAN
// exercises the last-vertex read (points[(count-1)*3 + 2]).
TEST(MorphBlendWalkEdge, ExactPointBufferReadInBounds) {
    const int count = 4;
    u8 pts0[count * 3], pts1[count * 3];
    for (int i = 0; i < count * 3; ++i) { pts0[i] = (u8)i; pts1[i] = (u8)(255 - i); }
    MorphLayer ly{};
    ly.kf0.points = pts0; ly.kf1.points = pts1;
    ly.kf0.scale[0] = ly.kf0.scale[1] = ly.kf0.scale[2] = 1.0f;
    ly.kf1.scale[0] = ly.kf1.scale[1] = ly.kf1.scale[2] = 1.0f;
    ly.layerWeight = 1.0f; ly.w0 = 0.5f; ly.w1 = 0.5f;
    Vertex verts[count]{};
    int applied = AccumulateMorphBlend(verts, count, &ly, 1);
    CHECK_EQ(applied, 1);             // one active layer
}

// Two layers but the SECOND has a shorter (and gated-off) buffer: only the first is
// applied; the second's null/zero-weight gate keeps its read off the wire. Proves a
// per-layer mismatch does not OOB the shorter buffer.
TEST(MorphBlendWalkEdge, MismatchedSecondLayerGated) {
    const int count = 3;
    u8 a0[count*3] = {0}, a1[count*3] = {0};
    MorphLayer layers[2]{};
    layers[0].kf0.points = a0; layers[0].kf1.points = a1; layers[0].layerWeight = 1.0f;
    // layers[1]: null points -> skipped (would otherwise be a short buffer).
    layers[1].layerWeight = 1.0f;
    Vertex verts[count]{};
    CHECK_EQ(AccumulateMorphBlend(verts, count, layers, 2), 1); // only layer 0
}
