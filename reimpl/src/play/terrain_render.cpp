#include "play/terrain_render.h"

#include "render/terrain_render.h"  // SelectTileMeshLod, TileSubdivCount, Floor LOD
#include "render/raster.h"          // RasterizeTexturedTriangle, RasterVertex
#include "render/raster_textured.h" // RasterizeTexturedTriangleRgbz (16bpp leaf)
#include "render/fog.h"             // FogState / ComputeFogFactor (W7-FOGPIX)
#include "render/texture.h"         // render::Texture (the resolved slot record)
#include "render/surface.h"

// 3D ground pass (terrain-ground wave 4):
#include "render/clip.h"            // ClipContext / ClipScratch (the flush clip)
#include "render/cull.h"            // ComputeVertexClipFlags @0x5ad614
#include "render/meshlist.h"        // RasterizeMeshList @0x5AEC88
#include "render/scene_transform.h" // MatrixFromEuler / Transpose / Apply / WorldToView
#include "render/terrain_mesh.h"    // ComputeTileVertices @0x5bdec4 + elevation summary
#include "render/terrain_uvtable.h" // BuildTerrainUvTable @0x5b94cc
#include "render/tile_visibility.h" // ComputeTileCenterRadius / ComputeLodLevel / StitchTileLod
#include "util/coord.h"             // ConvertX (x87 truncate)

#include <cctype>
#include <cmath>
#include <cstring>

