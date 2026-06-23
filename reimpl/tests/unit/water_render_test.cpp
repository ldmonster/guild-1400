// Unit tests for render/water_render — the WATER RENDER ARM of the city terrain
// pass (VIBE_Floor_TransformTileGeometry @0x5be668). Golden vectors for:
//   * ComputeWaterShade  (0.5 - |normalize(axisH).(0,0,1)|, flt_628B2C/flt_5CA2B8)
//   * BuildWaterVertexPos (base + waveOut.xyz + waterHeight*axisH -> world)
//   * RenderWaterSurface  (the span/poly build + project + append over a
//     synthetic water region built by render::BuildWaterRegions)
//
// All goldens hand-traced from the 0x5be668 decompile.
#include "test.h"

#include "render/water_render.h"
#include "render/terrain_walk.h"   // TerrainRenderState
#include "render/floorwater.h"     // BuildWaterRegions
#include "render/water_vertices.h" // WaterMesh / AnimateWaterVertices

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using guild::render::ComputeWaterShade;
using guild::render::BuildWaterVertexPos;
using guild::render::RenderWaterSurface;
using guild::render::WaterDrawInput;
using guild::render::WaterDrawStats;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
} // namespace

// ---------------------------------------------------------------------------
// ComputeWaterShade: 0.5 - |normalize(axisH) . (0,0,1)|.
// ---------------------------------------------------------------------------
TEST(WaterRender, ShadeAxisAlignedZ) {
    // axisH along +Z: normalized = (0,0,1); dot with (0,0,1) = 1; 0.5 - 1 = -0.5.
    float h[3] = {0, 0, 5.0f};
    CHECK(feq(ComputeWaterShade(h), -0.5f));
}

TEST(WaterRender, ShadePerpendicular) {
    // axisH along +Y: dot with (0,0,1) = 0; shade = 0.5 - 0 = 0.5.
    float h[3] = {0, 3.0f, 0};
    CHECK(feq(ComputeWaterShade(h), 0.5f));
}

TEST(WaterRender, ShadeZeroVectorUnchanged) {
    // A zero axis: VectorNormalize @0x5cb148 zeroes it (loc_5CB1A0), dot 0 -> 0.5.
    float h[3] = {0, 0, 0};
    CHECK(feq(ComputeWaterShade(h), 0.5f));
}

// ---------------------------------------------------------------------------
// BuildWaterVertexPos: pos = axisH*wh + wave.xyz + base.
// ---------------------------------------------------------------------------
TEST(WaterRender, VertexPosCombinesBaseWaveHeight) {
    float base[3]   = {10.0f, 20.0f, 30.0f};
    float axisH[3]  = {0.0f, 2.0f, 0.0f};      // height axis (view space)
    float wave[4]   = {0.5f, -0.25f, 0.75f, 9.0f};  // .w is the depth-only term
    i16   wh        = 3;
    float out[3];
    BuildWaterVertexPos(base, axisH, wave, wh, /*ampBase*/ 0.0f, out);
    // x = 0*3 + 0.5 + 10 = 10.5 ; y = 2*3 + (-0.25) + 20 = 25.75 ; z = 0*3 + 0.75 + 30
    CHECK(feq(out[0], 10.5f));
    CHECK(feq(out[1], 25.75f));
    CHECK(feq(out[2], 30.75f));
}

TEST(WaterRender, VertexPosZeroEverything) {
    float base[3]  = {0, 0, 0};
    float axisH[3] = {1, 1, 1};
    float wave[4]  = {0, 0, 0, 0};
    float out[3];
    BuildWaterVertexPos(base, axisH, wave, /*wh*/ 0, 0.0f, out);
    CHECK(feq(out[0], 0.0f));
    CHECK(feq(out[1], 0.0f));
    CHECK(feq(out[2], 0.0f));
}

