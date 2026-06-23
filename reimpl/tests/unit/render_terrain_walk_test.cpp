#include "test.h"
#include "render/terrain_walk.h"
#include <vector>
#include <cstring>
#include <cmath>

using namespace guild;
using namespace guild::render;

// =============================================================================
// VIBE_Floor_RenderTerrain @0x5bf22c — whole-walk unit tests. Build a synthetic
// floor + 8x8 tile grid and drive each phase: light/axis setup, the per-tile LOD
// + vertex grid + quad poly build, the texture-cache binds, and the draw-list
// append. Golden values computed against the exact translated arithmetic.
// =============================================================================

namespace {

// A synthetic floor harness: owns the height/type grids, the 64 tile records, and
// per-tile vertex/poly buffers, all wired into a Floor.
struct FloorHarness {
    static constexpr i32 kSize = 32;       // grid edge (power of two)
    static constexpr i32 kSpan = 4;        // samples per tile edge (8 tiles * 4 = 32)
    std::vector<u8> heights;
    std::vector<u8> types;
    std::vector<u8> texSrc;
    std::vector<TerrainTile> tiles;
    // per-tile buffers: generous capacity (span+1)^2 verts, 2*(span)^2 polys.
    std::vector<std::vector<Vertex>>  vbufs;
    std::vector<std::vector<Polygon>> pbufs;
    std::vector<std::vector<void*>>   clipbufs;
    std::vector<std::vector<u8>>      drawbufs;
    TerrainFloor floor{};

    FloorHarness() {
        heights.assign((size_t)kSize * kSize, 0);
        types.assign((size_t)kSize * kSize, 0);
        texSrc.assign((size_t)kSize * kSize, 0);
        // A gentle height ramp + all type bytes positive (lit, high bit clear).
        for (i32 y = 0; y < kSize; ++y)
            for (i32 x = 0; x < kSize; ++x) {
                heights[(size_t)y*kSize + x] = (u8)((x + y) & 0x3F);
                types[(size_t)y*kSize + x]   = (u8)(20 + ((x*y) & 0x1F));
                texSrc[(size_t)y*kSize + x]  = (u8)((x ^ y) & 0xFF);
            }
        tiles.resize(64);
        vbufs.resize(64); pbufs.resize(64); clipbufs.resize(64); drawbufs.resize(64);
        for (int i = 0; i < 64; ++i) {
            vbufs[i].assign((size_t)(kSpan+2)*(kSpan+2), Vertex{});
            pbufs[i].assign((size_t)2*(kSpan+2)*(kSpan+2), Polygon{});
            clipbufs[i].assign((size_t)3*2*(kSpan+2)*(kSpan+2), nullptr);
            drawbufs[i].assign((size_t)24*(kSpan+2)*(kSpan+2), 0);
            tiles[i] = TerrainTile{};
            tiles[i].lod = 1;                  // default finest LOD
            tiles[i].prevLod = 0;              // force a rebuild
            tiles[i].vertexBuf = vbufs[i].data();
            tiles[i].polyBuf   = pbufs[i].data();
            tiles[i].clipList  = clipbufs[i].data();
            tiles[i].drawData  = drawbufs[i].data();
            tiles[i].clipFlag  = 0;
        }
        floor.size = kSize; floor.tileSpan = kSpan; floor.mask = kSize - 1;
        floor.heights = heights.data();
        floor.types   = types.data();
        floor.texSrc  = texSrc.data();
        for (int l = 0; l < 4; ++l) floor.mipTexSrc[l] = texSrc.data();
        floor.tiles = tiles.data();
        floor.flatLit = 1;        // bit0 = build geometry
        floor.minLodNibble = 0;
    }
};

// A texture-cache hook that records (u,v,lod) binds and returns a deterministic id.
struct TexBindLog {
    std::vector<std::tuple<i32,i32,i32>> binds;
};
u32 RecordBind(const u8* /*src*/, i32 /*w*/, i32 u, i32 v, i32 lod, void* state) {
    auto* log = (TexBindLog*)state;
    log->binds.emplace_back(u, v, lod);
    // id = a 128-aligned synthetic record so ((id/128)+1) is a clean sort key.
    return (u32)((log->binds.size()) * 128);
}

} // namespace

// ---------------------------------------------------------------------------
// Phase-0: light-param select (flat-lit vs sun).
// ---------------------------------------------------------------------------
TEST(TerrainWalkSetup, LightParamSelect) {
    TerrainRenderState st;
    float sScale[3] = {0.5f, 0.6f, 0.7f};
    float sAmb[3]   = {200.f, 200.f, 200.f};
    float sBias[3]  = {-10.f, -20.f, -30.f};

    SetupTerrainLight(st, /*flatLit*/true, sScale, sAmb, sBias);
    CHECK_EQ(st.ambient[0], 0.0f);
    CHECK_EQ(st.lightScale[1], 1.0f);
    CHECK_EQ(st.shadowBias[2], 0.0f);

    SetupTerrainLight(st, /*flatLit*/false, sScale, sAmb, sBias);
    CHECK_EQ(st.lightScale[0], 0.5f);
    CHECK_EQ(st.ambient[2], 200.0f);
    CHECK_EQ(st.shadowBias[1], -20.0f);
}

