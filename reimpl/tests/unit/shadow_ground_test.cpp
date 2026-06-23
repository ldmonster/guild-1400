// Golden-vector tests for the GROUND-shadow quad reconstruction (wave 7):
//   0x5f3048 VIBE_Shadow_BuildGroundShadow
//   0x5f216c VIBE_Shadow_ProjectGroundQuad
//   0x5f2a58 VIBE_Shadow_RasterizeHeightField
// Math is cross-checked against the leaf helpers in render_leaves7 (0x5f2bfb etc).
#include "render/shadow_ground.h"
#include "render/render_leaves7.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {

bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

GroundShadowPools makePools(std::vector<GroundShadowVertex>& va,
                            std::vector<GroundShadowVertex>& vb,
                            std::vector<GroundShadowDraw>& d0,
                            std::vector<GroundShadowDraw>& d1) {
    va.assign(4096, GroundShadowVertex{});
    vb.assign(4096, GroundShadowVertex{});
    d0.assign(4096, GroundShadowDraw{});
    d1.assign(4096, GroundShadowDraw{});
    GroundShadowPools p;
    p.vertexA = va.data();
    p.vertexB = vb.data();
    p.draw0 = d0.data();
    p.draw1 = d1.data();
    p.capacity = 0;  // unbounded for tests
    return p;
}

GroundShadowCaster baseCaster() {
    GroundShadowCaster c;
    c.colorKey = 0x11223344;
    c.dim = 64;
    c.hasData = 1;
    c.directional = 0;
    c.minX = 0; c.maxX = 3; c.minY = 0; c.maxY = 2;
    c.ground[0] = 100.0f; c.ground[1] = 5.0f; c.ground[2] = 200.0f;  // xBase,baseY,zBase
    c.xform0[0] = 2.0f; c.xform0[1] = 0.25f; c.xform0[2] = 3.0f;       // xStep,yScale,zStep
    c.heightStride = 64;
    c.heightBase = 0;
    return c;
}

}  // namespace

// --- 1. Per-vertex Y matches the leaf arithmetic (0x5f2bfb) ------------------
TEST(shadow_ground, HeightVertexYGolden) {
    // bias = 1.0 (high-detail off). vy = (h+1.0)*yScale + (baseY+1.0).
    const float bias = HeightFieldBias(false);
    CHECK(feq(bias, 1.0f));
    // h=40, yScale=0.25, baseY=5.0 -> (40+1)*0.25 + (5+1) = 10.25 + 6 = 16.25
    CHECK(feq(RasterizeHeightVertexY(40, 1.0f, 0.25f, 5.0f), 16.25f));
    // h=0 -> (0+1)*0.25 + 6 = 6.25
    CHECK(feq(RasterizeHeightVertexY(0, 1.0f, 0.25f, 5.0f), 6.25f));
}

// --- 2. RasterizeHeightField phase-1 vertex grid -----------------------------
TEST(shadow_ground, RasterizePhase1Grid) {
    auto c = baseCaster();
    // 4 cols (x0..x1 = 0..3), 3 rows (y0..y1 = 0..2) = 12 vertices.
    std::vector<unsigned char> h(c.dim * c.dim, 0);
    // make height = 8 everywhere so Y is uniform.
    for (auto& v : h) v = 8;
    c.heights = h.data();

    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    ShadowClipRect clip;
    CHECK_EQ(ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip), 1);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};

    char r = RasterizeHeightField(clip, c, uv, p);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(p.vertexCountA, 12);     // 4*3
    CHECK_EQ(p.drawCount0, 12);       // one per cell

    // vertex[0] = (x0*xStep+xBase, (8+1)*0.25+(5+1), y0*zStep+zBase)
    //           = (0*2+100, 2.25+6, 0*3+200) = (100, 8.25, 200)
    CHECK(feq(va[0].pos[0], 100.0f));
    CHECK(feq(va[0].pos[1], 8.25f));
    CHECK(feq(va[0].pos[2], 200.0f));
    // vertex[1] = col1: X = 1*2+100 = 102, same Y/Z
    CHECK(feq(va[1].pos[0], 102.0f));
    CHECK(feq(va[1].pos[2], 200.0f));
    // vertex[4] = row1 col0: Z = 1*3+200 = 203, X back to 100
    CHECK(feq(va[4].pos[0], 100.0f));
    CHECK(feq(va[4].pos[2], 203.0f));
    // colour key threaded into draw0 +68
    CHECK_EQ((unsigned)d0[0].color0, (unsigned)c.colorKey);
    CHECK_EQ(d0[5].payload, 5);       // +72 points at vertex slot 5
}

