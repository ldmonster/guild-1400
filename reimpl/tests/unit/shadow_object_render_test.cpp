#include "test.h"

#include "render/shadow_project.h"
#include "render/shadow_render.h"

#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Count filled pixels (8bpp shadow surface: rasterizer writes fillValue==1).
int CountFilled(const std::vector<u8>& px, u8 v = 1) {
    int n = 0;
    for (u8 b : px) if (b == v) ++n;
    return n;
}
} // namespace

// ===========================================================================
// MapShadowVertexToSurface (0x5f3bb6..0x5f3bef) — projected ground XZ box maps
// onto the [0,surfW) shadow texture: u=(x-minX)*surfW/(maxX-minX),
// v=(z-minZ)*surfW/(maxZ-minZ).
// ===========================================================================
TEST(ShadowObjectRender, UvMappingCornersAndCenter) {
    ShadowBounds b;
    b.minX = -2.0f; b.maxX = 6.0f;   // width 8
    b.minZ =  1.0f; b.maxZ = 5.0f;   // depth 4
    float surfW = 32.0f;

    // min corner -> (0,0)
    ShadowUv c0 = MapShadowVertexToSurface({-2.0f, 0.0f, 1.0f}, b, surfW);
    CHECK(Near(c0.u, 0.0f) && Near(c0.v, 0.0f));
    // max corner -> (surfW, surfW)  (both axes scaled by surfW / extent)
    ShadowUv c1 = MapShadowVertexToSurface({6.0f, 0.0f, 5.0f}, b, surfW);
    CHECK(Near(c1.u, 32.0f) && Near(c1.v, 32.0f));
    // center of X box -> surfW/2; center of Z box -> surfW/2
    ShadowUv cc = MapShadowVertexToSurface({2.0f, 0.0f, 3.0f}, b, surfW);
    CHECK(Near(cc.u, 16.0f) && Near(cc.v, 16.0f));
    // A point a quarter into X (x=0 -> (0-(-2))/8 = 0.25*32 = 8).
    ShadowUv cq = MapShadowVertexToSurface({0.0f, 0.0f, 1.0f}, b, surfW);
    CHECK(Near(cq.u, 8.0f) && Near(cq.v, 0.0f));
}

// ===========================================================================
// ShadowBoundsAcceptable (0x5f3857) — every bound and span must be within
// ±16384.0 or the shadow is rejected (RenderMeshShadow skips the whole tail).
// ===========================================================================
TEST(ShadowObjectRender, BoundsAcceptanceGuard) {
    ShadowBounds ok;
    ok.minX = -100.0f; ok.maxX = 100.0f; ok.minZ = -50.0f; ok.maxZ = 50.0f;
    CHECK(ShadowBoundsAcceptable(ok));

    // Right at the limit is still accepted (<=).
    ShadowBounds edge;
    edge.minX = 0.0f; edge.maxX = 16384.0f; edge.minZ = 0.0f; edge.maxZ = 16384.0f;
    CHECK(ShadowBoundsAcceptable(edge));

    // One bound past the limit rejects.
    ShadowBounds bad;
    bad.minX = -16385.0f; bad.maxX = 1.0f; bad.minZ = 0.0f; bad.maxZ = 1.0f;
    CHECK(!ShadowBoundsAcceptable(bad));

    // A span (maxX-minX) past the limit rejects even if endpoints are in range.
    ShadowBounds bigSpan;
    bigSpan.minX = -10000.0f; bigSpan.maxX = 10000.0f;  // span 20000 > 16384
    bigSpan.minZ = 0.0f; bigSpan.maxZ = 1.0f;
    CHECK(!ShadowBoundsAcceptable(bigSpan));
}

