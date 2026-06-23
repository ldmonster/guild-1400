// Golden vectors for guild::render::MakeProjection / ProjectViewPoint —
// the engine's exact perspective projection (gilde.exe 0x5de3e4).
#include "render/d3_projection.h"
#include "tests/framework/test.h"

#include <cmath>

using namespace guild::render;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
}

// The clip window is built verbatim from the decompile: dvClipX=-1, dvClipWidth=2,
// dvClipY=H/W, dvClipHeight=(H/W)*2 with flt_62A6F4==2.0.
TEST(D3Projection, ClipWindow800x600) {
    D3Projection p = MakeProjection(800.0f, 600.0f, /*near=*/1.0f, /*far=*/1000.0f);
    CHECK(Near(p.clipX, -1.0f));
    CHECK(Near(p.clipWidth, 2.0f));
    CHECK(Near(p.clipY, 0.75f));         // 600/800
    CHECK(Near(p.clipHeight, 1.5f));     // 0.75 * 2.0
    // Q = far/(far-near) = 1000/999.
    CHECK(Near(p.q, 1000.0f / 999.0f, 1e-5f));
}

// On the optical axis a view point projects to the exact viewport centre
// (W/2, H/2) — matching the engine's picking centre flt_13FCD18 / flt_13FCD10.
TEST(D3Projection, AxisProjectsToCentre) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    ProjectedPoint q = ProjectViewPoint(p, 0.0f, 0.0f, 10.0f);
    CHECK(Near(q.sx, 400.0f));
    CHECK(Near(q.sy, 300.0f));
    CHECK(Near(q.w, 11.0f));             // w = z + near = 10 + 1
}

// Horizontal FOV is fixed at ~90 degrees: ndc_x reaches +1 (right screen edge,
// sx==W) exactly when vx == w == z + near.
TEST(D3Projection, HorizontalEdgeAt90Deg) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    // z=10 -> w=11; vx=11 -> ndc_x = 1 -> sx = W = 800.
    ProjectedPoint r = ProjectViewPoint(p, 11.0f, 0.0f, 10.0f);
    CHECK(Near(r.sx, 800.0f));
    CHECK(Near(r.sy, 300.0f));
    // vx = -11 -> left edge sx = 0.
    ProjectedPoint l = ProjectViewPoint(p, -11.0f, 0.0f, 10.0f);
    CHECK(Near(l.sx, 0.0f));
}

// Vertical extent is scaled by aspect = H/W: ndc_y reaches the TOP (sy==0) at
// vy == aspect * w, and the BOTTOM (sy==H) at vy == -aspect * w.
TEST(D3Projection, VerticalEdgesScaledByAspect) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    const float w = 11.0f;               // z=10, near=1
    const float vyTop = 0.75f * w;       // 8.25
    ProjectedPoint top = ProjectViewPoint(p, 0.0f, vyTop, 10.0f);
    CHECK(Near(top.sy, 0.0f));           // dvClipY top -> screen y 0
    ProjectedPoint bot = ProjectViewPoint(p, 0.0f, -vyTop, 10.0f);
    CHECK(Near(bot.sy, 600.0f));
}

// Square pixels: a square in view space (equal world dx and dy) at a given depth
// must cover an equal number of pixels horizontally and vertically.
TEST(D3Projection, SquarePixels) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    const float d = 2.0f, z = 50.0f;
    ProjectedPoint a = ProjectViewPoint(p, 0.0f, 0.0f, z);
    ProjectedPoint bx = ProjectViewPoint(p, d, 0.0f, z);
    ProjectedPoint by = ProjectViewPoint(p, 0.0f, d, z);
    const float dpx = std::fabs(bx.sx - a.sx);
    const float dpy = std::fabs(by.sy - a.sy);
    CHECK(Near(dpx, dpy, 1e-2f));
}

// NDC depth is monotonic in view depth and maps the near plane to ~0.
TEST(D3Projection, DepthMonotonic) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    ProjectedPoint nearP = ProjectViewPoint(p, 0.0f, 0.0f, 1.0f);
    ProjectedPoint midP  = ProjectViewPoint(p, 0.0f, 0.0f, 100.0f);
    ProjectedPoint farP  = ProjectViewPoint(p, 0.0f, 0.0f, 999.0f);
    CHECK(nearP.ndcZ < midP.ndcZ);
    CHECK(midP.ndcZ < farP.ndcZ);
}

// ---- wave-12 boundary/degenerate hardening -------------------------------
// Degenerate w==0 (view point exactly on the near-plane mirror z==-near): the
// engine returns BEFORE the divide (w==0 guard at 0x5de3e4). Reproduce the early
// return: r.w is set, the divides never run (no NaN/Inf write, no UB).
TEST(D3Projection, DegenerateWZeroNoDivide) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 1.0f, 1000.0f);
    // w = z + near == 0  =>  z = -1
    ProjectedPoint r = ProjectViewPoint(p, 5.0f, 5.0f, -1.0f);
    CHECK(Near(r.w, 0.0f));
    // sx/sy/ndcZ are left at their default-init (0) — never divided by zero.
    CHECK_EQ(r.sx, 0.0f);
    CHECK_EQ(r.sy, 0.0f);
    CHECK_EQ(r.ndcZ, 0.0f);
}

// Zero-width viewport: aspect uses the (width!=0)?:0 guard, far/near uses the
// (denom!=0)?:1 guard. Neither path produces a divide-by-zero / NaN.
TEST(D3Projection, ZeroWidthViewportGuarded) {
    D3Projection p = MakeProjection(0.0f, 600.0f, 1.0f, 1000.0f);
    CHECK_EQ(p.clipY, 0.0f);        // aspect guard -> 0
    CHECK_EQ(p.clipHeight, 0.0f);   // 0 * scale
    CHECK(std::isfinite(p.q));
}

// near==far: the Q denominator is zero -> the (denom!=0)?:1 guard yields q==1.
TEST(D3Projection, NearEqualsFarQGuarded) {
    D3Projection p = MakeProjection(800.0f, 600.0f, 500.0f, 500.0f);
    CHECK_EQ(p.q, 1.0f);
    CHECK(std::isfinite(p.q));
}

// Aspect handling: a 1:1 viewport gives a symmetric ±1 clip window in both axes.
TEST(D3Projection, SquareViewport) {
    D3Projection p = MakeProjection(512.0f, 512.0f, 1.0f, 1000.0f);
    CHECK(Near(p.clipY, 1.0f));          // 512/512
    CHECK(Near(p.clipHeight, 2.0f));     // 1.0 * 2.0
    ProjectedPoint c = ProjectViewPoint(p, 0.0f, 0.0f, 10.0f);
    CHECK(Near(c.sx, 256.0f));
    CHECK(Near(c.sy, 256.0f));
}
