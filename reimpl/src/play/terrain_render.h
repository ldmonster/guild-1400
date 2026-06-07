#pragma once
// =============================================================================
// guild::play — REAL CITY TERRAIN / FLOOR RENDER (PLAYABLE_PLAN P2).
//
// Renders the game's ground floor — the heightmap/terrain grid the engine draws
// every frame under the scene — into a software render::Surface, using the REAL
// reconstructed terrain leaves and the REAL software rasterizer. It is the
// terrain half of the world render that render_binder.cpp leaves as a single flat
// quad: here the actual tessellated, per-tile-lit ground is drawn.
//
// THE ENGINE PATH WE MIRROR
// ---------------------------------------------------------------------------
// The live engine draws the floor through VIBE_Floor_RenderTerrain (@0x5bf22c,
// the largest function in gilde.exe). Its body is a LOD/visibility/texture-cache/
// water thicket that is LISTED deferred, but its deterministic INNER LEAVES are
// already reconstructed 1:1 and are what actually decide the geometry + shading:
//
//   render/terrain.h          TileGrid / TileType         (per-cell type byte)
//   render/terrain_render.h   SelectTileMeshLod           (size -> 1..4 LOD)
//                             TileSubdivCount             (per-tile subdiv + the
//                                                          col/row==7 seam stitch)
//   render/tile_geometry.h    BuildTileVertex             (height+type -> world xyz
//                                                          + per-channel RGB light,
//                                                          the RenderTerrain inner
//                                                          per-vertex loop)
//   render/raster.h           RasterizeTexturedTriangle   (the affine SHADED span
//                                                          rasterizer the floor
//                                                          polys flow through)
//
// This module wires those leaves into the same TILE-DRAW ORDER the walk uses
// (row-major over the 8x8 tile grid; each tile tessellated into TileSubdivCount^2
// quads; each quad = 2 triangles), projects each tile vertex to screen with the
// floor's world axes, derives the per-vertex shade byte from BuildTileVertex's
// RGB light, and rasterizes the quads into the Surface. The output 8bpp surface
// holds the per-pixel shade exactly as FillTexturedSpansShaded writes it.
//
// DEVICE LEAVES -> INERT HOOKS
// ---------------------------------------------------------------------------
// The original per-tile texture fetch (VIBE_TextureCache_GetOrBuildTile) and the
// DirectDraw present are device-coupled and NOT reconstructed; they are routed
// through TerrainRenderHooks whose defaults are inert (no texture => the flat
// SHADED raster path runs, which is the engine's untextured-floor fallback). The
// inert defaults live in this module's .cpp so the unified library links (per the
// build model: every src-referenced symbol is defined in src/).
// =============================================================================
#include "guild/common/types.h"
#include "render/surface.h"
#include "render/tile_geometry.h"   // TileLightParams, BuildTileVertex
#include "render/terrain.h"         // TileGrid

#include <vector>

namespace guild::play {

// ---------------------------------------------------------------------------
// A loaded floor heightfield: the size*size height + per-cell terrain-type byte
// grids the engine's Floor record carries (heights @Floor+16, types @Floor+28).
// `size` is the grid edge (a power of two; the grid wraps as a torus with
// mask == size-1). `tileSpan` is the samples-per-tile-edge before LOD subdivision
// (Floor+4); the 8x8 tile grid spans the whole field (size == 8 * tileSpan).
// A type byte's high bit (0x80) marks a shadowed cell (the engine's shadow
// branch) — NOT a hole here (holes are a separate poly-visibility concern handled
// by the deferred walk; this renderer draws every quad).
// ---------------------------------------------------------------------------
struct Heightfield {
    i32              size     = 0;   // grid edge (power of two)
    i32              tileSpan = 0;   // samples per tile edge (size / 8)
    std::vector<u8>  heights;        // size*size elevation bytes (row-major)
    std::vector<u8>  types;          // size*size terrain-type/shadow bytes

    bool valid() const {
        return size > 0 && tileSpan > 0 &&
               (i32)heights.size() == size * size &&
               (i32)types.size()   == size * size;
    }
    i32 mask() const { return size - 1; }
    render::TileGrid tileGrid() const { return { size, size - 1, types.data() }; }

    // Build a deterministic synthetic heightfield: a smooth radial dome with a
    // diagonal ridge, type bytes ramped by elevation (the high bit set on the
    // darker, lower band to exercise the shadow branch). `edge` MUST be a multiple
    // of 8 (so the 8x8 tile grid divides it). `seed` perturbs the field so e2e can
    // mix in real city bytes while keeping the shape ground-like.
    static Heightfield MakeSynthetic(i32 edge, u32 seed = 0);
};

// ---------------------------------------------------------------------------
// The per-frame view/transform the renderer projects tile vertices through. The
// world position of a sample is built by BuildTileVertex (height*axisH + acc); we
// then map world (x,z) -> screen with an orthographic-ish (origin + scale) pass,
// matching the floor's flattened top-down draw. The lighting params are the same
// TileLightParams the engine's inner loop reads (flt_13FD4F0.. globals).
//   light  : per-channel RGB light scale/ambient/shadow (TileLightParams)
//   axisX/axisZ : world step per tile COLUMN / ROW sample (the floor's axisU/axisV
//                 rotated into view; flt_13FFD40.. / flt_13FD500..)
//   screenScale / screenOrigin : world(x,z) -> pixel mapping
// ---------------------------------------------------------------------------
struct TerrainView {
    render::TileLightParams light{};