// ---------------------------------------------------------------------------
// RenderWaterSurface over a synthetic water region: a 2x2 water block in an 8x8
// floor builds one region; the arm must build vertices, polys and append them.
// ---------------------------------------------------------------------------
namespace {
render::TerrainRenderState MakeFlatView() {
    render::TerrainRenderState st;
    // An OBLIQUE view (like the engine's tilted city camera): origin pushed in Z,
    // axisU = +X (per column moves screen X), axisV carries BOTH +Y (screen-down
    // depth) and a small +Z so different rows project to different screen Y (a flat
    // top-down view with axisV purely +Z would map the water sheet to a zero-area
    // line). axisH = +Y (height lifts screen Y).
    st.originView[0] = -4.0f; st.originView[1] = -4.0f; st.originView[2] = 100.0f;
    st.axisU[0] = 1; st.axisU[1] = 0;    st.axisU[2] = 0;
    st.axisV[0] = 0; st.axisV[1] = 1.0f; st.axisV[2] = 0.1f;
    st.axisH[0] = 0; st.axisH[1] = 1;    st.axisH[2] = 0;
    // Project: 1/z perspective (z ~100 -> on-screen).
    st.projXScale = 100.0f; st.projXOff = 80.0f;
    st.projYScale = 100.0f; st.projYOff = 60.0f;
    // neutral light
    for (int k = 0; k < 3; ++k) { st.lightScale[k] = 1; st.ambient[k] = 40; st.shadowBias[k] = 0; }
    return st;
}
} // namespace

TEST(WaterRender, BuildsAndAppendsWaterRegion) {
    const int n = 8;
    const u8 kWater = 7;
    std::vector<u8> grid((std::size_t)n * n, 0);
    // A 2x2 water block well inside the interior.
    for (int r = 2; r <= 3; ++r)
        for (int c = 2; c <= 3; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 50);

    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.anyWater);
    CHECK(wr.regionCount >= 1);
    CHECK(!wr.waterMeshes.empty());
    CHECK(!wr.spans.empty());

    render::TerrainRenderState st = MakeFlatView();

    WaterDrawInput in{};
    in.size = n;
    in.mask = n - 1;
    in.waterHeights = nullptr;     // built fresh inside the builder; arm samples 0
    in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data();
    in.spanCount = (int)wr.spans.size();
    in.polys = wr.polys.empty() ? nullptr : wr.polys.data();
    in.polyCount = (int)wr.polys.size();

    std::vector<render::Vertex> vbuf(4096);
    std::vector<render::Polygon> pbuf(8192);
    std::vector<render::DrawListEntry> dlbuf(8192);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};

    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl,
                                          /*waterTexture*/ nullptr);
    CHECK(s.spansWalked > 0);
    CHECK(s.vertsBuilt > 0);
    CHECK(s.polysBuilt > 0);
    // At least some polys survive the cull + on-screen test and get appended.
    CHECK(dl.count > 0);
    CHECK_EQ(s.appended, dl.count);
    // Headless (null texture) -> sort key 0 on every appended entry.
    for (int i = 0; i < dl.count; ++i)
        CHECK_EQ((int)dlbuf[(size_t)i].sortKey, 0);
}

// ---------------------------------------------------------------------------
// Empty / no-water inputs render nothing (the byte-identical no-water path).
// ---------------------------------------------------------------------------
TEST(WaterRender, NoSpansAppendsNothing) {
    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = 8; in.mask = 7;
    float meshes[86] = {0};
    in.meshes = meshes;
    in.meshCount = 1;
    in.spans = nullptr; in.spanCount = 0;
    std::vector<render::Vertex> vbuf(16);
    std::vector<render::Polygon> pbuf(16);
    std::vector<render::DrawListEntry> dlbuf(16);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, nullptr);
    CHECK_EQ(s.appended, 0);
    CHECK_EQ(dl.count, 0);
}