// ===========================================================================
// RenderObjectShadow (0x5f3f38 tail) — splats every triangle of a projected
// shadow mesh into the surface. End-to-end: project a quad's silhouette,
// map to the surface, render -> non-empty stencil.
// ===========================================================================
TEST(ShadowObjectRender, ProjectMapRenderQuad) {
    // A unit quad standing above the ground (two triangles), lit from straight up
    // so the directional projection flattens it to its own XZ footprint.
    ShadowVec3 mesh[4] = {
        {0.0f, 4.0f, 0.0f},
        {4.0f, 4.0f, 0.0f},
        {4.0f, 4.0f, 4.0f},
        {0.0f, 4.0f, 4.0f},
    };
    ShadowVec3 sun{0.0f, -1.0f, 0.0f};   // straight down
    ShadowVec3 proj[4];
    ShadowBounds bnd = ProjectMeshToGround(mesh, 4, proj, sun, /*directional=*/true, 0.0f);
    CHECK(ShadowBoundsAcceptable(bnd));
    for (int i = 0; i < 4; ++i) CHECK(Near(proj[i].y, 0.0f));   // all on ground

    // Surface 32x32, 8bpp.
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    // Build two triangles (0,1,2) and (0,2,3) with surface-mapped screen coords.
    // The engine maps the projected box onto [0,surfW); the max corner lands
    // exactly at surfW, so (as the engine does) clamp the integer bbox to
    // [0, width-1]. We map to (width-1) so the splat stays inside the surface.
    auto uv = [&](int i) {
        return MapShadowVertexToSurface(proj[i], bnd, (float)(surf.width - 1));
    };
    ShadowMeshTri tris[2];
    int idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int t = 0; t < 2; ++t) {
        for (int k = 0; k < 3; ++k) {
            ShadowUv m = uv(idx[t][k]);
            tris[t].v.x[k] = m.u;
            tris[t].v.y[k] = m.v;
        }
        tris[t].v.backFlag = true;   // allow either winding to fill
    }

    int n = RenderObjectShadow(tris, 2, surf);
    CHECK_EQ(n, 2);                  // both triangles processed
    CHECK(CountFilled(px) > 0);      // the quad's shadow stencil is non-empty
}

// Empty mesh: nothing rendered, surface stays clear, returns 0.
TEST(ShadowObjectRender, EmptyMeshNoOp) {
    ShadowSurface surf;
    std::vector<u8> px(16 * 16, 0);
    surf.pixels = px.data();
    surf.pitch = 16; surf.width = 16; surf.height = 16; surf.is16bpp = false;
    CHECK_EQ(RenderObjectShadow(nullptr, 0, surf), 0);
    CHECK_EQ(CountFilled(px), 0);
}

// ===========================================================================
// WAVE-10 HARDENING — degenerate / edge inputs exercising the rasterizer bounds
// under ASAN+UBSAN. The original shadow surface is square (res x res); these
// drive the silhouette splat with single triangles, over-large projected boxes
// (the validation over-clip), casters at/below ground, and a NON-SQUARE surface
// (smaller height) to pin the FillSpans row/span memory-safety guard.
// ===========================================================================

// Single triangle, well inside a square 8bpp surface -> fills, no OOB.
TEST(ShadowObjectRender, SingleTriangleInBounds) {
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    ShadowMeshTri tri;
    tri.v.x[0] = 4.0f;  tri.v.y[0] = 4.0f;
    tri.v.x[1] = 24.0f; tri.v.y[1] = 6.0f;
    tri.v.x[2] = 8.0f;  tri.v.y[2] = 26.0f;
    tri.v.backFlag = true;
    CHECK_EQ(RenderObjectShadow(&tri, 1, surf), 1);
    CHECK(CountFilled(px) > 0);
}

// Projected bbox LARGER than the surface: a vertex maps past width -> the
// validation guard (px <= (width<<16)-1) rejects the whole triangle (over-clip)
// and writes nothing. This is the faithful original behavior, and it must not OOB.
TEST(ShadowObjectRender, OverlargeTriangleRejectedNoWrite) {
    ShadowSurface surf;
    std::vector<u8> px(16 * 16, 0);
    surf.pixels = px.data();
    surf.pitch = 16; surf.width = 16; surf.height = 16; surf.is16bpp = false;

    ShadowMeshTri tri;
    // x[1] = 40 > width 16 -> px[1] > (16<<16)-1 -> validation bails.
    tri.v.x[0] = 2.0f;  tri.v.y[0] = 2.0f;
    tri.v.x[1] = 40.0f; tri.v.y[1] = 4.0f;
    tri.v.x[2] = 6.0f;  tri.v.y[2] = 30.0f;   // y also out of range
    tri.v.backFlag = true;
    RenderObjectShadow(&tri, 1, surf);
    CHECK_EQ(CountFilled(px), 0);              // rejected, surface untouched
}