// ---------------------------------------------------------------------------
// Phase-0: axis-vector rotation through the view 3x3.
// ---------------------------------------------------------------------------
TEST(TerrainWalkSetup, RotateFloorAxisIdentity) {
    // Identity view rotation (row-major view+396.. == I).
    float mat[16] = {0};
    mat[0]=1; mat[5]=1; mat[10]=1;   // +396/+416/+436 diag (view col mapping)
    float in[3] = {2.f, 3.f, 4.f};
    float out[3];
    RotateFloorAxis(in, mat, out);
    // out[0] uses m[0]/m[4]/m[8] = 1/0/0 -> 2; out[1] uses m[1]/m[5]/m[9]=0/1/0 -> 3;
    // out[2] uses m[2]/m[6]/m[10]=0/0/1 -> 4.
    CHECK_EQ(out[0], 2.0f);
    CHECK_EQ(out[1], 3.0f);
    CHECK_EQ(out[2], 4.0f);
}

// ---------------------------------------------------------------------------
// Gate: engine off or null floor -> no work.
// ---------------------------------------------------------------------------
TEST(TerrainWalkGate, ClosedGate) {
    FloorHarness h;
    TerrainRenderState st;
    st.engineOn = 0;
    TerrainWalkHooks hooks;
    CHECK_EQ(RenderTerrain(&h.floor, st, hooks, false), 0);
    st.engineOn = 1;
    CHECK_EQ(RenderTerrain(nullptr, st, hooks, false), 0);
}

// ---------------------------------------------------------------------------
// Phase-1 Pass A: per-tile vertex grid built; subdiv product cached; tiles with
// lod==0 zero their counts.
// ---------------------------------------------------------------------------
TEST(TerrainWalkPassA, VertexGridAndSubdiv) {
    FloorHarness h;
    TerrainRenderState st;
    // Axis: height -> +Y only; tiles laid in X/Z.
    st.axisH[1] = 1.0f;
    st.axisU[0] = 1.0f;
    st.axisV[2] = 1.0f;
    st.lightScale[0]=st.lightScale[1]=st.lightScale[2]=1.0f;
    st.ambient[0]=st.ambient[1]=st.ambient[2]=10.0f;

    // Set one tile to lod 0 (a hole) -> its counts must zero.
    h.tiles[0].lod = 0;

    std::vector<DrawListEntry> dl(4096);
    DrawList out{dl.data(), 0, (i32)dl.size()};
    st.drawList = &out;

    TexBindLog log;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    hooks.getOrBuildTile   = RecordBind;
    hooks.cacheState       = &log;

    i32 appended = RenderTerrain(&h.floor, st, hooks, false);

    // lod==0 tile: subdiv + poly counts zeroed.
    CHECK_EQ(h.tiles[0].subdivCached, 0);
    CHECK_EQ(h.tiles[0].polyCount, 0);

    // An interior lod-1 tile: subdiv product = v179*v180. span=4, lod=1 ->
    // base = 4/1+1 = 5; interior (col/row != 7) so cols=rows=5 -> product 25.
    TerrainTile& interior = h.tiles[2*8 + 2];
    CHECK_EQ(interior.subdivCached, 25);
    // Its vertices were built: vertex[0] world = origin (acc) since height axis only
    // affects Y. Light bytes = clamp(1*l + 10).
    Vertex& v0 = h.vbufs[2*8+2][0];
    // type at the tile's first cell
    i32 cell = (2*h.kSpan + h.kSize*(2*h.kSpan)) & h.floor.mask;
    u8 ty = h.types[cell];
    float l = (float)(ty & 0x7F) * 2.0f;
    CHECK_EQ((int)v0.lightIdx, (int)ClampLightByte(1.0f*l + 10.0f));

    // Texture binds happened for the rebuilt quads.
    CHECK(!log.binds.empty());
    // Polys were appended to the draw list.
    CHECK(appended > 0);
    CHECK_EQ(out.count, appended);

    // The build-geometry bit was cleared.
    CHECK_EQ((int)(h.floor.flatLit & 1), 0);
}

// ---------------------------------------------------------------------------
// Phase-1 Pass A: edge tile (col 7) subdivision stitch term applied.
// ---------------------------------------------------------------------------
TEST(TerrainWalkPassA, EdgeTileSubdiv) {
    FloorHarness h;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(4096);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };

    RenderTerrain(&h.floor, st, hooks, false);

    // col 7, row 2: v180 = base - 4/lod (idx==7 on col axis), v179 = base (row!=7).
    // span=4, lod=1: base=5; cols(v180)=5-4=1? but TileSubdivCount(col,true) edge ->
    // wait: col axis uses isCol=true -> idx==col==7 -> base-4 = 1. v179 row term: row!=7
    // -> base 5. product = 1*5 = 5.
    TerrainTile& edge = h.tiles[2*8 + 7];
    CHECK_EQ(edge.subdivCached, 5);
}