namespace guild::play {

using render::Vertex;
using render::TileLightParams;

// ===========================================================================
// Inert device hooks (defined here so the unified library links — build model).
// ===========================================================================
static const void* InertGetTileTexture(u8 /*typeByte*/) { return nullptr; }

static TerrainRenderHooks g_hooks = { &InertGetTileTexture };

void SetTerrainRenderHooks(const TerrainRenderHooks* hooks) {
    if (!hooks) { g_hooks = { &InertGetTileTexture }; return; }
    g_hooks = *hooks;
    if (!g_hooks.getTileTexture) g_hooks.getTileTexture = &InertGetTileTexture;
}
const TerrainRenderHooks& GetTerrainRenderHooks() { return g_hooks; }

// ===========================================================================
// Heightfield / view builders.
// ===========================================================================
Heightfield Heightfield::MakeSynthetic(i32 edge, u32 seed) {
    Heightfield hf;
    if (edge < 8) edge = 8;
    edge -= edge % 8;                 // 8x8 tile grid must divide the edge
    hf.size = edge;
    hf.tileSpan = edge / 8;
    hf.heights.assign((size_t)edge * edge, 0);
    hf.types.assign((size_t)edge * edge, 0);

    const float c = (edge - 1) * 0.5f;
    const float rMax = c * 1.41421356f + 1.0f;
    for (i32 y = 0; y < edge; ++y) {
        for (i32 x = 0; x < edge; ++x) {
            // radial dome (high at the centre) + a diagonal ridge — a smooth,
            // ground-like field. Deterministic; `seed` perturbs it gently.
            float dx = (float)x - c, dy = (float)y - c;
            float r = std::sqrt(dx * dx + dy * dy);
            float dome = (1.0f - r / rMax);                  // 1 centre .. 0 edge
            float ridge = 0.30f * std::sin((x + y) * 0.55f); // diagonal ripple
            float perturb = ((float)((seed + (u32)(x * 131 + y * 977)) & 31)) / 255.0f;
            float h = (dome + ridge * 0.5f + perturb) * 200.0f + 20.0f;
            if (h < 0) h = 0;
            if (h > 255) h = 255;
            u8 hb = (u8)h;
            hf.heights[(size_t)y * edge + x] = hb;

            // type byte: ramp by elevation; set the shadow high bit on the lowest
            // band so the BuildTileVertex shadow branch is exercised.
            u8 t = (u8)(hb >> 1);                 // 0..127 brightness band
            if (hb < 60) t |= 0x80;               // shadowed (high bit)
            hf.types[(size_t)y * edge + x] = t;
        }
    }
    return hf;
}

TerrainView TerrainView::MakeTopDown(i32 size, i32 tileSpan, int fbW, int fbH) {
    (void)tileSpan;
    TerrainView v;
    // Neutral sun: per-channel light scale ~1, modest ambient, mild shadow bias.
    v.light.lightScale[0] = 1.0f; v.light.lightScale[1] = 1.0f; v.light.lightScale[2] = 1.0f;
    v.light.ambient[0]    = 40.0f; v.light.ambient[1] = 40.0f; v.light.ambient[2] = 40.0f;
    v.light.shadowBias[0] = -30.0f; v.light.shadowBias[1] = -30.0f; v.light.shadowBias[2] = -30.0f;
    // height axis: lift world.y by the height byte (a gentle vertical exaggeration
    // that does not move screen x/z under the top-down map, but feeds the lighting
    // world position for the golden checks).
    v.light.heightAxis[0] = 0.0f; v.light.heightAxis[1] = 0.25f; v.light.heightAxis[2] = 0.0f;

    // World axes: one world unit per grid sample along x (column) and z (row).
    v.axisX[0] = 1; v.axisX[1] = 0; v.axisX[2] = 0;
    v.axisZ[0] = 0; v.axisZ[1] = 0; v.axisZ[2] = 1;

    // Map the [0,size] world span onto [margin, fb-margin] pixels (top-down).
    const float marginX = fbW * 0.06f, marginY = fbH * 0.06f;
    float span = (float)(size > 0 ? size : 1);
    v.screenScaleX = (fbW - 2 * marginX) / span;
    v.screenScaleY = (fbH - 2 * marginY) / span;
    v.screenOriginX = marginX;
    v.screenOriginY = marginY;
    return v;
}

// ===========================================================================
// Geometry / lighting leaves.
// ===========================================================================
u8 ShadeFromRgb(u8 r, u8 g, u8 b) {
    // Integer 0.299/0.587/0.114 luma (77/150/29 / 256).
    u32 luma = ((u32)r * 77u + (u32)g * 150u + (u32)b * 29u) >> 8;
    return (u8)(luma > 255 ? 255 : luma);
}

TileVertex BuildTerrainVertex(const Heightfield& hf, const TerrainView& view,
                              i32 sx, i32 sy) {
    TileVertex out{};
    const i32 m = hf.mask();
    // wrapped sample (torus), mirroring TileType's (mask&y)*size + (mask&x).
    i32 cx = m & sx, cy = m & sy;
    u8 h = hf.heights[(size_t)cy * hf.size + cx];
    u8 t = hf.types[(size_t)cy * hf.size + cx];

    // world accumulator = origin + sx*axisX + sy*axisZ. (The engine's per-row/col
    // running accumulator; here computed directly from the sample index.)
    float acc[3];
    for (int k = 0; k < 3; ++k)
        acc[k] = (float)sx * view.axisX[k] + (float)sy * view.axisZ[k];

    // REAL leaf: render::BuildTileVertex builds world xyz + per-channel RGB light.
    Vertex v{};
    std::memset(&v, 0, sizeof(v));
    render::BuildTileVertex(v, view.light, acc, h, t);

    out.worldX = v.x; out.worldY = v.y; out.worldZ = v.z;
    out.r = v.lightIdx;   // +66 R
    out.g = v._pad41;     // +65 G
    out.b = v.color0;     // +64 B
    out.shade = ShadeFromRgb(out.r, out.g, out.b);

    // Top-down project: world (x,z) -> screen pixels.
    out.screenX = view.screenOriginX + out.worldX * view.screenScaleX;
    out.screenY = view.screenOriginY + out.worldZ * view.screenScaleY;
    return out;
}

// ===========================================================================
// The renderer.
// ===========================================================================
namespace {

// Rasterize one quad (4 corners A,B,C,D) as two shaded triangles. Returns the
// number of triangles that emitted at least one span.
int DrawQuad(render::Surface* fb, const TileVertex& a, const TileVertex& b,
             const TileVertex& c, const TileVertex& d, u8 background,
             TerrainRenderStats& st) {
    auto mkRV = [](const TileVertex& tv) {
        render::RasterVertex rv;
        rv.x = tv.screenX; rv.y = tv.screenY; rv.light = tv.shade;
        return rv;
    };
    // shade range bookkeeping
    for (const TileVertex* tv : { &a, &b, &c, &d }) {
        if (tv->shade < st.minShade) st.minShade = tv->shade;
        if (tv->shade > st.maxShade) st.maxShade = tv->shade;
    }
    int drew = 0;
    {
        render::RasterVertex tri[3] = { mkRV(a), mkRV(b), mkRV(c) };
        if (render::RasterizeTexturedTriangle(fb, tri)) ++drew;
    }
    {
        render::RasterVertex tri[3] = { mkRV(a), mkRV(c), mkRV(d) };
        if (render::RasterizeTexturedTriangle(fb, tri)) ++drew;
    }
    (void)background;
    return drew;
}

int CountNonBackground(const render::Surface* fb, u8 background) {
    int n = 0;
    const int total = fb->width * fb->height;
    // 8bpp: walk pixels honouring the row stride.
    for (int y = 0; y < fb->height; ++y) {
        const u8* row = fb->pixels + (size_t)y * fb->widthPx;
        for (int x = 0; x < fb->width; ++x)
            if (row[x] != background) ++n;
    }
    (void)total;
    return n;
}

} // namespace

TerrainRenderStats RenderTerrain(render::Surface* fb, const Heightfield& hf,
                                 const TerrainView& view, u8 background) {
    TerrainRenderStats st{};
    if (!fb || fb->bpp != 8 || !hf.valid())
        return st;

    // LOD: the engine's size-derived 1..4 clamp from BuildTerrainMesh. scaleX is
    // the world-units-per-tile-column step (axisX length * tileSpan).
    float axLen = std::sqrt(view.axisX[0]*view.axisX[0] +
                            view.axisX[1]*view.axisX[1] +
                            view.axisX[2]*view.axisX[2]);
    float scaleX = axLen * (float)hf.tileSpan;
    if (scaleX <= 0) scaleX = 1.0f;
    st.lod = render::SelectTileMeshLod(scaleX);
    if (st.lod < 1) st.lod = 1;
    const u8 lod = (u8)st.lod;

    // Walk the 8x8 tile grid in ROW-MAJOR order (the engine's tile-draw order).
    for (i32 trow = 0; trow < 8; ++trow) {
        for (i32 tcol = 0; tcol < 8; ++tcol) {
            ++st.tilesDrawn;
            // Per-tile subdivision counts (with the col/row==7 seam stitch).
            i32 cols = render::TileSubdivCount(hf.tileSpan, lod, tcol, trow, /*isCol=*/true);
            i32 rows = render::TileSubdivCount(hf.tileSpan, lod, tcol, trow, /*isCol=*/false);
            if (cols < 1 || rows < 1) continue;

            // The tile's base sample (in the full grid) and the per-subdiv step.
            i32 baseX = tcol * hf.tileSpan;
            i32 baseY = trow * hf.tileSpan;
            // step so that `cols`/`rows` quads cover the tileSpan samples.
            // (subdivCount == tileSpan/lod + 1 is a VERTEX count; quads = count-1,
            //  each quad spans `lod` source samples.)
            i32 quadsX = cols - 1; if (quadsX < 1) quadsX = 1;
            i32 quadsY = rows - 1; if (quadsY < 1) quadsY = 1;
            i32 stepX = hf.tileSpan / quadsX; if (stepX < 1) stepX = 1;
            i32 stepY = hf.tileSpan / quadsY; if (stepY < 1) stepY = 1;

            for (i32 qy = 0; qy < quadsY; ++qy) {
                i32 sy0 = baseY + qy * stepY;
                i32 sy1 = sy0 + stepY;
                for (i32 qx = 0; qx < quadsX; ++qx) {
                    i32 sx0 = baseX + qx * stepX;
                    i32 sx1 = sx0 + stepX;
                    // 4 corners A=(x0,y0) B=(x1,y0) C=(x1,y1) D=(x0,y1) — the
                    // ComputeTileVertices A,B,C,D winding.
                    TileVertex A = BuildTerrainVertex(hf, view, sx0, sy0);
                    TileVertex B = BuildTerrainVertex(hf, view, sx1, sy0);
                    TileVertex C = BuildTerrainVertex(hf, view, sx1, sy1);
                    TileVertex D = BuildTerrainVertex(hf, view, sx0, sy1);
                    ++st.quadsBuilt;
                    st.trisDrawn += DrawQuad(fb, A, B, C, D, background, st);
                }
            }
        }
    }

    st.nonBlankPix = CountNonBackground(fb, background);
    if (st.minShade > st.maxShade) { st.minShade = 0; st.maxShade = 0; }
    return st;
}

render::Surface* RenderTerrainToSurface(int fbW, int fbH, const Heightfield& hf,
                                        const TerrainView& view, u8 background,
                                        TerrainRenderStats* outStats) {
    if (!hf.valid()) return nullptr;
    render::Surface* fb = render::SurfaceCreate(fbW, fbH, 8);
    if (!fb) return nullptr;
    std::memset(fb->pixels, background, (size_t)fb->pitch * fb->height);
    TerrainRenderStats st = RenderTerrain(fb, hf, view, background);
    if (outStats) *outStats = st;
    return fb;
}

// ===========================================================================
// 3D GROUND PASS (terrain-ground wave 4). See the header banner for the data
// flow, the verified frame position and the named gaps.
// ===========================================================================

FloorGround BuildFloorGroundFromBlock(const render::SceneFloorBlock& b) {
    FloorGround g;
    // The 0x5bd44c gate (@0x5bd707): `if (floor+20 && floor+16)` — without BOTH
    // the heights and the texture grid the engine frees the floor and returns 0.
    if (!b.ok || !b.heights.accepted || !b.textureGrid.accepted || b.gridN < 8 ||
        (b.gridN & (b.gridN - 1)) != 0)
        return g;
    const i32 n = (i32)b.gridN;
    if ((i32)b.heights.data.size() != n * n || (i32)b.textureGrid.data.size() != n * n)
        return g;

    // VIBE_Floor_LoadFromHeightmap @0x5bd44c placement math (DeriveFloorPlacement).
    const render::FloorPlacement p =
        render::DeriveFloorPlacement(n, b.cellScale, b.heightScale);
    g.size     = n;
    g.tileSpan = p.tileSpan;
    g.heights  = b.heights.data;
    g.texGrid  = b.textureGrid.data;
    // @0x5bd8ab..0x5bdc95: min/max scan + min subtraction (0-based slot indices).
    render::NormalizeFloorTextureGrid(g.texGrid.data(), g.texGrid.size(),
                                      &g.gridMin, &g.gridMax);
    for (int k = 0; k < 3; ++k) {
        g.axisU[k] = p.axisU[k];
        g.axisV[k] = p.axisV[k];
        g.axisH[k] = p.axisH[k];
    }
    g.origin[0] = p.originX;
    g.origin[1] = p.originY;
    g.origin[2] = p.originZ;
    // LoadFloorRegions @0x5e7d28: the trailing stream origin vec3 OVERWRITES
    // floor dwords[36..38] (+144/148/152) right after the floor is built.
    if (b.hasOrigin) {
        g.origin[0] = b.origin[0];
        g.origin[1] = b.origin[1];
        g.origin[2] = b.origin[2];
    }
    g.cellScale   = b.cellScale;
    g.heightScale = b.heightScale;
    // The 8 typeName slots the loader copies to Floor+0x1A64 (@0x5bd810 loop).
    for (int i = 0; i < 8; ++i) {
        std::memset(g.typeNames[i].name, 0, sizeof(g.typeNames[i].name));
        if (i < b.typeNameCount) {
            std::strncpy(g.typeNames[i].name, b.typeNames[i].c_str(),
                         sizeof(g.typeNames[i].name) - 1);
        }
    }

    // WATER (wave-6 W6-WR). waterFlag is the block's water-presence byte; the
    // water-type byte is the typeNames slot whose UPPER name is "WASSER" (the
    // loc_5CB930 type-name match VIBE_FloorWater_PrepareRegions makes), expressed
    // in MIN-NORMALIZED grid units (slot - gridMin) since g.texGrid is normalized.
    g.hasWater = b.waterFlag != 0;
    g.waterType = 0xFF;
    for (int i = 0; i < b.typeNameCount; ++i) {
        std::string up = b.typeNames[i];
        for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
        if (up.find("WASSER") != std::string::npos) {
            const int norm = i - (int)g.gridMin;
            if (norm >= 0 && norm <= 0xFF) g.waterType = (u8)norm;
            break;
        }
    }
    if (b.waterHeights.accepted && (i32)b.waterHeights.data.size() == n * n)
        g.waterHeights = b.waterHeights.data;
    return g;
}

bool FloorGroundWorldY(const FloorGround& g, float wx, float wz, float* outY) {
    if (!g.valid() || !outY || g.axisU[0] == 0.0f || g.axisV[2] == 0.0f)
        return false;
    const i32 mask = g.size - 1;
    // Invert world = origin + col*axisU + row*axisV (axisU=(c,0,0), axisV=(0,0,-c)).
    i32 col = (i32)util::ConvertX(((double)wx - (double)g.origin[0]) / (double)g.axisU[0]);
    i32 row = (i32)util::ConvertX(((double)wz - (double)g.origin[2]) / (double)g.axisV[2]);
    col &= mask;                       // the floor grid wraps as a torus (mask)
    row &= mask;
    const u8 h = g.heights[(size_t)row * (size_t)g.size + (size_t)col];
    *outY = g.origin[1] + (float)h * g.axisH[1];
    return true;
}

// ---------------------------------------------------------------------------
// GroundFrame.
// ---------------------------------------------------------------------------
namespace {
// The computeClipFlags hook is a plain C pointer (the engine called the global-
// state VIBE_Render_ComputeVertexClipFlags @0x5ad614); the active frustum is
// threaded through file scope exactly like the engine's plane-table globals.
const render::Frustum* g_groundFrustum = nullptr;

void GroundClipFlagsHook(u8 /*clipFlag*/, void* vbuf, void* pbuf,
                         i32 vcount, i32 pcount) {
    if (!g_groundFrustum || !vbuf || !pbuf)
        return;
    render::ComputeVertexClipFlags(0x3F, static_cast<render::Vertex*>(vbuf), vcount,
                                   static_cast<render::Polygon*>(pbuf), pcount,
                                   *g_groundFrustum);
}

i32 GroundAlwaysRebuild(render::TerrainFloor*) { return 1; }

// ===========================================================================
// GROUND TILE TEXTURING (wave-5 W5-TX). The per-quad texture fetch + the UV
// assignment that VIBE_Floor_RenderTerrain @0x5bf22c performs inline:
//
//   @0x5c1f78..0x5c1f90  subTexId = byte_13DCE58[(quad&0xFF)+base] & 0x3F   (the
//                        per-cell sub-pattern; all-zero in the shipped image ->
//                        0, so the single flt_13FE540 record is used)
//   @0x5c1fc8            texRec = VIBE_TextureCache_GetOrBuildTile(...)  (eax)
//   @0x5c1fcd            uvBase = subTexId * 0x60  (96 bytes == 24 floats)
//   @0x5c1fdc            poly+0x14 = texRec   (the bound texture record)
//   @0x5c1fe5            if (cellFlag & 0x80)  (BuildTilePolys +0x80 visible bit)
//   @0x5c1ff2/0x5c1ff5   poly+0x3c = texRec; uvPtr = &flt_13FE540[uvBase]
//   @0x5c2008            poly+0x10 = uvPtr           (tri0 UVs: 6 floats)
//   @0x5c201b            poly+0x38 = uvPtr + 0x18    (tri1 UVs: next 6 floats)
//
// flt_13FE540 is the corner-inset UV record VIBE_Render_ComputeFilterWeights
// @0x5b94cc builds (terrain_uvtable.cpp / GroundFrame.uvTable_): 4 triangles x 3
// verts x 2 floats. Our split emits two Polygon records per quad (p0 == engine
// tri0 -> T0 == floats[0..5], p1 == engine tri1 -> T1 == floats[6..11]); the UV
// selector is the per-tile poly-pair parity (even == T0, odd == T1).
//
// The slot-name -> loaded-record FETCH is the W5-TILE FloorTextureResolver,
// installed by CityView3D as TerrainRenderHooks::getTileTexture; GroundFrame
// turns the record into the (texels + 565 palette) the textured span samples
// via the GroundTexBinder. Unbound binder / null record -> the white default.
// ===========================================================================

// File-scope active ground-texturing state for the slot-dispatch trampoline
// (the engine kept the bound texture + UV table in process globals; this mirrors
// that, single ground render at a time — the same pattern as g_groundFrustum).
struct GroundTexState {
    const GroundTexBinder* binder = nullptr;  // CityView3D's per-type record source
    const float*           uvTable = nullptr; // flt_13FE540 (24 floats)
    // WAVE-7 W7-WATERTEX: the loaded EF_WASS water texture record (WaterMesh+4 handle,
    // null == headless / white default). A water poly (flags38 & 0x02, uvZ>0) samples
    // THIS record + the scroll (poly.uvX/uvY = mesh[82]/[83]) instead of a ground slot.
    const render::Texture* waterTex = nullptr;
    // WAVE-7 W7-FOGPIX: per-frame D3D vertex-fog state; null/!enabled -> no per-vertex
    // fog. Each textured span seeds RgbzVertex::fogFactor = ComputeFogFactor(view-z²).
    const render::FogState* fog = nullptr;
};
GroundTexState g_groundTex{};

// WAVE-7 W7-FOGPIX — seed the per-vertex fog factor (ComputeFogFactor over the
// vertex squared view-space distance, the engine's @0x5beb0b terrain pass) when the
// frame's fog state is enabled. The vertex keeps its view-space x/y/z (the reproject
// writes only screenX/Y), so d2 = x²+y²+z². No-op (factor stays 255) when fog off.
inline void SeedSpanFogFactor(render::RgbzVertex& rv, const render::Vertex& v) {
    if (g_groundTex.fog) {
        const float dx = v.x, dy = v.y, dz = v.z;
        rv.fogFactor = render::ComputeFogFactor(*g_groundTex.fog, dx*dx + dy*dy + dz*dz);
    }
}

// The getOrBuildTile walk hook (@0x5ba1e8 stand-in): resolve the cell's type byte
// to a texture through the active getTileTexture hook; return the type byte+1 when
// a texture binds (stamped into poly.uvZ by the walk, exactly the engine's
// poly+0x14 texId), else 0 (untextured). The +1 keeps a zero "no texture" id
// distinct from type 0 (which IS a real slot).
u32 GroundGetOrBuildTile(const u8* texSrc, i32 width, i32 u, i32 v, i32 lod,
                         void* /*cacheState*/) {
    if (!texSrc || width <= 0)
        return 0;
    (void)lod;
    const u32 mask = (u32)width * (u32)width - 1u;
    const u32 idx  = (((u32)v * (u32)width) + (u32)u) & mask;
    const u8  typeByte = texSrc[idx];
    const auto& hooks = GetTerrainRenderHooks();
    if (!hooks.getTileTexture || !hooks.getTileTexture(typeByte))
        return 0;
    return (u32)typeByte + 1u;   // texId carried in poly.uvZ
}

// The ground textured span (custom SpanDispatch slot 3/4). Decodes the poly's
// type byte (uvZ-1) + UV selector (uvX: 0 -> T0, 1 -> T1), resolves the texture
// + 565 palette through the active binder, and rasterizes textured via the 1:1
// affine leaf RasterizeTexturedTriangleRgbz (gilde.exe 0x5F6C30) with the corner
// UVs (* mipWidth). Falls back to the white-default opaque slot when untextured.
int GroundSpanTextured(render::Surface* fb, const render::Polygon& tri) {
    const render::Vertex* vp[3] = {tri.v0, tri.v1, tri.v2};
    const GroundTexState& gs = g_groundTex;
    // WAVE-7 W7-WATERTEX — water poly (the engine's water +38 no-cull bit, flags38 &
    // 0x02, set by RenderWaterSurface @0x5be668): sample the loaded EF_WASS texture
    // (WaterMesh+4) instead of a ground slot. The poly carries the per-frame texture
    // scroll (poly.uvX/uvY == mesh[82]/[83], the AnimateWaterVertices texAccum) — the
    // engine copies those onto every water poly (result+28/+32) so the ripple scrolls.
    // The corner UVs are the canonical per-cell quad UV (the engine's water mesh maps
    // one EF_WASS tile per cell) + the scroll add. Null waterTex / 8bpp -> the
    // white-default level-shaded fallback (byte-identical to wave-6).
    if (fb && fb->bpp != 8 && vp[0] && vp[1] && vp[2] && (tri.flags38 & 0x02u) &&
        tri.uvZ > 0.0f && gs.waterTex && gs.binder && gs.binder->palette565 &&
        !gs.waterTex->texels.empty() && gs.waterTex->mipWidth > 0) {
        const render::Texture* rec = gs.waterTex;
        const u16* palBase = gs.binder->palette565(rec);
        if (palBase) {
            const float w = (float)rec->mipWidth;
            const float sU = tri.uvX, sV = tri.uvY;     // mesh[82]/[83] scroll accum
            // Canonical per-cell quad UVs for the triangle's 3 corners (one EF_WASS
            // tile per water cell), shifted by the animated scroll. tri0/tri1 of a
            // cell share the same (0,0)/(1,0)/(0,1)/(1,1) corner set; the affine span
            // samples the wrapped texel so the ripple tiles + scrolls.
            static const float kCellUV[3][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
            render::RgbzVertex rv[3];
            for (int i = 0; i < 3; ++i) {
                rv[i].x = vp[i]->screenX;
                rv[i].y = vp[i]->screenY;
                rv[i].u = (kCellUV[i][0] + sU) * w;
                rv[i].v = (kCellUV[i][1] + sV) * w;
                u32 L = vp[i]->lightIdx;
                rv[i].light = (u8)(L > 62u ? 62u : L);
                SeedSpanFogFactor(rv[i], *vp[i]);   // W7-FOGPIX
            }
            if (render::RasterizeTexturedTriangleRgbz(fb, rv, *rec, palBase, tri.flags38))
                return 1;
        }
    }
    if (fb && fb->bpp != 8 && vp[0] && vp[1] && vp[2] && gs.binder && gs.uvTable &&
        gs.binder->getTileTextureRec && gs.binder->palette565 && tri.uvZ > 0.0f) {
        const u8 typeByte = (u8)((i32)tri.uvZ - 1);
        const render::Texture* rec = gs.binder->getTileTextureRec(typeByte);
        if (rec && !rec->texels.empty() && rec->mipWidth > 0) {
            // palette565 returns the record's HiColTab block (63 light-ramp rows of
            // 256 entries == *(tex+72) palBase); the textured span indexes it as
            // palBase[(avg<<8)|texel] — i.e. light-ramp row `avg` of the texel's
            // source-index colour. The texel indices are the raw source indices,
            // so the block is laid out by source index (row L entry i = palette[i]
            // * L/62), reproducing VIBE_HiColTab ramp semantics.
            const u16* palBase = gs.binder->palette565(rec);
            if (palBase) {
                // UV record: even poly of the quad -> T0 (floats 0..5), odd -> T1
                // (floats 6..11). The selector lives in poly.uvX (set post-walk).
                const int sel = (tri.uvX != 0.0f) ? 6 : 0;
                const float* uv = gs.uvTable + sel;
                const float w = (float)rec->mipWidth;
                render::RgbzVertex rv[3];
                for (int i = 0; i < 3; ++i) {
                    rv[i].x = vp[i]->screenX;
                    rv[i].y = vp[i]->screenY;
                    rv[i].u = uv[i * 2 + 0] * w;     // corner U * width
                    rv[i].v = uv[i * 2 + 1] * w;     // corner V * width
                    // The tile's per-vertex illumination (BuildTileVertex +66): the
                    // span palette row is avg(+66 bytes)<<8 (dword_13FC5E0). The
                    // HiColTab has 63 ramp rows, so clamp the row to [0,62] (a row
                    // beyond the ramp count is the engine's degenerate region; the
                    // block only provisions 63 rows + the direct row at 0x7E00).
                    u32 L = vp[i]->lightIdx;
                    rv[i].light = (u8)(L > 62u ? 62u : L);
                    SeedSpanFogFactor(rv[i], *vp[i]);   // W7-FOGPIX
                }
                if (render::RasterizeTexturedTriangleRgbz(fb, rv, *rec, palBase,
                                                          tri.flags38))
                    return 1;
            }
        }
    }
    // Untextured: the established white-default level-shaded fallback (format-
    // routed by render::SpanFillTexturedOpaque — 8bpp shade / 16bpp white default).
    return render::SpanFillTexturedOpaque(fb, tri);
}
} // namespace

bool GroundFrame::Bind(const FloorGround* g) {
    g_ = nullptr;
    if (!g || !g->valid())
        return false;
    const i32 span = g->tileSpan;
    // Per-tile buffer sizing: a LOD-1 tile builds (span+1)^2 vertices and
    // 2*span^2 quads polys; the seam stitch appends up to span per edge. The
    // (span+2)^2 envelope matches the established walk-harness sizing.
    const std::size_t maxV = (std::size_t)(span + 2) * (std::size_t)(span + 2);
    const std::size_t maxP = 2 * maxV + 4 * (std::size_t)(span + 2);
    tiles_.assign(64, render::TerrainTile{});
    vbufs_.assign(64, {});
    pbufs_.assign(64, {});
    for (int i = 0; i < 64; ++i) {
        vbufs_[(std::size_t)i].assign(maxV, render::Vertex{});
        pbufs_[(std::size_t)i].assign(maxP, render::Polygon{});
        tiles_[(std::size_t)i].vertexBuf = vbufs_[(std::size_t)i].data();
        tiles_[(std::size_t)i].polyBuf   = pbufs_[(std::size_t)i].data();
        tiles_[(std::size_t)i].lod = 0;
        tiles_[(std::size_t)i].prevLod = 0;
    }
    dl_.assign((std::size_t)64 * maxP, render::DrawListEntry{});

    // WINDING (wave-5, @0x5bf22c re-decompiled): the row-mirror hack is REMOVED.
    // The Pass-A quad emission (@0x5bf22c lines 730..786) and the Pass-C signed-
    // area cull (lines 1815..1836) were recovered 1:1; terrain_walk's winding now
    // matches the binary (tri0 = (i00,i11,i10), tri1 = (i00,i01,i11) for the >=0
    // split; the BL-TR diagonal for <0). Under the engine's from-above projection
    // that genuine winding is front-facing (area <= 0 keeps), so no lattice mirror
    // is needed — and the mirror was geometrically WRONG (it produced a N-S
    // flipped surface). The floor is fed in its true row order.
    const i32 n = g->size;
    (void)n;

    floor_ = render::TerrainFloor{};
    floor_.size     = g->size;
    floor_.tileSpan = g->tileSpan;
    // Floor+0x08: the 0x5bd44c loader writes (N-1) | (N*N-1) == N*N-1 — the
    // LINEAR cell-index mask (@0x5bd54e..0x5bd55e: v25 = (N-1)|(N*N-1) -> +8;
    // N-1 -> +12). The walk's cell arithmetic masks LINEAR indices, so N*N-1 is
    // the faithful value (size-1 collapses the sampling onto the first row).
    floor_.mask     = g->size * g->size - 1;
    floor_.heights  = g->heights.data();
    floor_.texSrc   = g->texGrid.data();
    // Floor+0x1C per-cell type/light bytes: the AllocLightBuffers @0x5bced8 fill
    // was not captured — HOST SEED: the normalized texture grid (header note).
    floor_.types    = g->texGrid.data();
    for (int l = 0; l < 4; ++l)
        floor_.mipTexSrc[l] = g->texGrid.data();
    floor_.tiles = tiles_.data();
    // +7280: bit0 (build geometry) set per frame in Render(); bit1 (flat-lit)
    // CLEAR — the 0x5bd44c loader masks *(floor+7280) &= 0xE1 (@0x5bd817), so
    // the shipped floor takes the LIT branch (sun globals).
    floor_.flatLit = 0;
    // +7281 low nibble (minimum LOD): the loader writes byte_64A02D & 0xF
    // (@0x5bd4c8..0x5bd4e4), a TextureCache_Setup runtime byte — seed 0.
    floor_.minLodNibble = 0;
    for (int c = 0; c < 3; ++c)
        floor_.lightSunScale[c] = 1.0f;   // Floor+208 writer: named gap (host 1.0)

    // Per-tile min/max elevation summary (ComputeSlopeFlags @0x5bbdb0 third pass)
    // — feeds the 8-corner tile bound the LOD pick uses.
    render::SummarizeTileElevations(g->heights.data(), nullptr, g->size,
                                    g->size - 1, g->tileSpan, &elev_);
    // The flt_13FE540 UV-table image (BuildTerrainUvTable @0x5b94cc); 64 is the
    // shipped dword_64A038 mip tile size (SetMipFilterLevel @0x5b9e74 clamp top).
    render::BuildTerrainUvTable(uvTable_, 64);

    std::memset(pendingLod_, 0, sizeof(pendingLod_));
    std::memset(lodCounter_, 0, sizeof(lodCounter_));
    g_ = g;
    return true;
}

GroundRenderStats GroundFrame::Render(render::Surface* fb, const GroundViewParams& vp,
                                      char frameFlags) {
    GroundRenderStats stats;
    if (!bound() || !fb)
        return stats;

    // The SAME camera transform the city instances render with (ComposeModelView
    // Matrix): Rc = MatrixFromEuler(-rot); view dir = Transpose(Rc) * world dir;
    // view point = Rc^T * (world - eye) (WorldToView).
    const float negRot[3] = {-vp.rot[0], -vp.rot[1], -vp.rot[2]};
    const render::Mat3 Rc = render::MatrixFromEuler(negRot);
    const render::Mat3 Vr = render::Transpose(Rc);

    render::TerrainRenderState st;
    st.engineOn = 1;
    // Phase-0 light select (@0x5bf22c lines 505..531): the shipped floor is LIT
    // (Floor+7280 bit1 cleared by the loader) — sun ambient/bias are the captured
    // binary values, the scale seed is the named gap (header).
    render::SetupTerrainLight(st, (floor_.flatLit & 2) != 0,
                              vp.sunScale, vp.sunAmbient, vp.sunBias);
    // Phase-0 axis rotation (lines 532..578): floor origin/axes into view space.
    // WINDING (wave-5): the row-mirror is removed — the floor origin/axes are
    // used in their true sense (axisU = +X, axisV = -Z, axisH = +Y). The genuine
    // @0x5bf22c winding is front-facing from above without any axis negation.
    render::WorldToView(Rc, vp.eye, g_->origin, st.originView);
    render::Apply(Vr, g_->axisU, st.axisU);
    render::Apply(Vr, g_->axisV, st.axisV);
    render::Apply(Vr, g_->axisH, st.axisH);
    // The SetupViewTransform projection scalars (flt_13FCD0C/D18/AF8/D10).
    st.projXScale = vp.proj.xScale;
    st.projXOff   = vp.proj.xOffset;
    st.projYScale = vp.proj.yScale;
    st.projYOff   = vp.proj.yOffset;

    render::DrawList dl{dl_.data(), 0, (i32)dl_.size()};
    st.drawList = &dl;

    // ---- per-tile LOD (the 0x5bef08 chain over view-space tile bounds) ------
    render::TileBuildParams tp;
    for (int k = 0; k < 3; ++k) {
        tp.axisU[k]      = st.axisU[k];
        tp.axisRow[k]    = st.axisV[k];
        tp.axisHeight[k] = st.axisH[k];
        tp.origin[k]     = st.originView[k];
    }
    const float tileScale = (g_->cellScale != 0.0f) ? g_->cellScale : 1.0f;
    for (i32 row = 0; row < 8; ++row) {
        for (i32 col = 0; col < 8; ++col) {
            const int t = (int)(row * 8 + col);
            // 8 corner vertices (ComputeTileVertices @0x5bdec4) over the tile's
            // min/max elevation bytes (tile+316/+317, SummarizeTileElevations).
            float corners[24];
            render::ComputeTileVertices(tp, g_->tileSpan,
                                        elev_.minHeight[t], elev_.maxHeight[t],
                                        col, row, corners);
            // ComputeTileCenterRadius walks 20-float (80-byte Vertex) strides.
            float c20[8 * 20] = {0};
            for (int k = 0; k < 8; ++k) {
                c20[k * 20 + 0] = corners[k * 3 + 0];
                c20[k * 20 + 1] = corners[k * 3 + 1];
                c20[k * 20 + 2] = corners[k * 3 + 2];
            }
            float centre[3];
            const float radius = render::ComputeTileCenterRadius(c20, centre);
            // ComputeLodLevel @0x5ba438 with the 0x5bd44c thresholds:
            // Floor+7268 = 20.0 (0x41A00000), Floor+7272 = 40.0 (0x42200000).
            u8 lod = render::ComputeLodLevel(floor_.flatLit, /*hasEntries=*/true,
                                             /*thrFar=*/40.0f, /*thrNear=*/20.0f,
                                             tileScale, /*tileBias=*/0.0f, radius,
                                             /*visibleCount=*/64,
                                             tiles_[(std::size_t)t].prevLod,
                                             &pendingLod_[t], &lodCounter_[t]);
            // 2:1 seam resolve vs the already-resolved left/up neighbour.
            lod = render::StitchTileLod(lod,
                                        col > 0 ? tiles_[(std::size_t)(t - 1)].lod : (u8)0,
                                        row > 0 ? tiles_[(std::size_t)(t - 8)].lod : (u8)0,
                                        floor_.minLodNibble);
            tiles_[(std::size_t)t].lod = lod;
            if (lod) {
                ++stats.tilesDrawn;
                ++stats.lodCounts[lod < 5 ? lod : 4];
            }
        }
    }
    // Neighbour edge LODs for the Pass-B stitch.
    for (i32 row = 0; row < 8; ++row) {
        for (i32 col = 0; col < 8; ++col) {
            render::TerrainTile& t = tiles_[(std::size_t)(row * 8 + col)];
            t.edgeRightLod = (col < 7) ? tiles_[(std::size_t)(row * 8 + col + 1)].lod : (u8)0;
            t.edgeLeftLod  = (col > 0) ? tiles_[(std::size_t)(row * 8 + col - 1)].lod : (u8)0;
            t.edgeUpLod    = (row > 0) ? tiles_[(std::size_t)((row - 1) * 8 + col)].lod : (u8)0;
            t.edgeDownLod  = (row < 7) ? tiles_[(std::size_t)((row + 1) * 8 + col)].lod : (u8)0;
        }
    }

    // Build-geometry dirty each frame (per-frame rebuilt buffers; the engine's
    // change-detect via UpdateTileVisibility @0x5bef08 is the documented host
    // simplification — the hook reports "changed").
    floor_.flatLit |= 1;

    render::TerrainWalkHooks hooks;
    hooks.updateVisibility = &GroundAlwaysRebuild;
    hooks.computeClipFlags = &GroundClipFlagsHook;   // the real @0x5ad614 leaf
    // wave-5 W5-TX: the per-quad texture FETCH (@0x5ba1e8). When a tex binder is
    // bound AND the active getTileTexture hook resolves a slot, the walk stamps
    // the texId (typeByte+1) into each quad poly's uvZ (poly+0x14). Unbound ->
    // null hook -> stays inert (uvZ == 0, the white-default fallback).
    const bool wantTex = texBinder_.getTileTextureRec != nullptr &&
                         texBinder_.palette565 != nullptr &&
                         GetTerrainRenderHooks().getTileTexture != nullptr;
    if (wantTex)
        hooks.getOrBuildTile = &GroundGetOrBuildTile;

    g_groundFrustum = vp.frustum;
    stats.appended = render::RenderTerrain(&floor_, st, hooks, frameFlags != 0);
    g_groundFrustum = nullptr;
    stats.vertsBuilt = st.vertsThisFrame;

    // ---- post-walk UV-record selector (@0x5bf22c poly+0x10 / poly+0x38) -------
    // The walk emits the quad's two triangles consecutively into each tile's poly
    // buffer (p0 == engine tri0 -> T0, p1 == engine tri1 -> T1). Tag each poly's
    // UV selector by its per-tile poly-pair parity so the textured span picks the
    // correct corner-UV record (the phantom stitch indices carry flags36 < 0x80
    // and are never appended/drawn). Done over the per-tile buffers in BUILD
    // order, before the draw list is flushed.
    if (wantTex) {
        for (int t = 0; t < 64; ++t) {
            render::Polygon* pb = pbufs_[(std::size_t)t].data();
            const i32 pc = tiles_[(std::size_t)t].polyCount;
            for (i32 i = 0; i < pc && i < (i32)pbufs_[(std::size_t)t].size(); ++i)
                pb[i].uvX = (float)(i & 1);   // even -> T0 (0), odd -> T1 (1)
        }
    }

    // ---- WATER SUB-PASS (wave-6 W6-WR; VIBE_Floor_TransformTileGeometry @0x5be668)
    // The engine draws water in RenderTerrain's Phase-2 tail (the per-tile call at
    // 0x5c17fd), in the SAME draw list as the ground tiles, AFTER them — under the
    // SAME view-space axes / projection scalars / light params Phase-0 gathered.
    // Append the animated water region surface here (before the flush), then it
    // rasterizes through the same MeshList flush below. waterMeshCount_ == 0 (no
    // water) leaves the draw list byte-identical to the ground-only frame.
    waterStats_ = render::WaterDrawStats{};
    if (waterMeshCount_ > 0 && !waterVbuf_.empty() && !waterPbuf_.empty()) {
        render::WaterDrawInput wi{};
        wi.size  = g_->size;
        wi.mask  = g_->size - 1;
        wi.waterHeights = g_->waterHeights.empty() ? nullptr : g_->waterHeights.data();
        wi.types = g_->texGrid.data();          // Floor+0x1C type/shadow bytes
        wi.meshes = reinterpret_cast<const float*>(water_.waterMeshes.data());
        wi.meshCount = (u32)waterMeshCount_;
        wi.spans = water_.spans.empty() ? nullptr : water_.spans.data();
        wi.spanCount = (i32)water_.spans.size();
        wi.polys = water_.polys.empty() ? nullptr : water_.polys.data();
        wi.polyCount = (i32)water_.polys.size();
        waterStats_ = render::RenderWaterSurface(
            wi, st, waterVbuf_.data(), (i32)waterVbuf_.size(),
            waterPbuf_.data(), (i32)waterPbuf_.size(), dl, waterTexture_);
    }

    // flags38 bit0 proxy collision (header note): the split selector is baked
    // into the vertex pointers; clear the bit so the flush does not misread it
    // as the texture-translucency proxy.
    for (i32 i = 0; i < dl.count; ++i)
        if (dl.entries[i].poly)
            dl.entries[i].poly->flags38 &= 0xFEu;

    // The REAL flush (@0x5AEC88) under the SAME clip planes + reproject scalars
    // the city object flush uses. wave-5 W5-TX: when a tex binder is bound the
    // dispatch slots 3/4 route through GroundSpanTextured (the corner-UV textured
    // span over the resolved slot BMP); otherwise the default slots draw the
    // engine's 16bpp LEVEL-shaded white-default leaf (byte-identical to before).
    render::MeshList list{dl_.data(), dl.count};
    render::ClipContext ctx{vp.clipPlaneCount,
                            reinterpret_cast<const render::ClipPlane*>(vp.clipPlanes)};
    render::ProjectScalars proj{vp.proj.xScale, vp.proj.xOffset,
                                vp.proj.yScale, vp.proj.yOffset};
    render::SpanDispatch dispatch;     // defaults: slot4 opaque / slot3 blend
    // WAVE-7 W7-WATERTEX: route water polys through the textured span sampling the
    // loaded EF_WASS record (waterTexture_) when one is bound; ground tiles keep the
    // ground-slot path. With no EF_WASS record waterTex stays null -> water draws the
    // white default (byte-identical to wave-6). The water span needs the binder's
    // palette565 even without ground slot textures, so enable the dispatch when EITHER
    // ground textures OR a water texture is bound.
    const render::Texture* waterTexRec =
        static_cast<const render::Texture*>(waterTexture_);
    const bool wantWaterSpan = waterTexRec != nullptr && texBinder_.palette565 != nullptr;
    if (wantTex || wantWaterSpan) {
        g_groundTex.binder   = &texBinder_;
        g_groundTex.uvTable  = uvTable_;
        g_groundTex.waterTex = wantWaterSpan ? waterTexRec : nullptr;
        g_groundTex.fog      = (vp.fog && vp.fog->enabled) ? vp.fog : nullptr;  // W7-FOGPIX
        dispatch.slot[4] = &GroundSpanTextured;   // opaque (key>>24 == 4)
        dispatch.slot[3] = &GroundSpanTextured;   // translucent
    }
    static render::ClipScratch scratch;  // ~24KB; single render at a time (the
                                          // engine's clip globals are file-scope)
    stats.rasterTris = render::RasterizeMeshList(list, fb, dispatch, ctx, proj, scratch);
    if (wantTex || wantWaterSpan) {
        g_groundTex.binder = nullptr; g_groundTex.uvTable = nullptr;
        g_groundTex.waterTex = nullptr; g_groundTex.fog = nullptr;
    }
    return stats;
}

// ===========================================================================
// WATER PIPELINE (wave-6 W6-WR) — build + animate (the front of the water
// pipeline whose render arm is RenderWaterSurface / @0x5be668).
// ===========================================================================
int GroundFrame::BuildWater(render::WaterTextureLoadFn loadTexture, void* ctx) {
    waterMeshCount_ = 0;
    waterTexture_ = nullptr;
    water_ = render::WaterRegions{};
    waterVbuf_.clear();
    waterPbuf_.clear();
    if (!bound() || !g_->hasWater || g_->waterType == 0xFF)
        return 0;

    // VIBE_FloorWater_PrepareRegions @0x5ba95c over the parsed grid: the texGrid
    // is the water-mask grid (cells == waterType are water), heights seed the
    // gradient, waterHeights (or null -> built fresh) the per-cell water height.
    water_ = render::BuildWaterRegions(
        g_->texGrid.data(), g_->heights.data(),
        g_->waterHeights.empty() ? nullptr : g_->waterHeights.data(),
        g_->waterType, g_->size, loadTexture, ctx);
    waterMeshCount_ = water_.regionCount;
    waterTexture_   = water_.meshTexture;   // WaterMesh+4 handle (0 == headless)
    if (waterMeshCount_ <= 0) { waterMeshCount_ = 0; return 0; }

    // Size the per-frame water surface buffers: one vertex per span cell, two
    // triangle records per span run (the RenderWaterSurface fan). Bound generously
    // so the run/poly emit never overflows (cap-clamped inside the arm regardless).
    std::size_t cellTotal = 0;
    for (const auto& sp : water_.spans) {
        const i32 run = sp.hi - sp.lo + 1;
        if (run > 0) cellTotal += (std::size_t)run;
    }
    if (cellTotal == 0) { waterMeshCount_ = 0; return 0; }
    waterVbuf_.assign(cellTotal, render::Vertex{});
    waterPbuf_.assign(cellTotal * 2 + water_.spans.size() + 4, render::Polygon{});
    return waterMeshCount_;
}

void GroundFrame::AnimateWater(i32 time, render::FindGroupMemberFn findGroupMember,
                               void* ctx) {
    if (waterMeshCount_ <= 0 || water_.waterMeshes.empty())
        return;
    // VIBE_Floor_AnimateWaterVertices @0x5be428 — advance the wave grid + texture
    // member of every built region. The builder stored the RAW 86-float engine
    // layout (float[0]texPtr, [1]member, [3/4]texRate, [6..9]waveSpeed,
    // [10..13]amp, [14..77]waveOut, [78..81]phase, [82/83]texAccum, [84]lastTime);
    // the reconstructed AnimateWaterVertices consumes the named WaterMesh struct.
    // Bridge raw->struct, animate, copy the advanced waveOut/phase/accum/member/
    // lastTime back into the raw records (the only fields the arm/anim mutate).
    static auto NoMember = [](i32, u8, void*) -> u32 { return 0u; };
    render::FindGroupMemberFn fn = findGroupMember ? findGroupMember : +NoMember;

    const u32 n = (u32)waterMeshCount_;
    std::vector<render::WaterMesh> ms((std::size_t)n);
    float* raw = reinterpret_cast<float*>(water_.waterMeshes.data());
    for (u32 i = 0; i < n; ++i) {
        const float* r = raw + (std::size_t)i * 86u;
        render::WaterMesh& m = ms[i];
        m = render::WaterMesh{};
        m.hasTexture = r[0] != 0.0f;
        m.activeMember = (u32)(i32)r[1];
        m.texRateA = r[3]; m.texRateB = r[4];
        for (int k = 0; k < 4; ++k) { m.waveSpeed[k] = r[6 + k]; m.amp[k] = r[10 + k]; }
        for (int k = 0; k < 64; ++k) m.waveOut[k] = r[14 + k];
        for (int k = 0; k < 4; ++k) m.phase[k] = r[78 + k];
        m.texAccumA = r[82]; m.texAccumB = r[83];
        // lastTime is stored as raw bits (the engine writes the tick dword); the
        // builder zeroed it, so it starts 0 and AnimateWaterVertices latches `time`.
        m.lastTime = (i32)r[84];
        m.texMemberCount = 0; m.texSpeedNibble = 0; m.groupId = 0;  // headless texture
    }
    render::AnimateWaterVertices(ms.data(), n, time, fn, ctx);
    for (u32 i = 0; i < n; ++i) {
        float* r = raw + (std::size_t)i * 86u;
        const render::WaterMesh& m = ms[i];
        for (int k = 0; k < 64; ++k) r[14 + k] = m.waveOut[k];
        for (int k = 0; k < 4; ++k) r[78 + k] = m.phase[k];
        r[82] = m.texAccumA; r[83] = m.texAccumB;
        r[1] = (float)(i32)m.activeMember;
        r[84] = (float)m.lastTime;
    }
}

} // namespace guild::play
