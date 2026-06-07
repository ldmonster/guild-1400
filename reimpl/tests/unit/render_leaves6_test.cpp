#include "test.h"

#include "render/render_leaves6.h"

#include <cmath>
#include <cstdint>
#include <cstring>

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

// Capture state for the drawSpan hook.
struct SpanCap {
    bool called = false;
    i32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    i16 color = 0;
};
SpanCap g_span;
i32 CaptureSpan(i32 x0, i32 y0, i32 x1, i32 y1, i16 color) {
    g_span.called = true;
    g_span.x0 = x0; g_span.y0 = y0; g_span.x1 = x1; g_span.y1 = y1;
    g_span.color = color;
    return x0 + 1000;   // distinct return to prove it propagated
}

void InstallSpan() {
    g_span = SpanCap{};
    RenderLeaves6Hooks h{};
    h.drawSpan = &CaptureSpan;
    render::InstallRenderLeaves6Hooks(h);
}
void ResetHooks() {
    RenderLeaves6Hooks h{};   // all-null => all inert defaults
    render::InstallRenderLeaves6Hooks(h);
}

} // namespace

// ---------------------------------------------------------------------------
// DrawLineClipped — golden vectors computed with a python oracle replaying the
// exact decompiled arithmetic (round-toward-zero truncation, edge clip order).
// Bounds {b40,b44,b48,b4C} = {10,10,200,200}.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_DrawLineClipped, InsideDrawsVerbatim) {
    InstallSpan();
    ClipBounds c{10, 10, 200, 200};
    i32 r = render::DrawLineClipped(c, 50, 50, 150, 160, 7);
    CHECK(g_span.called);
    if (g_span.called) {
        CHECK_EQ(g_span.x0, 50);
        CHECK_EQ(g_span.y0, 50);
        CHECK_EQ(g_span.x1, 150);
        CHECK_EQ(g_span.y1, 160);
        CHECK_EQ(g_span.color, (i16)7);
    }
    CHECK_EQ(r, 1050);   // CaptureSpan returns x0+1000
    ResetHooks();
}

TEST(RenderLeaves6_DrawLineClipped, ClipsX0LowToBound) {
    InstallSpan();
    ClipBounds c{10, 10, 200, 200};
    i32 r = render::DrawLineClipped(c, 5, 50, 150, 160, 3);
    CHECK(g_span.called);
    if (g_span.called) {
        CHECK_EQ(g_span.x0, 10);    // clipped to b40
        CHECK_EQ(g_span.y0, 53);    // interpolated (oracle)
        CHECK_EQ(g_span.x1, 150);
        CHECK_EQ(g_span.y1, 160);
    }
    CHECK_EQ(r, 1010);
    ResetHooks();
}

TEST(RenderLeaves6_DrawLineClipped, ClipsY0LowToBound) {
    InstallSpan();
    ClipBounds c{10, 10, 200, 200};
    i32 r = render::DrawLineClipped(c, 50, 5, 150, 160, 9);
    CHECK(g_span.called);
    if (g_span.called) {
        CHECK_EQ(g_span.x0, 53);    // interpolated
        CHECK_EQ(g_span.y0, 10);    // clipped to b44
        CHECK_EQ(g_span.x1, 150);
        CHECK_EQ(g_span.y1, 160);
    }
    CHECK_EQ(r, 1053);
    ResetHooks();
}

TEST(RenderLeaves6_DrawLineClipped, RejectReturnsNoDraw) {
    InstallSpan();
    // With bounds {640,480,0,0} a typical inside line is trivially rejected by
    // the verbatim guard chain (returns the running value, no draw).
    ClipBounds c{640, 480, 0, 0};
    i32 r = render::DrawLineClipped(c, 100, 100, 200, 300, 1);
    CHECK(!g_span.called);
    CHECK_EQ(r, 100);   // result == initial x0
    ResetHooks();
}