// ---------------------------------------------------------------------------
// Phase-2: draw-list capacity clamp — never overflow the list.
// ---------------------------------------------------------------------------
TEST(TerrainWalkAppend, CapacityClamp) {
    FloorHarness h;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;

    // Tiny draw list: only 3 entries of capacity.
    std::vector<DrawListEntry> dl(3);
    DrawList out{dl.data(), 0, (i32)dl.size()};
    st.drawList = &out;

    TexBindLog log;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    hooks.getOrBuildTile = RecordBind;
    hooks.cacheState = &log;

    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    // Never exceed capacity.
    CHECK(out.count <= out.capacity);
    CHECK_EQ(appended, out.count);
    CHECK(appended <= 3);
}

// ---------------------------------------------------------------------------
// Phase-2: sort key derivation ((tex/stride)+1) and per-frame counters.
// ---------------------------------------------------------------------------
TEST(TerrainWalkAppend, SortKeyAndCounters) {
    FloorHarness h;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    st.texBaseStride = 0x80;

    std::vector<DrawListEntry> dl(4096);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;

    TexBindLog log;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    hooks.getOrBuildTile = RecordBind;     // ids are multiples of 128
    hooks.cacheState = &log;

    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended > 0);
    // Every appended textured poly's key = id/128 + 1, so keys are >= 2 (id>=128).
    bool anyTextured = false;
    for (i32 i = 0; i < out.count; ++i) {
        if (out.entries[i].sortKey != 0) { anyTextured = true; CHECK(out.entries[i].sortKey >= 2u); }
    }
    CHECK(anyTextured);
    // Per-frame counters accumulated.
    CHECK(st.vertsThisFrame > 0);
    CHECK_EQ(st.vertsThisFrame, st.vertsAlt);
    CHECK(st.polysThisFrame > 0);
}

// ---------------------------------------------------------------------------
// updateVisibility == 0: geometry NOT rebuilt (the !updated branch only re-projects
// already-built tiles; here poly buffers stay as the prior frame). With a fresh
// harness (prevLod != lod) the subdiv product is still cached, but no quad build
// happens, so no polys are appended.
// ---------------------------------------------------------------------------
TEST(TerrainWalkPassA, NoRebuildWhenUnchanged) {
    FloorHarness h;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(4096);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;

    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 0; };   // nothing changed

    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    // No quad poly build => polyCount stays 0 => nothing appended.
    CHECK_EQ(appended, 0);
    CHECK_EQ(out.count, 0);
}

// ---------------------------------------------------------------------------
// Pass A quad WINDING golden (wave-5 re-decompile of @0x5bf22c lines 730..786).
// For a lod-1 interior tile the row stride is v180 = span/lod+1 = 5, so the
// vertex grid indices are i00=(r,c), i01=(r,c+1), i10=(r+1,c), i11=(r+1,c+1)
// with i01=i00+1, i10=i00+5, i11=i00+6. The first quad (r=c=0) of a tile must
// wind:
//   split >= 0 (TL-BR):  tri0=(i00,i11,i10)  tri1=(i00,i01,i11)
//   split <  0 (BL-TR):  tri0=(i10,i00,i01)  tri1=(i01,i11,i10)
// This is the genuine engine winding (front-facing from above under the Pass-C
// signed-area cull) — the wave-4 row-mirror hack is removed.
// ---------------------------------------------------------------------------
TEST(TerrainWalkPassA, QuadWindingGolden) {
    FloorHarness h;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(4096);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };

    // --- Branch A: slope buffer non-negative -> split >= 0 (TL-BR diagonal) ---
    // (FloorHarness fills texSrc with (x^y)&0xFF < 0x40, high bit clear -> v494>=0.)
    RenderTerrain(&h.floor, st, hooks, false);

    const i32 tIdx = 3*8 + 3;                 // an interior tile (col,row != 0,7)
    const Vertex* vb = h.vbufs[(size_t)tIdx].data();
    const Polygon* pb = h.pbufs[(size_t)tIdx].data();
    const i32 stride = 5;                      // v180 = span/lod+1 = 4/1+1
    const Vertex* i00 = &vb[0];
    const Vertex* i01 = &vb[1];
    const Vertex* i10 = &vb[stride];
    const Vertex* i11 = &vb[stride + 1];
    // tri0 = (i00, i11, i10)
    CHECK(pb[0].v0 == i00); CHECK(pb[0].v1 == i11); CHECK(pb[0].v2 == i10);
    // tri1 = (i00, i01, i11)
    CHECK(pb[1].v0 == i00); CHECK(pb[1].v1 == i01); CHECK(pb[1].v2 == i11);
    // +38 bit0 SET on both halves (the engine's shared split flag).
    CHECK_EQ((int)(pb[0].flags38 & 1), 1);
    CHECK_EQ((int)(pb[1].flags38 & 1), 1);

    // --- Branch B: drive the split byte negative (BL-TR diagonal) -------------
    // The split selector v494 reads the per-LOD SLOPE buffer v379 =
    // *(floor + 4*(lod>>1) + 36) (the BuildTilePolys @0x5bc45c output, modelled as
    // mipTexSrc[lod>>1] == texSrc here), NOT the cell type byte. Drive that buffer
    // high-bit-set so v494 < 0 selects the BL-TR diagonal.
    FloorHarness h2;
    for (auto& b : h2.texSrc) b = (u8)0x80;    // slope buffer high-bit set -> split < 0
    TerrainRenderState st2;
    st2.axisH[1]=1.0f; st2.axisU[0]=1.0f; st2.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl2(4096);
    DrawList out2{dl2.data(),0,(i32)dl2.size()}; st2.drawList=&out2;
    RenderTerrain(&h2.floor, st2, hooks, false);

    const Vertex* w = h2.vbufs[(size_t)tIdx].data();
    const Polygon* q = h2.pbufs[(size_t)tIdx].data();
    const Vertex* j00 = &w[0];
    const Vertex* j01 = &w[1];
    const Vertex* j10 = &w[stride];
    const Vertex* j11 = &w[stride + 1];
    // tri0 = (i10, i00, i01)
    CHECK(q[0].v0 == j10); CHECK(q[0].v1 == j00); CHECK(q[0].v2 == j01);
    // tri1 = (i01, i11, i10)
    CHECK(q[1].v0 == j01); CHECK(q[1].v1 == j11); CHECK(q[1].v2 == j10);
    // +38 bit0 CLEAR on both halves.
    CHECK_EQ((int)(q[0].flags38 & 1), 0);
    CHECK_EQ((int)(q[1].flags38 & 1), 0);
}

