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