// ---------------------------------------------------------------------------
// MeshDrawBoundingBox — AABB min/max over the resident submesh corners, then
// 4 corners through the (inert identity) transform pivot.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_MeshAabb, NonClassFourEarlyOut) {
    ResetHooks();
    SubmeshCorner sm[1] = {{0.0f, {1, 2, 3}}};
    float a[3], b[3], cc[3], d[3];
    i32 r = render::MeshDrawBoundingBox(nullptr, /*class*/3, 100.0f, sm, 1,
                                        a, b, cc, d);
    CHECK_EQ(r, 1);
}

TEST(RenderLeaves6_MeshAabb, MinMaxAndCorners) {
    ResetHooks();   // identity transform pivot
    // Three submeshes; the middle one is filtered out by the LOD cutoff.
    SubmeshCorner sm[3] = {
        {0.0f, {-2.0f, 5.0f, 1.0f}},     // resident
        {99.0f, {100.0f, 100.0f, 100.0f}}, // lod 99 > cutoff 10 => skipped
        {1.0f, {4.0f, -3.0f, 7.0f}},     // resident
    };
    float A[3], B[3], C[3], D[3];
    i32 r = render::MeshDrawBoundingBox(nullptr, 4, 10.0f, sm, 3, A, B, C, D);
    CHECK_EQ(r, 0);
    // min = (-2,-3,1) ; max = (4,5,7)
    // outA = (min0,min1,min2) ; outB=(max0,min1,min2)
    // outC = (max0,min1,max2) ; outD=(min0,min1,max2)
    CHECK(FEq(A[0], -2.0f) && FEq(A[1], -3.0f) && FEq(A[2], 1.0f));
    CHECK(FEq(B[0], 4.0f)  && FEq(B[1], -3.0f) && FEq(B[2], 1.0f));
    CHECK(FEq(C[0], 4.0f)  && FEq(C[1], -3.0f) && FEq(C[2], 7.0f));
    CHECK(FEq(D[0], -2.0f) && FEq(D[1], -3.0f) && FEq(D[2], 7.0f));
}

// ---------------------------------------------------------------------------
// SurfaceColorFillRect — clamp math, both vendor and linear paths.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_SurfaceFill, NullSurfaceReturnsZero) {
    ResetHooks();
    SurfaceFillRect rect;
    i32 r = render::SurfaceColorFillRect(nullptr, 0, 0, 10, 10, 0, &rect);
    CHECK_EQ(r, 0);
}

TEST(RenderLeaves6_SurfaceFill, VendorPathClampsRect) {
    ResetHooks();
    SurfaceView s;
    s.width = 100; s.height = 80; s.leftClampX = 5;
    int dummy = 0;
    s.vendorObj = &dummy;   // non-null => vendor path
    SurfaceFillRect rect;
    // x=-3 (clamped to leftClampX=5), y=10, h=200 (y1 clamps to 80), w=200 (x1 clamps to 100)
    i32 r = render::SurfaceColorFillRect(&s, -3, 10, 200, 200, 0xFF, &rect);
    CHECK_EQ(r, 1);
    CHECK(rect.issued);
    if (rect.issued) {
        CHECK_EQ(rect.x0, 5);
        CHECK_EQ(rect.y0, 10);
        CHECK_EQ(rect.x1, 100);
        CHECK_EQ(rect.y1, 80);
    }
}

TEST(RenderLeaves6_SurfaceFill, VendorOriginOutOfBoundsNotIssued) {
    ResetHooks();
    SurfaceView s;
    s.width = 100; s.height = 80; s.leftClampX = 0;
    int dummy = 0;
    s.vendorObj = &dummy;
    SurfaceFillRect rect;
    // x == width => out of bounds, no issue.
    i32 r = render::SurfaceColorFillRect(&s, 100, 10, 5, 5, 0, &rect);
    CHECK_EQ(r, 1);
    CHECK(!rect.issued);
}

