// Unit tests for guild::render frustum cull/clip-flag classification and the
// camera view-scale coord helper.
//   cull.{h,cpp}        VIBE_Render_ComputeVertexClipFlags  @0x5ad614
//   coord_view.{h,cpp}  VIBE_Coord_ComputeViewScale         @0x4525b4
// Also exercises the EXISTING guild::sim::CoordDistance3D (VIBE_Coord_Distance3D
// @0x4865b4, owned by sim/combat_escape) over a synthetic heightmap — reused per
// the ODR rule rather than re-translated.
//
// Golden vectors for the clip-flag outcodes and the view-scale clamp were
// computed independently in python (see the module report).
#include "render/cull.h"
#include "render/coord_view.h"
#include "render/geometry_types.h"
#include "sim/combat_escape.h"
#include "test.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Build the test frustum: the four side planes are axis-aligned half-spaces so
// the outcodes are hand-verifiable.
//   plane0 (a=1,c=0,d=0)  outside (bit0) when x < 0
//   plane1 (a=-1,c=0,d=0) outside (bit1) when x > 0
//   plane2 (b=1,c=0,d=0)  outside (bit2) when y < 0
//   plane3 (b=-1,c=0,d=0) outside (bit3) when y > 0
//   near z < 1.0 (bit4);  far z > 100.0 (bit5)
Frustum MakeFrustum() {
    Frustum f;
    std::memset(&f, 0, sizeof(f));
    f.plane[0][0] = 1.0f;
    f.plane[1][0] = -1.0f;
    f.plane[2][1] = 1.0f;
    f.plane[3][1] = -1.0f;
    f.nearZ = 1.0f;
    f.farZ = 100.0f;
    return f;
}

Vertex MakeVtx(float x, float y, float z) {
    Vertex v;
    std::memset(&v, 0, sizeof(v));
    v.x = x; v.y = y; v.z = z;
    return v;
}

} // namespace

// ----- ComputeVertexClipFlags: per-vertex outcode golden ---------------------
TEST(RenderCullUnit, VertexOutcodesGolden) {
    Frustum f = MakeFrustum();
    std::vector<Vertex> v = {
        MakeVtx(10.0f, 10.0f, 50.0f),    // x>0 -> bit1 ; y>0 -> bit3  => 0x0a
        MakeVtx(-10.0f, -10.0f, 50.0f),  // x<0 -> bit0 ; y<0 -> bit2  => 0x05
        MakeVtx(0.0f, 0.0f, 50.0f),      // on planes (not <)          => 0x00
        MakeVtx(10.0f, 10.0f, 0.5f),     // + near (z<1)               => 0x1a
        MakeVtx(10.0f, 10.0f, 200.0f),   // + far  (z>100)             => 0x2a
    };
    // No polygons in this pass: just classify the vertices.
    ComputeVertexClipFlags(kClipOutMask, v.data(), (i32)v.size(),
                           nullptr, 0, f);
    CHECK_EQ(v[0].clipFlags, (u8)0x0a);
    CHECK_EQ(v[1].clipFlags, (u8)0x05);
    CHECK_EQ(v[2].clipFlags, (u8)0x00);
    CHECK_EQ(v[3].clipFlags, (u8)0x1a);
    CHECK_EQ(v[4].clipFlags, (u8)0x2a);
}

// ----- plane mask only updates the masked bits -------------------------------
TEST(RenderCullUnit, PlaneMaskSelectsBits) {
    Frustum f = MakeFrustum();
    Vertex v = MakeVtx(10.0f, 10.0f, 0.5f);  // would be 0x1a under full mask
    // Test only near + far (0x30): x/y planes not evaluated -> only near bit set.
    ComputeVertexClipFlags((u8)(kClipNear | kClipFar), &v, 1, nullptr, 0, f);
    CHECK_EQ(v.clipFlags, (u8)0x10);  // z<near only

    // Test only the side planes (0x0f): near/far untouched (re-zeroed each call).
    ComputeVertexClipFlags((u8)0x0f, &v, 1, nullptr, 0, f);
    CHECK_EQ(v.clipFlags, (u8)0x0a);  // x>0 (bit1) + y>0 (bit3)
}