// --- 3. RasterizeHeightField phase-2 quad stitch + edge flag -----------------
TEST(shadow_ground, RasterizePhase2EdgeFlag) {
    auto c = baseCaster();
    std::vector<unsigned char> h(c.dim * c.dim, 0);
    c.heights = h.data();

    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    RasterizeHeightField(clip, c, uv, p);

    // (4-1)x(3-1) = 3x2 = 6 cells, two tris each = 12 draw1 records + 12 vbB.
    CHECK_EQ(p.drawCount1, 12);
    CHECK_EQ(p.vertexCountB, 12);
    // all heights equal -> no edge discontinuities flagged.
    bool anyEdge = false;
    for (int i = 0; i < p.drawCount1; ++i) if (d1[i].edgeFlag) anyEdge = true;
    CHECK(!anyEdge);
    // -1.0f sentinel written into +24.
    CHECK(feq(d1[0].biasW, -1.0f));
    // first triangle indices: (0,1,4) top-left/top-right/bottom-left
    CHECK_EQ(d1[0].v[0], 0);
    CHECK_EQ(d1[0].v[1], 1);
    CHECK_EQ(d1[0].v[2], 4);
}

// --- 4. Edge-discontinuity bit fires above 25.0 tolerance --------------------
TEST(shadow_ground, EdgeDiscontinuityFires) {
    // |hA-hB| > 25.0 -> flag set.  dbl_62C270 == 25.0.
    CHECK(!HeightEdgeDiscontinuous(10.0f, 30.0f, 12.0f));   // 20 <= 25, 2 <= 25
    CHECK(HeightEdgeDiscontinuous(10.0f, 40.0f, 12.0f));    // 30 > 25
    CHECK(HeightEdgeDiscontinuous(10.0f, 12.0f, 50.0f));    // 40 > 25

    auto c = baseCaster();
    // Build a height field with a cliff between col0 and col1 of row0.
    std::vector<unsigned char> h(c.dim * c.dim, 0);
    // heights interact via Y = (h+1)*0.25 + 6, so to exceed 25 in Y space need
    // big h delta: h diff of 200 -> Y diff = 200*0.25 = 50 > 25.
    h[0] = 0; h[1] = 200; h[2] = 0; h[3] = 0;
    c.heights = h.data();

    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    RasterizeHeightField(clip, c, uv, p);

    // cell (0,0) tri1 uses verts 0,1,4: |Y0-Y1| = 50 > 25 -> edge flag set.
    CHECK_EQ((int)d1[0].edgeFlag, 2);
}

// --- 5. BuildGroundShadow dispatch: heightfield path -------------------------
TEST(shadow_ground, BuildDispatchHeightfield) {
    auto c = baseCaster();
    std::vector<unsigned char> h(c.dim * c.dim, 4);
    c.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    // hfEnabled + no tile -> RasterizeHeightField path, emits the grid.
    char r = BuildGroundShadow(c, /*hfEnabled*/ true, /*tile*/ nullptr, p);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(p.vertexCountA, 12);
    CHECK(p.drawCount1 > 0);
}