    float axisX[3] = {1, 0, 0};   // world step per column sample (axisU)
    float axisZ[3] = {0, 0, 1};   // world step per row sample    (axisV)

    float screenScaleX = 1.0f;    // world.x -> pixels
    float screenScaleY = 1.0f;    // world.z -> pixels
    float screenOriginX = 0.0f;   // pixel x of world (0,0)
    float screenOriginY = 0.0f;   // pixel y of world (0,0)

    // Build a sensible top-down view that fits a `size`-edge field into `fbW`x`fbH`
    // pixels with a neutral sun (per-channel light scale ~1, small ambient). Used
    // by the integration / e2e renders.
    static TerrainView MakeTopDown(i32 size, i32 tileSpan, int fbW, int fbH);
};

// ---------------------------------------------------------------------------
// Cross-module device leaves the original RenderTerrain calls but which are not
// reconstructed. Defaults are INERT (defined in terrain_render.cpp): no texture =>
// the untextured SHADED raster path runs. Tests may install recording mocks.
// ---------------------------------------------------------------------------
struct TerrainRenderHooks {
    // VIBE_TextureCache_GetOrBuildTile — per-tile texture fetch. Inert default
    // returns nullptr (untextured: the shaded fallback draws). `typeByte` is the
    // tile's dominant terrain-type byte. Return value is opaque to this module.
    const void* (*getTileTexture)(u8 typeByte);
};
void SetTerrainRenderHooks(const TerrainRenderHooks* hooks);
const TerrainRenderHooks& GetTerrainRenderHooks();

// ---------------------------------------------------------------------------
// Per-render statistics, read back for assertions.
// ---------------------------------------------------------------------------
struct TerrainRenderStats {
    int lod          = 0;   // SelectTileMeshLod result (1..4)
    int tilesDrawn   = 0;   // tile records visited (<= 64)
    int quadsBuilt   = 0;   // quads tessellated across all tiles
    int trisDrawn    = 0;   // triangles RasterizeTexturedTriangle emitted spans for
    int nonBlankPix  = 0;   // framebuffer pixels != background after the draw
    u8  minShade     = 255; // min / max per-vertex shade byte observed (lighting)
    u8  maxShade     = 0;
};

// ===========================================================================
// Geometry / lighting MATH leaves (golden-vector testable in isolation).
// ===========================================================================

// One tessellated tile vertex ready for the rasterizer: its projected screen
// position and the shade byte derived from BuildTileVertex's per-channel light.
struct TileVertex {
    float screenX = 0, screenY = 0;
    float worldX = 0, worldY = 0, worldZ = 0;
    u8    shade   = 0;   // luma of the BuildTileVertex RGB light (raster light in)
    u8    r = 0, g = 0, b = 0;  // the per-channel light bytes (golden checks)
};

// Convert a per-channel RGB light to the single 8-bit shade the affine raster
// consumes: luma = (R*77 + G*150 + B*29) >> 8 (the standard 0.299/0.587/0.114
// integer weights). Deterministic; matches what the shaded span writes.
u8 ShadeFromRgb(u8 r, u8 g, u8 b);

// Build ONE tessellated terrain vertex at GRID sample (sx, sy) (in [0,size]) for
// the given heightfield + view. Samples the (wrapped) height + type byte, runs
// the REAL render::BuildTileVertex to get world xyz + RGB light, then projects
// world(x,z) to screen via the view's scale/origin. Returns the vertex; this is
// the deterministic geometry+lighting core the unit golden vectors pin down.
TileVertex BuildTerrainVertex(const Heightfield& hf, const TerrainView& view,
                              i32 sx, i32 sy);

// ===========================================================================
// The renderer.
// ===========================================================================

// Rasterize the whole floor into `fb` (an 8bpp surface; the shaded raster writes
// one shade byte per pixel). Walks the 8x8 tile grid in row-major order (the
// engine's tile-draw order), tessellates each tile into TileSubdivCount^2 quads
// (with the col/row==7 seam stitch), builds each quad's 4 vertices via
// BuildTerrainVertex and draws it as two RasterizeTexturedTriangle calls.
// `background` is the byte the surface was cleared to (for the non-blank count).
// Returns the per-render stats. `fb` MUST be non-null and 8bpp.
TerrainRenderStats RenderTerrain(render::Surface* fb, const Heightfield& hf,
                                 const TerrainView& view, u8 background = 0);

// Allocate an 8bpp surface cleared to `background`, render the floor into it, and
// return it (caller owns it; free with render::SurfaceDestroy). `outStats` is
// optional. Returns nullptr on allocation failure or an invalid heightfield.
render::Surface* RenderTerrainToSurface(int fbW, int fbH, const Heightfield& hf,
                                        const TerrainView& view, u8 background,
                                        TerrainRenderStats* outStats);

} // namespace guild::play