// =============================================================================
// WAVE-10 HARDENING: degenerate/edge memory-safety coverage for RenderTerrain.
// Drives the real walk over the SMALLEST (8) and LARGEST (128) grids and over the
// stitch pass so ASAN/UBSAN exercise every buffer bound. Tile/vertex/poly buffers
// are sized EXACTLY to the per-tile subdivision the walk computes (no slack) so an
// off-by-one write/read trips ASAN immediately.
// =============================================================================
namespace {

// A sized harness: `size` grid edge (8 tiles), `span` = size/8 samples per tile.
// Per-tile buffers are sized to the MAXIMUM the finest LOD (lod 1) can emit:
//   verts = (span+1)^2, polys = 2*span^2 + a stitch margin.
struct SizedFloorHarness {
    i32 size, span;
    std::vector<u8> heights, types, texSrc;
    std::vector<TerrainTile> tiles;
    std::vector<std::vector<Vertex>>  vbufs;
    std::vector<std::vector<Polygon>> pbufs;
    TerrainFloor floor{};

    SizedFloorHarness(i32 sz, u8 typeFill, i32 polySlack = 0) : size(sz), span(sz/8) {
        heights.assign((size_t)size*size, 0);
        types.assign((size_t)size*size, typeFill);
        texSrc.assign((size_t)size*size, 0);
        for (i32 y = 0; y < size; ++y)
            for (i32 x = 0; x < size; ++x)
                heights[(size_t)y*size + x] = (u8)((x + y) & 0x3F);
        const i32 vcap = (span + 1) * (span + 1);     // lod-1 finest vertex count
        const i32 pcap = 2 * span * span + polySlack;  // lod-1 finest poly count
        tiles.resize(64); vbufs.resize(64); pbufs.resize(64);
        for (int i = 0; i < 64; ++i) {
            vbufs[i].assign((size_t)vcap, Vertex{});
            pbufs[i].assign((size_t)pcap, Polygon{});
            tiles[i] = TerrainTile{};
            tiles[i].lod = 1; tiles[i].prevLod = 0;
            tiles[i].vertexBuf = vbufs[i].data();
            tiles[i].polyBuf   = pbufs[i].data();
        }
        floor.size = size; floor.tileSpan = span; floor.mask = size - 1;
        floor.heights = heights.data();
        floor.types = types.data();
        floor.texSrc = texSrc.data();
        for (int l = 0; l < 4; ++l) floor.mipTexSrc[l] = texSrc.data();
        floor.tiles = tiles.data();
        floor.flatLit = 1; floor.minLodNibble = 0;
    }
};

} // namespace

// Smallest possible grid: size 8 (span 1). lod 1 -> base = 1/1+1 = 2 verts/axis,
// 4 verts, 1 quad = 2 polys per interior tile. Edge tiles (idx 7) get base-4 = -2
// for the subdiv term -> the loops must clamp to nothing, not under-run.
TEST(TerrainWalkHardening, SmallestGrid8) {
    SizedFloorHarness h(8, /*type*/30);
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    // No crash / OOB; interior tile builds 1 quad.
    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended >= 0);
    TerrainTile& interior = h.tiles[2*8 + 2];
    CHECK_EQ(interior.subdivCached, 4);   // 2*2
    CHECK_EQ(interior.polyCount, 2);      // 1 quad
    // col-7 edge tile: TileSubdivCount(col,true) idx==7 -> base-4 = 2-4 = -2.
    CHECK_EQ(TileSubdivCount(1, 1, 7, 2, true), -2);
    // The walk must NOT emit polys for that tile (rowsM1/colsM1 negative => skip).
    TerrainTile& edge = h.tiles[2*8 + 7];
    CHECK_EQ(edge.polyCount, 0);
}

