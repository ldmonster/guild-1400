#include "tests/framework/test.h"

// ===========================================================================
// Pins for the CITY TERRAIN systems added by the live-capture work (suite
// prefix: CityTerrain):
//
//  * BuildTerrainLightMap — the Floor+0x1C hillshade model (frida-fitted):
//    a flat height grid yields the flat-ground base (26) everywhere; a slope
//    responds with the fitted directional gradient (brighter on the +x-facing
//    side, per the +0.698*dH/dx term).
//  * TerrainSubTexId — WORLD-STABLE indexing: the same absolute cell coords
//    give the same sub-record id regardless of call order (the tile-local
//    indexing regression made the ground reshuffle while scrolling); the
//    0x40 cell-flag gates; a null table is 0.
//  * BuildTerrainUvTable64 — record 0 is byte-identical to the corner-inset
//    single-record table; records 1..63 are finite and non-degenerate.
//  * The transition-tile bake (@0x5ba1e8 model) — with synthetic solid-colour
//    slot textures via the TerrainRenderHooks seam: a uniform 3x3 block never
//    bakes (the walk fetch returns the plain slot id); a mixed block bakes a
//    tile whose INTERIOR keeps the own texture's colour and whose differing
//    EDGE blends toward the neighbour (~50/50 at the shared edge); the cache
//    returns the same index for the same block and drops on invalidation.
// ===========================================================================
#include "crt/rand.h"
#include "play/terrain_render.h"
#include "render/terrain_uvtable.h"
#include "render/texture.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using guild::play::GroundFrame;

namespace {

// Two synthetic 8-bit slot textures: solid palette-index-1 texels with a
// single-colour palette (type 0 = RED, type 1 = GREEN).
render::Texture g_texRed, g_texGreen;

void MakeSolid(render::Texture& t, u8 r, u8 g, u8 b) {
    render::TextureSetSize(t, 16);
    std::fill(t.texels.begin(), t.texels.end(), (u8)1);
    t.paletteStore.assign(768, 0);
    t.paletteStore[3 * 1 + 0] = r;
    t.paletteStore[3 * 1 + 1] = g;
    t.paletteStore[3 * 1 + 2] = b;
}

const void* SlotHook(u8 typeByte) {
    if (typeByte == 0) return &g_texRed;
    if (typeByte == 1) return &g_texGreen;
    return nullptr;
}

// Average palette colour of a baked tile's texel row segment.
void AvgColor(const render::Texture* rec, int x0, int x1, int y, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    int n = 0;
    for (int x = x0; x < x1; ++x) {
        const u8 idx = rec->texels[(std::size_t)y * rec->mipWidth + x];
        out[0] += rec->paletteStore[3 * idx + 0];
        out[1] += rec->paletteStore[3 * idx + 1];
        out[2] += rec->paletteStore[3 * idx + 2];
        ++n;
    }
    if (n) { out[0] /= n; out[1] /= n; out[2] /= n; }
}

} // namespace