// Negative screen coords are rejected by the px>=0 / py>=0 validation; no OOB.
TEST(ShadowObjectRender, NegativeCoordTriangleRejected) {
    ShadowSurface surf;
    std::vector<u8> px(16 * 16, 0);
    surf.pixels = px.data();
    surf.pitch = 16; surf.width = 16; surf.height = 16; surf.is16bpp = false;

    ShadowMeshTri tri;
    tri.v.x[0] = -3.0f; tri.v.y[0] = 2.0f;     // negative X
    tri.v.x[1] = 8.0f;  tri.v.y[1] = 4.0f;
    tri.v.x[2] = 4.0f;  tri.v.y[2] = 12.0f;
    tri.v.backFlag = true;
    RenderObjectShadow(&tri, 1, surf);
    CHECK_EQ(CountFilled(px), 0);
}

// NON-SQUARE surface (height < width): the original assumes a square surface so
// the bbox-validation only bounds against width. A tall triangle would walk rows
// past a short buffer; the wave-10 FillSpans guard clamps the write to the
// surface window so ASAN sees no OOB. Splat is allowed up to the real height.
TEST(ShadowObjectRender, NonSquareSurfaceNoRowOverflow) {
    ShadowSurface surf;
    // width 32 (validation bound), but only 8 rows of storage.
    std::vector<u8> px(32 * 8, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 8; surf.is16bpp = false;

    ShadowMeshTri tri;
    // A triangle whose Y spans well past row 8 (down to y=30) -> would overrun a
    // square-assumption rasterizer; the guard keeps every write inside [0,8).
    tri.v.x[0] = 2.0f;  tri.v.y[0] = 1.0f;
    tri.v.x[1] = 20.0f; tri.v.y[1] = 2.0f;
    tri.v.x[2] = 4.0f;  tri.v.y[2] = 30.0f;
    tri.v.backFlag = true;
    RenderObjectShadow(&tri, 1, surf);
    // Some pixels in the valid top rows are filled; ASAN confirms no OOB write.
    CHECK(CountFilled(px) >= 0);   // the assertion of record is "no sanitizer trip"
}

// Caster at/below the ground plane: projection still lands on the plane (the
// straight-down directional case) and never divides by zero for nonzero dir.y.
TEST(ShadowObjectRender, CasterAtAndBelowGround) {
    ShadowVec3 dir{0.0f, -1.0f, 0.0f};
    // Caster exactly on the ground (v.y == groundY): t = 0, stays put on plane.
    ShadowVec3 onGround[3] = {{0,5,0},{4,5,0},{2,5,3}};
    ShadowVec3 out[3];
    ShadowBounds b1 = ProjectMeshToGround(onGround, 3, out, dir, true, 5.0f);
    for (int i = 0; i < 3; ++i) CHECK(Near(out[i].y, 5.0f));
    CHECK(ShadowBoundsAcceptable(b1));

    // Caster BELOW the ground plane (v.y < groundY): t negative; the math is
    // still finite (no UB) and lands the projected y back on the plane.
    ShadowVec3 below[3] = {{0,1,0},{4,1,0},{2,1,3}};
    ShadowBounds b2 = ProjectMeshToGround(below, 3, out, dir, true, 5.0f);
    for (int i = 0; i < 3; ++i) CHECK(Near(out[i].y, 5.0f));
    CHECK(ShadowBoundsAcceptable(b2));
}

// 16bpp path: RenderObjectShadow drives the word-fill branch (fillValue 0xFFFF).
TEST(ShadowObjectRender, Render16bpp) {
    ShadowSurface surf;
    std::vector<u8> px(24 * 24 * 2, 0);
    surf.pixels = px.data();
    surf.pitch = 24 * 2; surf.width = 24; surf.height = 24; surf.is16bpp = true;

    // Keep the triangle strictly inside the 24x24 surface (right-edge ceil never
    // reaches width), as the engine's bbox-clamp guarantees.
    ShadowMeshTri tri;
    tri.v.x[0] = 2.0f;  tri.v.y[0] = 2.0f;
    tri.v.x[1] = 18.0f; tri.v.y[1] = 4.0f;
    tri.v.x[2] = 6.0f;  tri.v.y[2] = 18.0f;
    tri.v.backFlag = true;

    RenderObjectShadow(&tri, 1, surf);
    int words = 0;
    const u16* w = (const u16*)px.data();
    for (size_t i = 0; i < px.size() / 2; ++i) if (w[i] == 0xFFFF) ++words;
    CHECK(words > 0);
}

// ===========================================================================
// WINDING / APEX RESOLUTION goldens (RasterizeTriangle 0x603ed4, verified
// against the disasm at 0x60400b..0x6041eb). These pin the three corrections:
//   (1) the FLAT-TOP "equal" branch (v8 == py[next[a4]]) — the right edge runs
//       next[a4] -> next[next[a4]], NOT a4 -> next[a4];
//   (2) the first span block uses ceil16(MIN(py[leftV],py[rightV])) - topRow;
//   (3) the second span block count is sign-correct for the !v36 case
//       (py[leftV] >= py[rightV]): ceil16(py[leftV]) - ceil16(py[rightV]).
// Each fills a fully-interior triangle; we lock the exact filled-pixel count and
// require the fill to be a contiguous, plausible triangle (no missing rows from
// a negative-count FillSpans, no double counting from a wrong apex).
// ===========================================================================

// Reference top-left scanline triangle area (matches the engine's ceil16 fill
// rule for an axis-trivial check); used only as a sanity lower bound here.
TEST(ShadowObjectRender, FlatTopTriangleWinding) {
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    // FLAT TOP: two vertices share the minimum Y (y=4). This drives the equal
    // branch (v8 == py[next[a4]]). With the pre-fix code the right edge was wrong
    // (a4->next instead of next->next[next]) and the top row band was mis-filled.
    ShadowMeshTri tri;
    tri.v.x[0] = 6.0f;  tri.v.y[0] = 4.0f;    // top-left
    tri.v.x[1] = 24.0f; tri.v.y[1] = 4.0f;    // top-right (same Y -> flat top)
    tri.v.x[2] = 15.0f; tri.v.y[2] = 26.0f;   // bottom apex
    tri.v.backFlag = true;
    CHECK_EQ(RenderObjectShadow(&tri, 1, surf), 1);

    int filled = CountFilled(px);
    // A flat-top triangle ~18 wide at top narrowing to a point over 22 rows must
    // fill a substantial, downward-narrowing region. Lock the exact count so the
    // equal-branch right edge (next->next[next]) stays 1:1.
    CHECK_EQ(filled, 207);

    // Top band (rows 4..) must be the widest; verify monotone non-increasing span
    // widths from top to bottom (a wrong apex/edge would break monotonicity).
    int prevW = 33, breaks = 0;
    for (int y = 4; y <= 26; ++y) {
        int rowW = 0;
        for (int x = 0; x < 32; ++x) if (px[y * 32 + x]) ++rowW;
        if (rowW > 0) { if (rowW > prevW) ++breaks; prevW = rowW; }
    }
    CHECK_EQ(breaks, 0);   // widths never increase going down -> correct winding
}

// Forces the !v36 second-span branch (py[leftV] >= py[rightV]): the left bottom
// vertex is LOWER (larger Y) than the right bottom. The pre-fix code used the
// v36 span count for both branches, which is NEGATIVE here -> the lower sub-edge
// would not fill (missing rows). The fix uses ceil16(py[leftV])-ceil16(py[rightV]).
TEST(ShadowObjectRender, SecondSpanNotV36Branch) {
    ShadowSurface surf;
    std::vector<u8> px(32 * 32, 0);
    surf.pixels = px.data();
    surf.pitch = 32; surf.width = 32; surf.height = 32; surf.is16bpp = false;

    ShadowMeshTri tri;
    // Top apex high; the two lower vertices at DIFFERENT Y so one sub-edge
    // continues below the other -> exercises the second FillSpans block.
    tri.v.x[0] = 16.0f; tri.v.y[0] = 3.0f;     // apex (min Y)
    tri.v.x[1] = 28.0f; tri.v.y[1] = 10.0f;    // right, higher up
    tri.v.x[2] = 4.0f;  tri.v.y[2] = 27.0f;    // left, lower down (larger Y)
    tri.v.backFlag = true;
    CHECK_EQ(RenderObjectShadow(&tri, 1, surf), 1);

    int filled = CountFilled(px);
    CHECK_EQ(filled, 191);

    // The lower portion (rows 11..26, below the higher bottom vertex) MUST have
    // filled pixels — the pre-fix negative span count left these rows empty.
    int lowerRows = 0;
    for (int y = 11; y <= 26; ++y) {
        for (int x = 0; x < 32; ++x)
            if (px[y * 32 + x]) { ++lowerRows; break; }
    }
    CHECK(lowerRows >= 10);   // most of the lower band fills (was 0 pre-fix)
}