// Largest grid: size 128 (span 16). lod 1 -> base = 17 verts/axis, 289 verts,
// 16*16 = 256 quads = 512 polys per interior tile (exactly fills the buffers).
TEST(TerrainWalkHardening, LargestGrid128) {
    SizedFloorHarness h(128, /*type*/40);
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(200000);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended > 0);
    TerrainTile& interior = h.tiles[3*8 + 3];
    CHECK_EQ(interior.subdivCached, 17*17);   // 289
    CHECK_EQ(interior.polyCount, 2*16*16);    // 512 -> exactly the buffer size
    CHECK(out.count <= out.capacity);
}

// Border / wrap-mask: the last tile row+col (col 7,row 7) is the grid border. The
// per-cell index (worldX + size*worldY) & mask and the per-step (lod*size+cell)&mask
// must keep every height/type read inside [0,size*size). Tight per-tile buffers +
// ASAN guarantee any over-read of heights[]/types[] is caught.
TEST(TerrainWalkHardening, BorderTileWrapMask) {
    SizedFloorHarness h(32, /*type*/25);
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(16384);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended > 0);
    // Corner tile (7,7): both axes hit the idx==7 stitch term. span=4,lod=1 -> base 5,
    // cols=rows=5-4=1 -> product 1, 0 quads.
    TerrainTile& corner = h.tiles[7*8 + 7];
    CHECK_EQ(corner.subdivCached, 1);
    CHECK_EQ(corner.polyCount, 0);
}

// Stitch pass poly-count growth (WAVE-17: stitch EMISSION reconstructed 1:1). Drive
// neighbour edge LODs so the Pass-B stitch fires on an interior LOD-2 tile. The four
// arms each emit (cols-1)/(rows-1) seam polys (one per non-null boundary poly): an
// interior LOD-2 tile has cols=rows=3, so each arm emits 2 -> +8 stitch polys atop the
// 8 Pass-A polys = 16. The vertex/subdiv counters track 1:1. Buffers carry stitch
// slack so ASAN catches any over-write past the emitted records.
TEST(TerrainWalkHardening, StitchPolyCountStaysInBuffer) {
    // tile LOD 2: base = span/2+1 = 3, verts 9, interior quads 2*2 -> 8 Pass-A polys.
    SizedFloorHarness h(32, /*type*/25, /*polySlack*/16);  // room for 8 stitch polys
    // Pass-A LOD-2 emits 8 polys; the stitch can add up to 8 more (2 per arm). Size the
    // poly buffer to EXACTLY 8 + 8 so a +1 over-emit trips ASAN.
    for (int i = 0; i < 64; ++i) h.pbufs[i].assign(16, Polygon{});
    for (int i = 0; i < 64; ++i) h.tiles[i].polyBuf = h.pbufs[i].data();
    // Vertex buffer: 9 Pass-A verts + up to 8 seam verts = 17; LOD-1 (span+1)^2=25 fits.
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(16384);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    // Make neighbours coarser so the stitch gate (neighbourLod < lod) fires.
    for (int i = 0; i < 64; ++i) {
        h.tiles[i].lod = 2;             // this tile LOD 2
        h.tiles[i].edgeRightLod = 1;    // finer neighbour -> gate (1 < 2) fires
        h.tiles[i].edgeDownLod  = 1;
        h.tiles[i].edgeUpLod    = 1;
        h.tiles[i].edgeLeftLod  = 1;
    }
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended >= 0);
    CHECK(out.count <= out.capacity);
    // Interior LOD-2 tile (2,2): 8 Pass-A + 8 stitch (4 arms * 2) = 16 polys.
    TerrainTile& t = h.tiles[2*8 + 2];
    CHECK_EQ(t.polyCount, 16);
    // subdivCached = 9 Pass-A verts + 8 seam verts; vertCount = the 8 seam verts.
    CHECK_EQ(t.subdivCached, 9 + 8);
    CHECK_EQ(t.vertCount, 8);
}

