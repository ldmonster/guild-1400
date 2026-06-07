#include "test.h"

#include "render/render_leaves6.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::ClipBounds;
using guild::render::RenderLeaves6Hooks;
using guild::render::CameraView;
using guild::render::LabelObject;
using guild::render::LabelEntry;
using guild::render::SubmeshCorner;
using guild::render::SurfaceView;
using guild::render::SurfaceFillRect;

namespace {

bool FEq(float a, float b) {
    std::uint32_t ua, ub;
    std::memcpy(&ua, &a, 4);
    std::memcpy(&ub, &b, 4);
    return ua == ub;
}

// Software framebuffer the drawSpan hook fills (a tiny 16bpp run raster, just
// enough to observe the clipped span the line clipper hands it).
struct SoftFB {
    int w = 0, h = 0;
    std::vector<std::uint16_t> px;
    int lastX0 = -1, lastY0 = -1, lastX1 = -1, lastY1 = -1;
    bool drew = false;
};
SoftFB g_fb;

i32 E2ESpan(i32 x0, i32 y0, i32 x1, i32 y1, i16 color) {
    g_fb.drew = true;
    g_fb.lastX0 = x0; g_fb.lastY0 = y0; g_fb.lastX1 = x1; g_fb.lastY1 = y1;
    // Mark the start pixel (clipped origin) if in-bounds, as a flow witness.
    if (x0 >= 0 && x0 < g_fb.w && y0 >= 0 && y0 < g_fb.h)
        g_fb.px[static_cast<size_t>(y0) * g_fb.w + x0] = static_cast<std::uint16_t>(color);
    return x0;
}

// A transform pivot that applies a fixed +1000 world offset, so the AABB->corner
// transform flow is observable end to end.
void OffsetPivot(void* /*obj*/, const float* src, float* dst) {
    dst[0] = src[0] + 1000.0f;
    dst[1] = src[1] + 1000.0f;
    dst[2] = src[2] + 1000.0f;
}

// Surface fill that tallies issued fills.
int g_fillCount = 0;
i32 TallyFill(void* /*surface*/, const i32* /*destRect*/, i32 /*color*/) {
    ++g_fillCount;
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// A small render "frame": clip+raster several lines, build a label list, then a
// mesh AABB, then fill a surface rect — all through the installed hooks, exactly
// as the debug-overlay path would chain these leaves.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_E2E, OverlayFrameFlow) {
    g_fb = SoftFB{};
    g_fb.w = 100; g_fb.h = 100;
    g_fb.px.assign(static_cast<size_t>(g_fb.w) * g_fb.h, 0);
    g_fillCount = 0;

    RenderLeaves6Hooks h{};
    h.drawSpan       = &E2ESpan;
    h.transformPivot = &OffsetPivot;
    h.surfaceFill    = &TallyFill;
    render::InstallRenderLeaves6Hooks(h);

    // --- 1) clip + raster two lines (one inside, one rejected) ---------------
    ClipBounds c{10, 10, 200, 200};
    i32 r1 = render::DrawLineClipped(c, 50, 50, 55, 60, 0x07E0);   // inside
    CHECK(g_fb.drew);
    if (g_fb.drew) {
        CHECK_EQ(g_fb.lastX0, 50);
        CHECK_EQ(g_fb.lastY0, 50);
    }
    CHECK_EQ(r1, 50);   // E2ESpan returns x0

    g_fb.drew = false;
    ClipBounds rej{640, 480, 0, 0};
    i32 r2 = render::DrawLineClipped(rej, 100, 100, 200, 300, 1);  // rejected
    CHECK(!g_fb.drew);
    CHECK_EQ(r2, 100);

    // The inside line's start pixel must be set.
    CHECK_EQ((int)g_fb.px[static_cast<size_t>(50) * g_fb.w + 50], 0x07E0);

    // --- 2) build a camera-relative label list -------------------------------
    CameraView cam;
    cam.worldPos[0] = 5; cam.worldPos[1] = 5; cam.worldPos[2] = 5;
    cam.trans[0] = 1;    cam.trans[1] = 1;    cam.trans[2] = 1;
    LabelObject labels[2];
    labels[0].visible = true;
    labels[0].posA[0] = 8; labels[0].posA[1] = 9; labels[0].posA[2] = 10;
    labels[0].posB[0] = 2; labels[0].posB[1] = 3; labels[0].posB[2] = 4;
    labels[1].visible = false;   // skipped
    LabelEntry le[2];
    i32 nLabels = render::ComputeLabelEntries(cam, labels, 2, le, 2);
    CHECK_EQ(nLabels, 1);
    CHECK(FEq(le[0].a[0], 3.0f) && FEq(le[0].a[1], 4.0f) && FEq(le[0].a[2], 5.0f));
    CHECK(FEq(le[0].b[0], 1.0f) && FEq(le[0].b[1], 2.0f) && FEq(le[0].b[2], 3.0f));

    // --- 3) mesh AABB through the offset transform pivot ---------------------
    SubmeshCorner sm[2] = {
        {0.0f, {-1.0f, -2.0f, -3.0f}},
        {0.0f, {4.0f, 5.0f, 6.0f}},
    };
    float A[3], B[3], Cc[3], D[3];
    i32 r3 = render::MeshDrawBoundingBox(nullptr, 4, 100.0f, sm, 2, A, B, Cc, D);
    CHECK_EQ(r3, 0);
    // min=(-1,-2,-3), max=(4,5,6); outA=(min) + 1000 offset from the pivot.
    CHECK(FEq(A[0], 999.0f) && FEq(A[1], 998.0f) && FEq(A[2], 997.0f));
    CHECK(FEq(B[0], 1004.0f) && FEq(B[1], 998.0f) && FEq(B[2], 997.0f));
    CHECK(FEq(Cc[0], 1004.0f) && FEq(Cc[1], 998.0f) && FEq(Cc[2], 1006.0f));
    CHECK(FEq(D[0], 999.0f) && FEq(D[1], 998.0f) && FEq(D[2], 1006.0f));

    // --- 4) surface rect fill (vendor path) ----------------------------------
    SurfaceView surf;
    surf.width = 64; surf.height = 64; surf.leftClampX = 0;
    int dummy = 0; surf.vendorObj = &dummy;
    SurfaceFillRect fr;
    i32 r4 = render::SurfaceColorFillRect(&surf, 5, 5, 100, 100, 0xABCD, &fr);
    CHECK_EQ(r4, 1);
    CHECK(fr.issued);
    if (fr.issued) {
        CHECK_EQ(fr.x1, 64);   // clamped to width
        CHECK_EQ(fr.y1, 64);   // clamped to height
    }
    CHECK_EQ(g_fillCount, 1);

    // Restore inert defaults for the rest of the unified binary.
    RenderLeaves6Hooks reset{};
    render::InstallRenderLeaves6Hooks(reset);
}