// Flat heights -> the flat-ground base everywhere; a slope shades directionally.
TEST(CityTerrain, LightMapFlatBaseAndSlopeResponse) {
    const int n = 16;
    std::vector<u8> flat((std::size_t)n * n, 50);
    std::vector<u8> out;
    GroundFrame::BuildTerrainLightMap(flat.data(), n, out);
    CHECK_EQ((int)out.size(), n * n);
    for (u8 v : out)
        CHECK_EQ((int)v, 26);                     // 26.19 truncates to 26

    // A ramp rising toward +x: dH/dx > 0 -> brighter than the base
    // (the fitted +0.698*gx term). Sample away from the wrap seam.
    std::vector<u8> ramp((std::size_t)n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            ramp[(std::size_t)y * n + x] = (u8)(x * 8);
    GroundFrame::BuildTerrainLightMap(ramp.data(), n, out);
    CHECK((int)out[(std::size_t)8 * n + 8] > 26);
}

// World-stable sub-record ids: pure function of the absolute cell coords.
TEST(CityTerrain, SubTexIdWorldStable) {
    std::vector<u8> table(65536);
    for (std::size_t i = 0; i < table.size(); ++i)
        table[i] = (u8)(i * 37u + (i >> 8));
    const u32 a = render::TerrainSubTexId(0x40, table.data(), 300, 77);
    const u32 b = render::TerrainSubTexId(0x40, table.data(), 300, 77);
    CHECK_EQ((int)a, (int)b);                     // repeatable
    // The exact indexing: (v & 0xFF) << 8 | (u & 0xFF), masked & 0x3F.
    const u32 want = table[((77u & 0xFF) << 8) | (300u & 0xFF)] & 0x3F;
    CHECK_EQ((int)a, (int)want);
    // Coordinates wrap mod 256 (the table tiles across the world).
    CHECK_EQ((int)render::TerrainSubTexId(0x40, table.data(), 300 + 256, 77),
             (int)a);
    // The 0x40 gate + the null table.
    CHECK_EQ((int)render::TerrainSubTexId(0x00, table.data(), 300, 77), 0);
    CHECK_EQ((int)render::TerrainSubTexId(0x40, nullptr, 300, 77), 0);
}

// Record 0 of the 64-record table is the corner-inset single record verbatim;
// the random records are finite.
TEST(CityTerrain, UvTable64Record0Verbatim) {
    float single[render::kTerrainUvTableSize];
    render::BuildTerrainUvTable(single, 64);
    float full[render::kTerrainUvRecordCount * render::kTerrainUvTableSize];
    crt::Srand(1234);                             // deterministic rand stream
    render::BuildTerrainUvTable64(full, 64);
    for (int i = 0; i < render::kTerrainUvTableSize; ++i)
        CHECK_EQ(full[i], single[i]);
    for (int i = render::kTerrainUvTableSize;
         i < render::kTerrainUvRecordCount * render::kTerrainUvTableSize; ++i) {
        CHECK(std::isfinite(full[i]));
        CHECK(full[i] > -4.0f && full[i] < 5.0f); // centre+rot*scale bounds
    }
}

// The transition bake: interior purity + edge blend + cache identity.
TEST(CityTerrain, TransitionBakeEdgeBand) {
    MakeSolid(g_texRed, 200, 0, 0);
    MakeSolid(g_texGreen, 0, 200, 0);
    play::TerrainRenderHooks hooks{};
    hooks.getTileTexture = &SlotHook;
    play::SetTerrainRenderHooks(&hooks);
    GroundFrame::InvalidateTransitionBakes();

    // A red cell with a green EAST neighbour (3x3 block, row-major, centre=4).
    const u8 blk[9] = {0, 0, 1,
                       0, 0, 1,
                       0, 0, 1};
    const int id = GroundFrame::TestBakeTransitionTile(blk);
    CHECK(id >= 0);
    const render::Texture* rec = GroundFrame::TestBakedTileRecord(id);
    CHECK(rec != nullptr);
    const int W = rec->mipWidth;
    CHECK(W >= 4);

    float c[3];
    const int midY = W / 2;
    // Interior (left 40%): pure own (red) — the edge band must not reach it.
    AvgColor(rec, 0, (int)(W * 0.4f), midY, c);
    CHECK(c[0] > 180.0f);
    CHECK(c[1] < 20.0f);
    // The differing east edge: ~50/50 red/green (continuity with the
    // neighbour's mirrored ramp).
    AvgColor(rec, W - 2, W, midY, c);
    CHECK(c[0] > 60.0f && c[0] < 140.0f);
    CHECK(c[1] > 60.0f && c[1] < 140.0f);

    // Cache identity + invalidation.
    CHECK_EQ(GroundFrame::TestBakeTransitionTile(blk), id);
    GroundFrame::InvalidateTransitionBakes();
    CHECK(GroundFrame::TestBakedTileRecord(id) == nullptr);

    play::SetTerrainRenderHooks(nullptr);
}

// The walk fetch: uniform neighbourhood -> the plain slot id (type+1); mixed
// -> a bake id (>= 0x101).
TEST(CityTerrain, GetOrBuildTileGatesOnMixedBlock) {
    MakeSolid(g_texRed, 200, 0, 0);
    MakeSolid(g_texGreen, 0, 200, 0);
    play::TerrainRenderHooks hooks{};
    hooks.getTileTexture = &SlotHook;
    play::SetTerrainRenderHooks(&hooks);
    GroundFrame::InvalidateTransitionBakes();

    // An 8x8 type grid: all red except one green cell at (5, 5).
    std::vector<u8> grid((std::size_t)8 * 8, 0);
    grid[(std::size_t)5 * 8 + 5] = 1;
    // Far from the green cell: the plain slot path.
    CHECK_EQ((int)GroundFrame::TestGetOrBuildTile(grid.data(), 8, 1, 1), 1);
    // Adjacent to the green cell: baked (id >= 0x101).
    CHECK(GroundFrame::TestGetOrBuildTile(grid.data(), 8, 5, 5) >= 0x101u);

    GroundFrame::InvalidateTransitionBakes();
    play::SetTerrainRenderHooks(nullptr);
}

// ---------------------------------------------------------------------------
// SEASONAL FLOOR RESOLVE (FloorTextureResolver::SetSeason): the load order is
// <name><season>_high -> <name><season> -> <name>_high -> <name>, with the
// season words _fruehling / (base) / _herbst / _snow (spring..winter — the
// shipped _DYNAMIC/Boden set; live-validated day 0 == season 0 == spring).
// A season change invalidates the resolved slots.
// ---------------------------------------------------------------------------
#include "render/floorgfx_recon.h"
#include "render/texture_asset.h"

namespace {
int SeedRec(render::TextureAssetCache& cache, const std::string& name) {
    return cache.set().CreateRecord(name, 8);
}
} // namespace

TEST(CityTerrain, SeasonalFloorResolvePreference) {
    render::TextureAssetCache cache(32);
    SeedRec(cache, "WIESE");
    SeedRec(cache, "WIESE_fruehling_high");
    SeedRec(cache, "WIESE_snow_high");

    char names[8][64];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names[0], "WIESE");

    render::FloorTextureResolver r;
    r.Bind(names, &cache);
    r.SetSeason(0);                               // spring
    const render::Texture* t = r.Resolve(0);
    CHECK(t != nullptr);
    CHECK(t->name == "WIESE_fruehling_high");

    r.SetSeason(1);                               // summer: base (no _sommer
    t = r.Resolve(0);                             // files ship; _high unseeded)
    CHECK(t != nullptr);
    CHECK(t->name == "WIESE");

    r.SetSeason(2);                               // autumn: _herbst unseeded
    t = r.Resolve(0);                             // -> falls through to base
    CHECK(t != nullptr);
    CHECK(t->name == "WIESE");

    r.SetSeason(3);                               // winter
    t = r.Resolve(0);
    CHECK(t != nullptr);
    CHECK(t->name == "WIESE_snow_high");

    // Same-season SetSeason is a no-op (the cached record persists).
    const render::Texture* again = r.Resolve(0);
    r.SetSeason(3);
    CHECK(r.Resolve(0) == again);
}