// ----- polygon keep/cull flags + vertex "kept" marker ------------------------
TEST(RenderCullUnit, PolygonKeepAndCullGolden) {
    Frustum f = MakeFrustum();
    std::vector<Vertex> v = {
        MakeVtx(10.0f, 10.0f, 50.0f),    // 0x0a
        MakeVtx(-10.0f, -10.0f, 50.0f),  // 0x05
        MakeVtx(0.0f, 0.0f, 50.0f),      // 0x00
        MakeVtx(10.0f, 10.0f, 0.5f),     // 0x1a
        MakeVtx(10.0f, 10.0f, 200.0f),   // 0x2a
    };
    std::vector<Polygon> p(2);
    std::memset(p.data(), 0, p.size() * sizeof(Polygon));
    // poly0: verts 0,1,2 -> AND of outcodes == 0 -> KEPT, flags36 = 0x8f.
    p[0].v0 = &v[0]; p[0].v1 = &v[1]; p[0].v2 = &v[2];
    // poly1: verts 3,4,3 -> shared bits (1,3) nonzero -> culled, flags36 = 0.
    p[1].v0 = &v[3]; p[1].v1 = &v[4]; p[1].v2 = &v[3];

    ComputeVertexClipFlags(kClipOutMask, v.data(), (i32)v.size(),
                           p.data(), (i32)p.size(), f);

    CHECK_EQ(p[0].flags36, (u8)0x8f);   // (0x0a|0x05|0x00) | 0x80
    CHECK_EQ(p[1].flags36, (u8)0x00);   // culled

    // poly0's three vertices are marked kept (bit7); poly1's are not.
    CHECK((v[0].clipFlags & kClipKept) != 0);
    CHECK((v[1].clipFlags & kClipKept) != 0);
    CHECK((v[2].clipFlags & kClipKept) != 0);
    CHECK((v[3].clipFlags & kClipKept) == 0);
    CHECK((v[4].clipFlags & kClipKept) == 0);
}

// ----- skip-flagged polygon (flags38 bit1) is left with flags36 == 0 ---------
TEST(RenderCullUnit, SkipFlaggedPolygonIgnored) {
    Frustum f = MakeFrustum();
    Vertex v[3] = { MakeVtx(0,0,50), MakeVtx(0,0,50), MakeVtx(0,0,50) };
    Polygon p;
    std::memset(&p, 0, sizeof(p));
    p.v0 = &v[0]; p.v1 = &v[1]; p.v2 = &v[2];
    p.flags36 = 0x55;          // pre-set garbage; must be cleared to 0
    p.flags38 = 0x02;          // bit1 set -> skip the keep test
    ComputeVertexClipFlags(kClipOutMask, v, 3, &p, 1, f);
    CHECK_EQ(p.flags36, (u8)0x00);
    CHECK((v[0].clipFlags & kClipKept) == 0);
}

// ----- null v0 polygon ignored ----------------------------------------------
TEST(RenderCullUnit, NullPolygonIgnored) {
    Frustum f = MakeFrustum();
    Polygon p;
    std::memset(&p, 0, sizeof(p));
    p.v0 = nullptr;
    p.flags36 = 0x77;
    ComputeVertexClipFlags(kClipOutMask, nullptr, 0, &p, 1, f);
    CHECK_EQ(p.flags36, (u8)0x00);
}

// ----- wave-12 boundary hardening --------------------------------------------
// Zero vertex AND zero polygon counts: both loops must not execute (no read of
// the null base pointers).  ASAN/UBSAN clean = no spurious deref at count 0.
TEST(RenderCullUnit, ZeroCountsNoAccess) {
    Frustum f = MakeFrustum();
    ComputeVertexClipFlags(kClipOutMask, nullptr, 0, nullptr, 0, f);
    CHECK(true);  // reaching here without a sanitizer trap is the assertion
}

