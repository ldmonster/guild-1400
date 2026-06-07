#include "play/terrain_render.h"

#include "render/terrain_render.h"  // SelectTileMeshLod, TileSubdivCount, Floor LOD
#include "render/raster.h"          // RasterizeTexturedTriangle, RasterVertex
#include "render/surface.h"

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

} // namespace guild::play