// ---------------------------------------------------------------------------
// BuildTileVertex RGB placement — the engine's per-vertex terrain light triple
// (scale*l + ambient, shadow branch adds bias) lands at the exact byte slots
// the D3D diffuse consumes: R @ +66 (lightIdx), G @ +65 (_pad41), B @ +64
// (color0) — and the packed return mirrors B | G<<8 | R<<16.
// ---------------------------------------------------------------------------
#include "render/tile_geometry.h"

TEST(CityTerrain, TileVertexRgbPlacementAndClamp) {
    render::TileLightParams p{};
    p.heightAxis[0] = 0; p.heightAxis[1] = 1; p.heightAxis[2] = 0;
    p.lightScale[0] = 1.0f; p.lightScale[1] = 0.5f; p.lightScale[2] = 0.25f;
    p.ambient[0] = 10.0f; p.ambient[1] = 20.0f; p.ambient[2] = 30.0f;
    p.shadowBias[0] = -5.0f; p.shadowBias[1] = -5.0f; p.shadowBias[2] = -5.0f;
    const float acc[3] = {0, 0, 0};

    render::Vertex v{};
    // typeByte 40 (no shadow bit): l = 40*2 = 80.
    const u32 packed = render::BuildTileVertex(v, p, acc, /*h=*/0, /*type=*/40);
    CHECK_EQ((int)v.lightIdx, 90);   // R = 1.00*80 + 10
    CHECK_EQ((int)v._pad41, 60);     // G = 0.50*80 + 20
    CHECK_EQ((int)v.color0, 50);     // B = 0.25*80 + 30
    CHECK_EQ((int)packed, 50 | (60 << 8) | (90 << 16));

    // Shadow bit (0x80): the bias joins each channel.
    render::Vertex vs{};
    render::BuildTileVertex(vs, p, acc, 0, (u8)(0x80 | 40));
    CHECK_EQ((int)vs.lightIdx, 85);
    CHECK_EQ((int)vs._pad41, 55);
    CHECK_EQ((int)vs.color0, 45);

    // Clamp: a huge l saturates at 255.
    p.ambient[0] = 250.0f;
    render::Vertex vc{};
    render::BuildTileVertex(vc, p, acc, 0, 127);
    CHECK_EQ((int)vc.lightIdx, 255); // 1.0*254 + 250 -> clamped
}