// --- 6. BuildGroundShadow gates: directional/no-data/empty rect reject -------
TEST(shadow_ground, BuildGatesReject) {
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;

    {  // directional caster -> skip heightfield, falls to flat-quad path
        auto c = baseCaster();
        c.directional = 1;
        c.quad[0] = 0; c.quad[1] = 1; c.quad[2] = 0; c.quad[3] = 1; c.quad[4] = 9;
        auto p = makePools(va, vb, d0, d1);
        char r = BuildGroundShadow(c, true, nullptr, p);
        CHECK_EQ((int)r, 1);            // flat quad emitted
        CHECK_EQ(p.vertexCountA, 4);    // 4 corners (four put() calls)
        // 0x5f34ad: the original bumps drawCount0 (v5[2]) by FOUR (it writes four
        // per-vertex draw0 records), and emits the two triangle COMMANDS into
        // drawPool1 indexed by drawBaseCount, bumping drawBaseCount (v5[3]) by 2.
        CHECK_EQ(p.drawCount0, 4);      // v5[2] += 4
        CHECK_EQ(p.drawBaseCount, 2);   // v5[3] += 2
        CHECK_EQ(d1[0].v[0], 0);        // tri 1 vertex indices (base+0/1/2)
        CHECK_EQ(d1[0].v[1], 1);
        CHECK_EQ(d1[0].v[2], 2);
        CHECK_EQ(d1[1].v[0], 1);        // tri 2 vertex indices (base+1/3/2)
        CHECK_EQ(d1[1].v[1], 3);
        CHECK_EQ(d1[1].v[2], 2);
    }
    {  // hfEnabled false -> flat-quad path too
        auto c = baseCaster();
        std::vector<unsigned char> h(c.dim * c.dim, 0);
        c.heights = h.data();
        auto p = makePools(va, vb, d0, d1);
        char r = BuildGroundShadow(c, false, nullptr, p);
        CHECK_EQ((int)r, 1);
        CHECK_EQ(p.vertexCountA, 4);
    }
    {  // empty rect (maxX <= minX) -> reject with 0
        auto c = baseCaster();
        c.maxX = c.minX;               // maxX - minX <= 0 -> 0x5f3138 reject
        std::vector<unsigned char> h(c.dim * c.dim, 0);
        c.heights = h.data();
        auto p = makePools(va, vb, d0, d1);
        char r = BuildGroundShadow(c, true, nullptr, p);
        CHECK_EQ((int)r, 0);
        CHECK_EQ(p.vertexCountA, 0);
    }
}

// ===========================================================================
// WAVE-10 HARDENING — degenerate clip rects, null heights, capacity gates. The
// clip clamp (ComputeShadowClipRect) keeps the height-field walk in [0,dim);
// these drive the single-cell, single-row/column and null-height boundaries so
// ASAN exercises the vertexA/vertexB/draw0/draw1 indexing and heights[] reads.
// ===========================================================================

// Single CELL (1x1 quad): 2x2 vertices, 1 cell, 2 tris. Tightest non-empty case.
TEST(shadow_ground, RasterizeSingleCell) {
    auto c = baseCaster();
    c.minX = 0; c.maxX = 1; c.minY = 0; c.maxY = 1;
    std::vector<unsigned char> h(c.dim * c.dim, 12);
    c.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    ShadowClipRect clip;
    CHECK_EQ(ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip), 1);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    CHECK_EQ((int)RasterizeHeightField(clip, c, uv, p), 1);
    CHECK_EQ(p.vertexCountA, 4);     // 2x2
    CHECK_EQ(p.drawCount1, 2);       // 1 cell -> 2 triangles
    CHECK_EQ(p.vertexCountB, 2);
}

// Single COLUMN clip (x1 == x0): phase-1 emits one vertex per row, phase-2's
// `if (v37 > 0)` guard skips all stitching -> no draw1 emitted, no OOB.
TEST(shadow_ground, RasterizeSingleColumnNoStitch) {
    auto c = baseCaster();
    std::vector<unsigned char> h(c.dim * c.dim, 3);
    c.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    // Hand-build a degenerate clip: x0==x1==0, y0..y1 == 0..2 (3 rows).
    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 0; clip.y0 = 0; clip.y1 = 2;
    clip.uOff = 0; clip.uStep = 1; clip.vOff = 0; clip.vStep = 1;
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    CHECK_EQ((int)RasterizeHeightField(clip, c, uv, p), 1);
    CHECK_EQ(p.vertexCountA, 3);     // 1 col x 3 rows
    CHECK_EQ(p.drawCount1, 0);       // v37 == 0 -> no quads stitched
}