// =============================================================================
// WAVE-17 — LOD-boundary STITCH EMISSION golden (@0x5bf22c lines 848..1691).
// Synthetic unequal-LOD adjacency: an interior LOD-2 tile with ONE finer (LOD-1)
// neighbour per edge, isolating each arm. Pins, against the exact decompiled math:
//   * the midpoint seam Vertex world position + light bytes,
//   * the boundary poly's far-vertex re-point to the midpoint,
//   * the appended poly (copy of the boundary tri, v1 := midpoint, uvX == 16.0f),
//   * the clip-scratch v483[3i+{0,1,2}] = {vert, seamU, seamV},
//   * the counter bumps (polyCount/subdivCached/vertCount).
// =============================================================================
namespace {
// A harness with a clip-scratch list + drawData per tile (the stitch needs clipList).
struct StitchHarness {
    static constexpr i32 kSize = 32, kSpan = 4;
    std::vector<u8> heights, types, texSrc;
    std::vector<TerrainTile> tiles;
    std::vector<std::vector<Vertex>>  vbufs;
    std::vector<std::vector<Polygon>> pbufs;
    std::vector<std::vector<void*>>   clipbufs;
    TerrainFloor floor{};
    StitchHarness(u8 typeFill) {
        heights.assign((size_t)kSize*kSize, 0);
        types.assign((size_t)kSize*kSize, typeFill);
        texSrc.assign((size_t)kSize*kSize, 0);   // slope buffer 0 -> split>=0, all visible
        for (i32 y=0;y<kSize;++y) for (i32 x=0;x<kSize;++x)
            heights[(size_t)y*kSize+x] = (u8)((x*3 + y*5) & 0x3F);
        tiles.resize(64); vbufs.resize(64); pbufs.resize(64); clipbufs.resize(64);
        for (int i=0;i<64;++i){
            vbufs[i].assign(64, Vertex{});
            pbufs[i].assign(64, Polygon{});
            clipbufs[i].assign(3*64, nullptr);
            tiles[i] = TerrainTile{};
            tiles[i].lod = 2; tiles[i].prevLod = 0;
            tiles[i].vertexBuf = vbufs[i].data();
            tiles[i].polyBuf   = pbufs[i].data();
            tiles[i].clipList  = clipbufs[i].data();
        }
        floor.size=kSize; floor.tileSpan=kSpan; floor.mask=kSize-1;
        floor.heights=heights.data(); floor.types=types.data(); floor.texSrc=texSrc.data();
        for (int l=0;l<4;++l) floor.mipTexSrc[l]=texSrc.data();
        floor.tiles=tiles.data(); floor.flatLit=1; floor.minLodNibble=0;
    }
};
} // namespace

TEST(TerrainWalkStitch, RightArmMidpointGolden) {
    StitchHarness h(/*type*/30);
    // Isolate the RIGHT arm: only the right neighbour of the probe tile is finer.
    const int R=3, C=3, idx=R*8+C;
    h.tiles[idx].edgeRightLod = 1;        // 1 < 2 -> RIGHT stitch fires
    // (left/up/down edge LODs left 0 -> those arms gated off)

    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    st.lightScale[0]=st.lightScale[1]=st.lightScale[2]=1.0f;
    st.ambient[0]=st.ambient[1]=st.ambient[2]=10.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };

    // Snapshot the Pass-A boundary polys BEFORE the walk re-points them: re-run a copy
    // of the harness through Pass A only is awkward; instead recompute the expected
    // indices/winding from the known geometry.
    const i32 span=h.kSpan, size=h.kSize, lod=2, v387=lod>>1;       // 1
    const i32 worldX=C*span, worldY=R*span;                          // 12, 12
    const i32 cols=TileSubdivCount(span,lod,C,R,true);               // 3
    const i32 rows=TileSubdivCount(span,lod,C,R,false);              // 3
    CHECK_EQ(cols,3); CHECK_EQ(rows,3);

    i32 appended = RenderTerrain(&h.floor, st, hooks, false);
    CHECK(appended > 0);

    TerrainTile& t = h.tiles[idx];
    Vertex*  vb = h.vbufs[idx].data();
    Polygon* pb = h.pbufs[idx].data();
    void**   cl = h.clipbufs[idx].data();

    // RIGHT emits rows-1 = 2 seam polys/verts.
    CHECK_EQ(t.vertCount, 2);
    CHECK_EQ(t.polyCount, 8 + 2);          // 8 Pass-A + 2 stitch
    CHECK_EQ(t.subdivCached, 9 + 2);       // 9 Pass-A verts + 2 seam verts

    // Seam vertex 0: midpoint of the topmost right-edge segment. World math:
    //   u = span+worldX = 16 ; v = v387+worldY = 13 ; world = (u, h*1, v).
    const i32 seamVtxBase = 9;             // appended at vertexBuf[subdivCached_start=9]
    Vertex& m0 = vb[seamVtxBase + 0];
    i32 cell0 = (span + worldX + size*(v387 + worldY)) & (size-1);
    u8  h0 = h.heights[cell0];
    CHECK_EQ(m0.x, (float)(span + worldX));            // axisU.x * 16
    CHECK_EQ(m0.y, (float)h0);                          // axisH.y * height
    CHECK_EQ(m0.z, (float)(v387 + worldY));            // axisV.z * 13
    // Light byte = clamp(1*((type&0x7F)*2) + 10).
    u8 ty0 = h.types[cell0];
    float l0 = (float)(ty0 & 0x7F) * 2.0f;
    CHECK_EQ((int)m0.lightIdx, (int)ClampLightByte(1.0f*l0 + 10.0f));

    // Seam vertex 1: one LOD step down the column (axisV*lod, cell += size*lod).
    Vertex& m1 = vb[seamVtxBase + 1];
    CHECK_EQ(m1.z, (float)(v387 + worldY + lod));      // 13 + 2 = 15

    // Clip scratch: v483[0]={m0, span+worldX, v387+worldY}, [3]={m1,.., +lod}.
    CHECK(cl[0] == &m0);
    CHECK_EQ((i32)(intptr_t)cl[1], span + worldX);     // seamU = 16
    CHECK_EQ((i32)(intptr_t)cl[2], v387 + worldY);     // seamV = 13
    CHECK(cl[3] == &m1);
    CHECK_EQ((i32)(intptr_t)cl[5], v387 + worldY + lod);

    // Boundary poly re-point + appended poly. Type 30 (high bit clear) + slope buffer
    // 0 -> split >= 0 (TL-BR), so flags38 bit0 SET on every Pass-A quad. RIGHT TL-BR:
    //   boundary = last-col 2nd-tri at index (2*cols-4 + 1) = 3; far-vertex slot = v2.
    const i32 bIdx = 2*cols - 4 + 1;       // 3
    Polygon& boundary = pb[bIdx];
    CHECK_EQ((int)(boundary.flags38 & 1), 1);          // TL-BR diagonal
    CHECK(boundary.v2 == &m0);                          // far vertex re-pointed to midpoint
    // Appended poly #0 at index 8 (first stitch poly): copy of the boundary tri with
    // v1 := midpoint and uvX == 16.0f. v0 keeps the boundary's near corner.
    Polygon& app0 = pb[8];
    CHECK(app0.v1 == &m0);
    CHECK_EQ(app0.uvX, 16.0f);
    CHECK(app0.v0 == boundary.v0);                      // shares the near corner
}