TEST(RenderLeaves6_SurfaceFill, LinearPathFullSurface) {
    ResetHooks();
    SurfaceView s;
    s.width = 64; s.height = 48; s.vendorObj = nullptr;   // linear path
    SurfaceFillRect rect;
    i32 r = render::SurfaceColorFillRect(&s, 10, 10, 5, 5, 0, &rect);
    CHECK_EQ(r, 1);
    CHECK(rect.issued);
    if (rect.issued) {
        CHECK_EQ(rect.x0, 0);
        CHECK_EQ(rect.y0, 0);
        CHECK_EQ(rect.x1, 64);
        CHECK_EQ(rect.y1, 48);
    }
}

// ---------------------------------------------------------------------------
// ComputeLabelEntries / ComputeMarkerEntry — camera-relative subtract.
// ---------------------------------------------------------------------------
TEST(RenderLeaves6_Labels, CameraRelativeSubtractAndSkip) {
    ResetHooks();
    CameraView cam;
    cam.worldPos[0] = 10; cam.worldPos[1] = 20; cam.worldPos[2] = 30;
    cam.trans[0] = 1;     cam.trans[1] = 2;      cam.trans[2] = 3;

    LabelObject labels[3];
    labels[0].visible = true;
    labels[0].posA[0] = 15; labels[0].posA[1] = 25; labels[0].posA[2] = 35;
    labels[0].posB[0] = 4;  labels[0].posB[1] = 6;  labels[0].posB[2] = 9;
    labels[1].visible = false;   // skipped
    labels[2].visible = true;
    labels[2].posA[0] = 0;  labels[2].posA[1] = 0;  labels[2].posA[2] = 0;
    labels[2].posB[0] = 1;  labels[2].posB[1] = 2;  labels[2].posB[2] = 3;

    LabelEntry out[3];
    i32 n = render::ComputeLabelEntries(cam, labels, 3, out, 3);
    CHECK_EQ(n, 2);   // label[1] skipped
    CHECK(FEq(out[0].a[0], 5.0f) && FEq(out[0].a[1], 5.0f) && FEq(out[0].a[2], 5.0f));
    CHECK(FEq(out[0].b[0], 3.0f) && FEq(out[0].b[1], 4.0f) && FEq(out[0].b[2], 6.0f));
    // label[2] posA == cam.worldPos? no: posA=0 => a = -cam.worldPos
    CHECK(FEq(out[1].a[0], -10.0f) && FEq(out[1].a[1], -20.0f) && FEq(out[1].a[2], -30.0f));
    CHECK(FEq(out[1].b[0], 0.0f) && FEq(out[1].b[1], 0.0f) && FEq(out[1].b[2], 0.0f));
}

TEST(RenderLeaves6_Labels, CapacityCap) {
    ResetHooks();
    CameraView cam;
    LabelObject labels[3];
    for (auto& l : labels) l.visible = true;
    LabelEntry out[2];
    i32 n = render::ComputeLabelEntries(cam, labels, 3, out, 2);
    CHECK_EQ(n, 2);   // capped at cap
}

TEST(RenderLeaves6_Markers, CameraRelativeSubtract) {
    ResetHooks();
    CameraView cam;
    cam.worldPos[0] = 100; cam.worldPos[1] = 200; cam.worldPos[2] = 300;
    cam.trans[0] = 7;      cam.trans[1] = 8;       cam.trans[2] = 9;
    float wA[3] = {110, 210, 310};
    float wB[3] = {17, 18, 19};
    LabelEntry e = render::ComputeMarkerEntry(cam, wA, wB);
    CHECK(FEq(e.a[0], 10.0f) && FEq(e.a[1], 10.0f) && FEq(e.a[2], 10.0f));
    CHECK(FEq(e.b[0], 10.0f) && FEq(e.b[1], 10.0f) && FEq(e.b[2], 10.0f));
}