// Extreme / huge coordinates: every per-plane test is a plain compare, so
// FLT_MAX and -FLT_MAX classify deterministically with no overflow/UB.
TEST(RenderCullUnit, ExtremeCoordsClassify) {
    Frustum f = MakeFrustum();
    const float big = 3.0e38f;
    Vertex v[2] = { MakeVtx(big, big, big), MakeVtx(-big, -big, -big) };
    ComputeVertexClipFlags(kClipOutMask, v, 2, nullptr, 0, f);
    // (+big): x>0 bit1, y>0 bit3, z>far bit5 => 0x2a
    CHECK_EQ(v[0].clipFlags, (u8)0x2a);
    // (-big): x<0 bit0, y<0 bit2, z<near bit4 => 0x15
    CHECK_EQ(v[1].clipFlags, (u8)0x15);
}

// A "zero-extent" triangle (all three verts identical, fully inside): the AND of
// equal outcodes (0) is 0 -> kept, OR is 0 -> flags36 == 0x80 (kept bit only).
TEST(RenderCullUnit, DegenerateTriangleKept) {
    Frustum f = MakeFrustum();
    Vertex v[3] = { MakeVtx(0,0,50), MakeVtx(0,0,50), MakeVtx(0,0,50) };
    Polygon p;
    std::memset(&p, 0, sizeof(p));
    p.v0 = &v[0]; p.v1 = &v[1]; p.v2 = &v[2];
    ComputeVertexClipFlags(kClipOutMask, v, 3, &p, 1, f);
    CHECK_EQ(p.flags36, (u8)0x80);   // (0|0|0) | kept
}

// ----- ComputeViewScale clamp golden -----------------------------------------
TEST(RenderCullUnit, ViewScaleClampGolden) {
    CHECK_EQ(ComputeViewScale(0.0f),    (i32)5);
    CHECK_EQ(ComputeViewScale(100.0f),  (i32)5);   // 0.6 -> trunc 0 -> 5
    CHECK_EQ(ComputeViewScale(1000.0f), (i32)6);   // 6.0
    CHECK_EQ(ComputeViewScale(1500.0f), (i32)9);   // 9.0
    CHECK_EQ(ComputeViewScale(2000.0f), (i32)12);  // 12.0
    CHECK_EQ(ComputeViewScale(2667.0f), (i32)16);  // 16.002 -> trunc 16
    CHECK_EQ(ComputeViewScale(3000.0f), (i32)16);  // 18 -> clamp 16
    CHECK_EQ(ComputeViewScale(5000.0f), (i32)16);  // clamp
    CHECK_EQ(ComputeViewScale(-1000.0f),(i32)5);   // -6 -> clamp 5
}

// ----- reuse sim::CoordDistance3D against a synthetic heightmap ---------------
TEST(RenderCullUnit, CoordDistance3DSimpleGrid) {
    // 4x4 flat heightmap, 1 world-unit per tile, height byte 0 everywhere.
    Heightmap hm;
    std::memset(&hm, 0, sizeof(hm));
    hm.originX = 0.0f; hm.originY = 0.0f; hm.originZ = 0.0f;
    hm.scaleX = 1.0f;  hm.scaleY = 1.0f;  hm.scaleZ = 1.0f;
    hm.size = 4;
    std::vector<u8> heights(16, 0);
    hm.heights = heights.data();

    // (0,0) vs (3,0): pure X separation of 3 world units.
    double d = guild::sim::CoordDistance3D(&hm, 0, 0, 3, 0);
    CHECK(std::fabs(d - 3.0) < 1e-4);

    // (1,1) vs (2,3): dx=1, dz=2 -> sqrt(5).
    double d2 = guild::sim::CoordDistance3D(&hm, 1, 1, 2, 3);
    CHECK(std::fabs(d2 - std::sqrt(5.0)) < 1e-4);
}