// ---------------------------------------------------------------------------
// Animation feeds the render arm: after AnimateWaterVertices advances the wave
// grid, the SAME RenderWaterSurface produces moved water vertex positions.
// ---------------------------------------------------------------------------
TEST(WaterRender, AnimatedWaveMovesVertices) {
    const int n = 8;
    const u8 kWater = 7;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 2; r <= 4; ++r)
        for (int c = 2; c <= 4; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 55);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();
    std::vector<render::Vertex> vbuf(4096);
    std::vector<render::Polygon> pbuf(8192);
    std::vector<render::DrawListEntry> dlbuf(8192);

    // frame 1: animate to t=100, render.
    auto bridgeAnimate = [&](i32 t) {
        const u32 cnt = wr.regionCount;
        std::vector<render::WaterMesh> ms((std::size_t)cnt);
        float* raw = reinterpret_cast<float*>(wr.waterMeshes.data());
        for (u32 i = 0; i < cnt; ++i) {
            const float* r = raw + (std::size_t)i * 86u;
            render::WaterMesh& m = ms[i]; m = render::WaterMesh{};
            for (int k = 0; k < 4; ++k) { m.waveSpeed[k] = r[6 + k]; m.amp[k] = r[10 + k]; }
            for (int k = 0; k < 64; ++k) m.waveOut[k] = r[14 + k];
            for (int k = 0; k < 4; ++k) m.phase[k] = r[78 + k];
            m.texAccumA = r[82]; m.texAccumB = r[83]; m.lastTime = (i32)r[84];
        }
        render::AnimateWaterVertices(ms.data(), cnt, t,
                                     [](i32, u8, void*) -> u32 { return 0u; }, nullptr);
        for (u32 i = 0; i < cnt; ++i) {
            float* r = raw + (std::size_t)i * 86u; const render::WaterMesh& m = ms[i];
            for (int k = 0; k < 64; ++k) r[14 + k] = m.waveOut[k];
            for (int k = 0; k < 4; ++k) r[78 + k] = m.phase[k];
            r[82] = m.texAccumA; r[83] = m.texAccumB; r[84] = (float)m.lastTime;
        }
    };
    bridgeAnimate(100);
    render::DrawList dl1{dlbuf.data(), 0, (int)dlbuf.size()};
    RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                       pbuf.data(), (int)pbuf.size(), dl1, nullptr);
    std::vector<render::Vertex> snap1(vbuf.begin(), vbuf.begin() + 64);

    // frame 2: animate to t=400 (dt>0) — the wave grid advances, vertices move.
    bridgeAnimate(400);
    render::DrawList dl2{dlbuf.data(), 0, (int)dlbuf.size()};
    RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                       pbuf.data(), (int)pbuf.size(), dl2, nullptr);
    int moved = 0;
    for (int i = 0; i < 64; ++i)
        if (!feq(snap1[(size_t)i].x, vbuf[(size_t)i].x, 1e-7f) ||
            !feq(snap1[(size_t)i].y, vbuf[(size_t)i].y, 1e-7f) ||
            !feq(snap1[(size_t)i].z, vbuf[(size_t)i].z, 1e-7f)) ++moved;
    CHECK(moved > 0);   // the animated waveOut displaced the surface vertices
}

// ---------------------------------------------------------------------------
// A non-null water texture handle carries the ((tex-base)>>7)+1 == 1 sort key.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// wave-7: the FAITHFUL FindRegionOffset point linkage. Every WaterPoly's 4 corners
// resolve through the per-tile point buffer (poly.pTL/pBR/pBL/pTR >= 0), and the
// arm threads them into exactly two triangles per poly (no (row,col) lattice
// approximation). The builder stamps the per-tile group + point indices.
// ---------------------------------------------------------------------------
TEST(WaterRender, FindRegionOffsetPointLinkage) {
    const int n = 16;
    const u8 kWater = 7;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 4; r <= 8; ++r)
        for (int c = 4; c <= 8; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 50);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);
    CHECK(!wr.polys.empty());

    // The builder resolved each poly's 4 corner POINT indices through the real
    // FindRegionOffset (off/80, per-tile). For an interior region every poly's four
    // corners land on emitted span points.
    int linked = 0;
    for (const auto& p : wr.polys)
        if (p.pTL >= 0 && p.pBR >= 0 && p.pBL >= 0 && p.pTR >= 0) ++linked;
    CHECK(linked > 0);
    CHECK_EQ(linked, (int)wr.polys.size());   // interior region: ALL corners link

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();
    in.polys = wr.polys.data(); in.polyCount = (int)wr.polys.size();

    std::vector<render::Vertex> vbuf(8192);
    std::vector<render::Polygon> pbuf(16384);
    std::vector<render::DrawListEntry> dlbuf(16384);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, nullptr);
    // The arm reports the linked polys and builds two triangles per WaterPoly.
    CHECK_EQ(s.linkedPolys, linked);
    CHECK_EQ(s.polysBuilt, 2 * (int)wr.polys.size());   // 2 tris / poly (the 80-byte
                                                        // poly == two 40-byte sub-polys)
    CHECK(s.appended > 0);
}