// Single ROW clip (y1 == y0): phase-2's `if (v36 > 0)` guard skips stitching.
TEST(shadow_ground, RasterizeSingleRowNoStitch) {
    auto c = baseCaster();
    std::vector<unsigned char> h(c.dim * c.dim, 3);
    c.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 3; clip.y0 = 0; clip.y1 = 0;   // 4 cols x 1 row
    clip.uOff = 0; clip.uStep = 1; clip.vOff = 0; clip.vStep = 1;
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    CHECK_EQ((int)RasterizeHeightField(clip, c, uv, p), 1);
    CHECK_EQ(p.vertexCountA, 4);     // 4 cols x 1 row
    CHECK_EQ(p.drawCount1, 0);       // v36 == 0 -> no quads
}

// NULL height buffer: the `heights ? heights[hIdx] : 0` guard supplies 0 so the
// phase-1 walk never dereferences a null pointer. Y collapses to (0+bias)*scale.
TEST(shadow_ground, RasterizeNullHeights) {
    auto c = baseCaster();
    c.heights = nullptr;             // no height buffer
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    CHECK_EQ((int)RasterizeHeightField(clip, c, uv, p), 1);
    CHECK_EQ(p.vertexCountA, 12);
    // h==0 everywhere -> uniform Y == (0+1)*0.25 + (5+1) = 6.25.
    CHECK(feq(va[0].pos[1], 6.25f));
}

// Capacity gate: a too-small dword_64A7EC budget rejects the heightfield path
// before any emission (0x5f31fa). 2*cap <= 2*v18*v17 + drawBaseCount -> reject.
TEST(shadow_ground, BuildCapacityGateRejects) {
    auto c = baseCaster();              // footprint 4x3 -> v17=3, v18=2
    std::vector<unsigned char> h(c.dim * c.dim, 4);
    c.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    // 2*v18*v17 = 2*2*3 = 12; set cap so 2*cap <= 12 (cap <= 6) -> reject.
    p.capacity = 6;
    char r = BuildGroundShadow(c, /*hfEnabled*/ true, /*tile*/ nullptr, p);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(p.vertexCountA, 0);        // nothing emitted

    // Just above the gate (cap large) -> emits normally, in-bounds.
    auto p2 = makePools(va, vb, d0, d1);
    p2.capacity = 100000;
    CHECK_EQ((int)BuildGroundShadow(c, true, nullptr, p2), 1);
    CHECK_EQ(p2.vertexCountA, 12);
}

// Flat-quad capacity gate: 2*cap-2 <= drawBaseCount rejects the fallback quad.
TEST(shadow_ground, FlatQuadCapacityGateRejects) {
    auto c = baseCaster();
    c.directional = 1;                  // -> flat-quad path
    c.quad[0] = 0; c.quad[1] = 1; c.quad[2] = 0; c.quad[3] = 1; c.quad[4] = 9;
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    p.capacity = 2;                     // 2*2-2 = 2 <= drawBaseCount(0)? no...
    p.drawBaseCount = 5;               // ...make 2 <= 5 true -> reject
    char r = BuildGroundShadow(c, true, nullptr, p);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(p.vertexCountA, 0);
}

// --- 7. ProjectGroundQuad null-tile fallthrough (0x5f3286 guard) -------------
TEST(shadow_ground, ProjectGroundQuadNullTile) {
    auto c = baseCaster();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    ComputeShadowClipRect(c.dim, c.minX, c.maxX, c.minY, c.maxY, &clip);
    float uv[4] = {clip.uOff, clip.uStep, clip.vOff, clip.vStep};
    CHECK_EQ((int)ProjectGroundQuad(nullptr, clip, c, uv, p), 0);
    CHECK_EQ(p.vertexCountA, 0);
}