// Both diagonals: drive the slope buffer high-bit set so the Pass-A split is BL-TR,
// and verify the RIGHT arm re-points the boundary 2nd-tri's v0 (not v2).
TEST(TerrainWalkStitch, RightArmBlTrDiagonal) {
    StitchHarness h(/*type*/30);
    for (auto& b : h.texSrc) b = (u8)0x80;             // slope buffer < 0 -> BL-TR split
    const int R=3, C=3, idx=R*8+C;
    h.tiles[idx].edgeRightLod = 1;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    RenderTerrain(&h.floor, st, hooks, false);

    Polygon* pb = h.pbufs[idx].data();
    Vertex*  vb = h.vbufs[idx].data();
    const i32 cols=3, bIdx = 2*cols - 4 + 1;           // 3
    Polygon& boundary = pb[bIdx];
    CHECK_EQ((int)(boundary.flags38 & 1), 0);          // BL-TR diagonal
    Vertex& m0 = vb[9];
    CHECK(boundary.v0 == &m0);                          // BL-TR re-points v0
    CHECK(pb[8].v1 == &m0);
    CHECK_EQ(pb[8].uvX, 16.0f);
}

// All four arms fire together on an interior tile: 4*(edge-1) = 8 seam polys/verts.
TEST(TerrainWalkStitch, FourArmsCountAndStamp) {
    StitchHarness h(/*type*/25);
    const int R=3, C=3, idx=R*8+C;
    h.tiles[idx].edgeRightLod = 1;
    h.tiles[idx].edgeLeftLod  = 1;
    h.tiles[idx].edgeUpLod    = 1;
    h.tiles[idx].edgeDownLod  = 1;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    RenderTerrain(&h.floor, st, hooks, false);

    TerrainTile& t = h.tiles[idx];
    CHECK_EQ(t.vertCount, 8);              // 4 arms * (3-1)
    CHECK_EQ(t.polyCount, 8 + 8);
    CHECK_EQ(t.subdivCached, 9 + 8);
    // Every appended stitch poly carries the 16.0f uvX stamp.
    Polygon* pb = h.pbufs[idx].data();
    for (i32 i = 8; i < 16; ++i) CHECK_EQ(pb[i].uvX, 16.0f);
}

// Gate fidelity: NO stitch when neighbour LOD is equal or coarser (>= lod), and none
// when the neighbour edge byte is 0 (no neighbour). Equal-LOD path stays byte-identical.
TEST(TerrainWalkStitch, GateNoFireWhenNotFiner) {
    StitchHarness h(/*type*/25);
    const int idx=3*8+3;
    h.tiles[idx].edgeRightLod = 2;        // equal -> no fire
    h.tiles[idx].edgeLeftLod  = 3;        // coarser -> no fire
    h.tiles[idx].edgeUpLod    = 0;        // no neighbour -> no fire
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    RenderTerrain(&h.floor, st, hooks, false);
    TerrainTile& t = h.tiles[idx];
    CHECK_EQ(t.polyCount, 8);             // Pass-A only, no stitch
    CHECK_EQ(t.vertCount, 0);
    CHECK_EQ(t.subdivCached, 9);
}