// ---------------------------------------------------------------------------
// wave-7: the animated EF_WASS scroll members (mesh[82]/[83] == texAccumA/B) the
// engine copies into each water poly (result+28/+32 = mesh+328/+332, @0x5beee9/
// 0x5beef5). The arm must carry the region's scroll members onto the appended
// textured polys so the textured water span can scroll the ripple.
// ---------------------------------------------------------------------------
TEST(WaterRender, ScrollMembersCopiedToPoly) {
    const int n = 16;
    const u8 kWater = 6;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 4; r <= 8; ++r)
        for (int c = 4; c <= 8; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 60);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);

    // Stamp a known scroll into region 0 (mesh[82]=texAccumA, mesh[83]=texAccumB) —
    // exactly what AnimateWaterVertices advances each frame.
    float* raw = reinterpret_cast<float*>(wr.waterMeshes.data());
    raw[82] = 0.375f;   // mesh[82] (+0x148) texAccumA
    raw[83] = 0.625f;   // mesh[83] (+0x14C) texAccumB

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = raw; in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();
    in.polys = wr.polys.data(); in.polyCount = (int)wr.polys.size();

    std::vector<render::Vertex> vbuf(8192);
    std::vector<render::Polygon> pbuf(16384);
    std::vector<render::DrawListEntry> dlbuf(16384);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    int dummyTex = 0;   // non-null handle so the scroll is reported on a textured poly
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, &dummyTex);
    CHECK(s.texturedPolys > 0);
    CHECK(feq(s.scrollU, 0.375f));   // mesh[82] reached the poly
    CHECK(feq(s.scrollV, 0.625f));   // mesh[83] reached the poly
    // and the poly records carry it (poly.uvY == mesh[83] scroll).
    bool found = false;
    for (int i = 0; i < dl.count; ++i)
        if (dlbuf[(size_t)i].poly && feq(dlbuf[(size_t)i].poly->uvY, 0.625f)) found = true;
    CHECK(found);
}

// ---------------------------------------------------------------------------
// wave-7: the EXACT sort-key arithmetic ((tex - dword_1406A84) >> 7) + 1 when a
// real texture-record base + stride are supplied (@0x5beebb). The handle at record
// index k yields key k+1.
// ---------------------------------------------------------------------------
TEST(WaterRender, SortKeyFromTexBaseRecordIndex) {
    const int n = 16;
    const u8 kWater = 5;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 4; r <= 8; ++r)
        for (int c = 4; c <= 8; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 60);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();
    in.polys = wr.polys.data(); in.polyCount = (int)wr.polys.size();

    std::vector<render::Vertex> vbuf(8192);
    std::vector<render::Polygon> pbuf(16384);
    std::vector<render::DrawListEntry> dlbuf(16384);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};

    // A synthetic 128-byte-stride record array; the water texture is record index 5.
    std::vector<u8> recArray((std::size_t)128 * 32, 0);
    const void* texBase = recArray.data();
    const void* tex = recArray.data() + 128 * 5 + 30;   // inside record 5
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, tex,
                                          texBase, /*texStride*/ 128);
    CHECK(s.appended > 0);
    for (int i = 0; i < dl.count; ++i)
        CHECK_EQ((int)dlbuf[(size_t)i].sortKey, 6);   // ((tex-base)>>7)+1 == 5+1
}

// ===========================================================================
// wave-10 HARDENING (ASAN+UBSAN): degenerate render-arm inputs.
// ===========================================================================

// A scene with NO water: meshCount 0 / empty span list -> the arm returns the
// byte-identical no-op (no allocation walk, nothing appended).
TEST(WaterRender, NoWaterMeshCountZero) {
    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = 8; in.mask = 7;
    float meshes[86] = {0};
    in.meshes = meshes;
    in.meshCount = 0;                 // no regions
    render::WaterStripSpan sp{};      // a span exists but meshCount==0 gates out
    sp.row = 1; sp.lo = 1; sp.hi = 3; sp.marker = 0; sp.tile = 0; sp.base = 0;
    in.spans = &sp; in.spanCount = 1;
    std::vector<render::Vertex> vbuf(16);
    std::vector<render::Polygon> pbuf(16);
    std::vector<render::DrawListEntry> dlbuf(16);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, nullptr);
    CHECK_EQ(s.spansWalked, 0);   // meshCount==0 -> the top guard returns early
    CHECK_EQ(s.vertsBuilt, 0);
    CHECK_EQ(s.appended, 0);
    CHECK_EQ(dl.count, 0);
}