// ===========================================================================
// WAVE-17 — ProjectGroundQuad (0x5f216c) projected-terrain-tile path, golden-
// pinned against the decompile with synthetic tile inputs (the live in-game path:
// the caller passes the terrain scene record dword_64A028; here a portable view).
// ===========================================================================
namespace {
GroundShadowTile baseTile(std::vector<GroundShadowTile::Cell>& cells,
                          int rows, int cols) {
    GroundShadowTile t;
    t.cellStride0 = 4;     // height-byte row stride (v53)
    t.cellStride  = 1;     // cell divisor (v6) -> tile coords == raw coords
    t.xBase = 100.0f; t.baseY = 5.0f; t.zBase = 200.0f;
    t.xStep = 2.0f; t.zStep = 3.0f; t.yScale = 0.25f;
    cells.assign(static_cast<size_t>(rows * cols), GroundShadowTile::Cell{});
    t.grid = cells.data();
    t.gridRows = rows;
    t.gridCols = cols;
    return t;
}
}  // namespace

// Minimal single-vertex case: clip {0,0,0,0}, cellStride 1, one enabled cell.
// v33=v44=v35=v45=0; v54=v9=0 -> phase1 emits exactly ONE vertex; v25=0 and
// gridRowCount=0 -> phase2 emits nothing.  v76 -> 1.
TEST(shadow_ground, ProjectGroundQuadSingleVertex) {
    auto c = baseCaster();
    std::vector<GroundShadowTile::Cell> cells;
    auto t = baseTile(cells, /*rows*/ 8, /*cols*/ 8);
    cells[0].stepByte = 1;          // cell (0,0) participates (v30=1)
    cells[0].enable   = 0;          // windingA
    cells[0].vertBase = 0;          // v59

    std::vector<unsigned char> h(64, 40);   // height 40 everywhere
    t.heights = h.data();

    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 0; clip.y0 = 0; clip.y1 = 0;
    float uv[4] = {0.0f, 0.5f, 0.0f, 0.5f};  // uBase,uStep,vBase,vStep
    CHECK_EQ((int)ProjectGroundQuad(&t, clip, c, uv, p), 1);
    CHECK_EQ(p.vertexCountA, 1);
    CHECK_EQ(p.drawCount0, 1);
    CHECK_EQ(p.drawCount1, 0);       // no stitched triangles
    CHECK_EQ(p.vertexCountB, 0);
    // vertex: X = x0*xStep + xBase = 0*2+100 = 100
    //         Z = y0*zStep + zBase = 0*3+200 = 200
    //         Y = h*yScale + (baseY+0.5) = 40*0.25 + 5.5 = 15.5
    CHECK(feq(va[0].pos[0], 100.0f));
    CHECK(feq(va[0].pos[2], 200.0f));
    CHECK(feq(va[0].pos[1], 15.5f));
    // colorKey threaded into draw0 +68; +72 -> vertex slot 0.
    CHECK_EQ((unsigned)d0[0].color0, (unsigned)c.colorKey);
    CHECK_EQ(d0[0].payload, 0);
}