// =============================================================================
// Wave-18: the flt_13FE540 UV-emission subsystem (per-quad UV records + the
// seam-UV midpoint blend). Pass-A stamps each quad poly's tri0/tri1 UV record;
// Pass-B copies + 0.5-blends the seam UVs into the per-tile uvScratch.
// =============================================================================
namespace {
// A stitch harness that also binds per-poly UV records + the per-tile UV scratch
// + the flt_13FE540 table (so UV emission is active).
struct UvStitchHarness : StitchHarness {
    std::vector<std::vector<float>> polyUvBufs;   // 6 floats per poly
    std::vector<std::vector<float>> uvScratchBufs;// 6 floats per scratch record
    float uvTable[guild::render::kTerrainUvTableSize];
    explicit UvStitchHarness(u8 typeFill) : StitchHarness(typeFill) {
        guild::render::BuildTerrainUvTable(uvTable, 64);
        polyUvBufs.resize(64); uvScratchBufs.resize(64);
        for (int i=0;i<64;++i){
            polyUvBufs[i].assign(6*64, 0.0f);
            uvScratchBufs[i].assign(6*64, 0.0f);
            tiles[i].polyUv = polyUvBufs[i].data();
            tiles[i].uvScratch = uvScratchBufs[i].data();
        }
    }
};
} // namespace

// Pass-A per-quad UV records: each quad's tri0 == flt_13FE540[0..5], tri1 == [6..11]
// (subTexId == 0, the shipped image). Verified against the table directly.
TEST(TerrainWalkUv, PassAQuadUvRecords) {
    UvStitchHarness h(/*type*/30);
    const int idx=3*8+3;
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    st.uvTable = h.uvTable;                 // enable UV emission
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    RenderTerrain(&h.floor, st, hooks, false);

    const float* pu = h.polyUvBufs[idx].data();
    // Quad 0 -> poly slots 0 (tri0) and 1 (tri1).
    for (int i=0;i<6;++i) {
        CHECK_EQ(pu[6*0 + i], h.uvTable[i]);       // tri0 = T0 floats 0..5
        CHECK_EQ(pu[6*1 + i], h.uvTable[6 + i]);   // tri1 = T1 floats 6..11
    }
}

// Pass-B seam-UV midpoint blend: the boundary poly's UV record is copied into the
// per-tile uvScratch and 0.5-blended; the appended poly carries the blended midpoint.
TEST(TerrainWalkUv, SeamBlendIntoScratch) {
    UvStitchHarness h(/*type*/30);
    const int R=3, C=3, idx=R*8+C;
    h.tiles[idx].edgeRightLod = 1;          // RIGHT arm fires (TL-BR diagonal)
    TerrainRenderState st;
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    st.uvTable = h.uvTable;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };

    // Pre-blend boundary record == the table T1 record (last-col 2nd-tri, subTexId 0).
    float pre[6];
    TerrainQuadUvT1(pre, h.uvTable, 0);
    float blended[6]; for (int i=0;i<6;++i) blended[i]=pre[i];
    TerrainSeamBlendUv(blended, /*TL-BR*/true);

    RenderTerrain(&h.floor, st, hooks, false);

    TerrainTile& t = h.tiles[idx];
    CHECK_EQ(t.vertCount, 2);
    // 2 seam polys * 2 scratch records each.
    CHECK_EQ(t.uvScratchCount, 4);

    const float* sc = h.uvScratchBufs[idx].data();
    const float* pu = h.polyUvBufs[idx].data();
    // Scratch record 0 == the first seam poly's blended boundary record.
    for (int i=0;i<6;++i) CHECK_EQ(sc[6*0 + i], blended[i]);
    // The boundary poly (RIGHT TL-BR: index 2*cols-4+1 = 3) UV re-pointed to it.
    const i32 bIdx = 2*3 - 4 + 1;           // 3
    for (int i=0;i<6;++i) CHECK_EQ(pu[6*bIdx + i], blended[i]);
    // The appended poly (slot 8) carries the blended midpoint in its v1 slot (floats 2,3).
    const float midU = blended[4], midV = blended[5];   // TL-BR -> vert2 slot
    CHECK_EQ(pu[6*8 + 2], midU);
    CHECK_EQ(pu[6*8 + 3], midV);
}

// Without uvTable the walk emits geometry only (UV arrays stay zero) — additive-safe.
TEST(TerrainWalkUv, InertWithoutTable) {
    UvStitchHarness h(/*type*/30);
    const int R=3, C=3, idx=R*8+C;
    h.tiles[idx].edgeRightLod = 1;
    TerrainRenderState st;                   // st.uvTable == nullptr
    st.axisH[1]=1.0f; st.axisU[0]=1.0f; st.axisV[2]=1.0f;
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    RenderTerrain(&h.floor, st, hooks, false);
    // polyUv / uvScratch untouched (all zero), geometry still built.
    for (float v : h.polyUvBufs[idx])   CHECK_EQ(v, 0.0f);
    for (float v : h.uvScratchBufs[idx]) CHECK_EQ(v, 0.0f);
    CHECK_EQ(h.tiles[idx].polyCount, 8 + 2);   // stitch geometry still emitted
}