// A span whose region marker is >= meshCount must be SKIPPED (not an OOB read into
// the 86-float mesh stride). Drives the `region >= meshCount` guard.
TEST(WaterRender, OutOfRangeRegionSkipped) {
    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = 8; in.mask = 7;
    std::vector<float> meshes(86 * 1, 0.0f);   // exactly ONE region (indices 0..85)
    in.meshes = meshes.data();
    in.meshCount = 1;
    render::WaterStripSpan sps[2] = {};
    sps[0].row = 2; sps[0].lo = 2; sps[0].hi = 4; sps[0].marker = 0;  // valid region 0
    sps[0].tile = 0; sps[0].base = 0;
    sps[1].row = 2; sps[1].lo = 2; sps[1].hi = 4; sps[1].marker = 200; // OOB region
    sps[1].tile = 0; sps[1].base = 0;
    in.spans = sps; in.spanCount = 2;
    std::vector<render::Vertex> vbuf(64);
    std::vector<render::Polygon> pbuf(64);
    std::vector<render::DrawListEntry> dlbuf(64);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, nullptr);
    CHECK_EQ(s.spansWalked, 1);   // only the valid-region span walked
}

// Capacity boundaries: tiny vertex / poly / draw-list buffers must clamp rather
// than overflow. The arm stops emitting at vbufCap/pbufCap and appending at
// dl.capacity. Built over a real region so the loops actually fill the buffers.
TEST(WaterRender, CapacityBoundariesClamp) {
    const int n = 16;
    const u8 kWater = 7;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 4; r <= 10; ++r)
        for (int c = 4; c <= 10; ++c)
            grid[c + r * n] = kWater;       // a large region -> many verts/polys
    std::vector<u8> heights((std::size_t)n * n, 50);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);
    CHECK(!wr.spans.empty());

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();
    in.polys = wr.polys.empty() ? nullptr : wr.polys.data();
    in.polyCount = (int)wr.polys.size();

    // Deliberately undersized buffers (exact-size +1 ASAN red-zone check).
    std::vector<render::Vertex> vbuf(5);
    std::vector<render::Polygon> pbuf(3);
    std::vector<render::DrawListEntry> dlbuf(2);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, nullptr);
    CHECK(s.vertsBuilt <= (int)vbuf.size());
    CHECK(s.polysBuilt <= (int)pbuf.size());
    CHECK(dl.count <= dl.capacity);
    CHECK(s.appended <= dl.capacity);

    // zero-capacity buffers: the top guard returns immediately.
    render::DrawList dl0{dlbuf.data(), 0, 0};
    WaterDrawStats s0 = RenderWaterSurface(in, st, vbuf.data(), 0,
                                           pbuf.data(), 0, dl0, nullptr);
    CHECK_EQ(s0.vertsBuilt, 0);
    CHECK_EQ(s0.appended, 0);
}

TEST(WaterRender, TexturedSortKey) {
    const int n = 8;
    const u8 kWater = 5;
    std::vector<u8> grid((std::size_t)n * n, 0);
    for (int r = 2; r <= 4; ++r)
        for (int c = 2; c <= 4; ++c)
            grid[c + r * n] = kWater;
    std::vector<u8> heights((std::size_t)n * n, 60);
    render::WaterRegions wr =
        render::BuildWaterRegions(grid.data(), heights.data(), nullptr, kWater, n);
    CHECK(wr.regionCount >= 1);

    render::TerrainRenderState st = MakeFlatView();
    WaterDrawInput in{};
    in.size = n; in.mask = n - 1; in.types = grid.data();
    in.meshes = reinterpret_cast<const float*>(wr.waterMeshes.data());
    in.meshCount = wr.regionCount;
    in.spans = wr.spans.data(); in.spanCount = (int)wr.spans.size();

    std::vector<render::Vertex> vbuf(4096);
    std::vector<render::Polygon> pbuf(8192);
    std::vector<render::DrawListEntry> dlbuf(8192);
    render::DrawList dl{dlbuf.data(), 0, (int)dlbuf.size()};
    int dummy = 0;  // a non-null texture handle
    WaterDrawStats s = RenderWaterSurface(in, st, vbuf.data(), (int)vbuf.size(),
                                          pbuf.data(), (int)pbuf.size(), dl, &dummy);
    if (dl.count > 0) {
        CHECK(s.texturedPolys > 0);
        for (int i = 0; i < dl.count; ++i)
            CHECK_EQ((int)dlbuf[(size_t)i].sortKey, 1);
    }
}