// A clip {0,1,0,1} with a single enabled cell -> phase1 emits a 2x2 vertex grid
// (v54=v9=1, step 1) and phase2 stitches one quad (two triangles) with windingA.
TEST(shadow_ground, ProjectGroundQuadQuadStitch) {
    auto c = baseCaster();
    std::vector<GroundShadowTile::Cell> cells;
    auto t = baseTile(cells, 8, 8);
    // Only cell (0,0) participates; clip max == 1 means v44/v33 == 1 != v49/v45 at
    // (0,0), so the cell-end raw coords v9/v54 are v6*(0+1) == 1 -> 2x2 verts.
    cells[0].stepByte = 1;
    cells[0].enable   = 0;          // windingA -> tri (TL,BR,BL)
    cells[0].vertBase = 0;

    std::vector<unsigned char> h(64, 0);
    t.heights = h.data();

    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);

    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 1; clip.y0 = 0; clip.y1 = 1;
    float uv[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    CHECK_EQ((int)ProjectGroundQuad(&t, clip, c, uv, p), 1);
    // phase1: 2x2 = 4 vertices for cell (0,0).
    CHECK(p.vertexCountA >= 4);
    // phase2 of cell (0,0): v25 = (1-0)/1 = 1, gridRowCount = (1-0)/1 = 1 -> one
    // grid row, one column -> two triangles (drawCount1 += 2), two vertexB recs.
    CHECK(p.drawCount1 >= 2);
    CHECK(p.vertexCountB >= 2);
    // windingA tri indices: TL, BR, BL == (v29*0+0, v29*1+1, v29*1+0) with v29=2
    //   -> (0, 3, 2).
    CHECK_EQ(d1[0].v[0], 0);
    CHECK_EQ(d1[0].v[1], 3);
    CHECK_EQ(d1[0].v[2], 2);
    // -1.0f sentinel + material key.
    CHECK(feq(d1[0].biasW, -1.0f));
    CHECK_EQ((unsigned)d1[0].matA, (unsigned)c.colorKey);
}

// Winding flip: enable bit7 set -> the `*v27 >= 0` branch takes layout B
// (TL,BL,BR) instead of (TL,BR,BL).
TEST(shadow_ground, ProjectGroundQuadWindingFlip) {
    auto c = baseCaster();
    std::vector<GroundShadowTile::Cell> cells;
    auto t = baseTile(cells, 8, 8);
    cells[0].stepByte = 1;
    cells[0].enable   = 0x80;       // bit7 set -> windingB
    cells[0].vertBase = 0;
    std::vector<unsigned char> h(64, 0);
    t.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 1; clip.y0 = 0; clip.y1 = 1;
    float uv[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    CHECK_EQ((int)ProjectGroundQuad(&t, clip, c, uv, p), 1);
    // windingB: TL, BL, BR == (0, 2, 3).
    CHECK_EQ(d1[0].v[0], 0);
    CHECK_EQ(d1[0].v[1], 2);
    CHECK_EQ(d1[0].v[2], 3);
}

// No participating cell (all stepByte == 0) -> nothing emitted, returns 0.
TEST(shadow_ground, ProjectGroundQuadEmptyGrid) {
    auto c = baseCaster();
    std::vector<GroundShadowTile::Cell> cells;
    auto t = baseTile(cells, 8, 8);    // all cells stepByte == 0
    std::vector<unsigned char> h(64, 0);
    t.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 1; clip.y0 = 0; clip.y1 = 1;
    float uv[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    CHECK_EQ((int)ProjectGroundQuad(&t, clip, c, uv, p), 0);
    CHECK_EQ(p.vertexCountA, 0);
    CHECK_EQ(p.drawCount1, 0);
}

// Outer guard: v33 < v45 (clip max row below min row) -> immediate 0, no emit.
TEST(shadow_ground, ProjectGroundQuadOuterGuard) {
    auto c = baseCaster();
    std::vector<GroundShadowTile::Cell> cells;
    auto t = baseTile(cells, 8, 8);
    cells[0].stepByte = 1;
    std::vector<unsigned char> h(64, 0);
    t.heights = h.data();
    std::vector<GroundShadowVertex> va, vb;
    std::vector<GroundShadowDraw> d0, d1;
    auto p = makePools(va, vb, d0, d1);
    ShadowClipRect clip;
    clip.x0 = 0; clip.x1 = 1; clip.y0 = 5; clip.y1 = 0;   // y1/v6=0 < y0/v6=5
    float uv[4] = {0.0f, 0.5f, 0.0f, 0.5f};
    CHECK_EQ((int)ProjectGroundQuad(&t, clip, c, uv, p), 0);
    CHECK_EQ(p.vertexCountA, 0);
}
