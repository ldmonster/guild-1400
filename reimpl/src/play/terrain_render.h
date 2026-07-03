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

// =============================================================================
// 3D GROUND PASS (terrain-ground wave 4) — the city floor drawn through the
// REAL frame spine at the VERIFIED position: render::BeginUniverseFrame @0x5B3900
// invokes hooks.renderTerrain (frame.cpp, the 0x5b3a2f `VIBE_Floor_RenderTerrain(
// dword_64A028, a2)` arm) AFTER the clear and BEFORE the object scene walk.
//
// DATA: the REAL parsed floor block of the loaded city (render/scene_floor.h,
// VIBE_WorldIo_LoadFloorRegions @0x5e78a8 over the embedded .cty blob /
// stadt_*.ed3), turned into the engine Floor placement by the byte-exact
// VIBE_Floor_LoadFromHeightmap @0x5bd44c math (DeriveFloorPlacement: origin =
// (-N/2*cell, heightScale*-64, N/2*cell), axisU=(cell,0,0), axisV=(0,0,-cell),
// axisH=(0,heightScale,0); the trailing stream origin vec3 OVERWRITES floor
// +144/148/152 @0x5e7d28 when present) and the min-normalized texture grid.
//
// WALK: the complete reconstructed VIBE_Floor_RenderTerrain @0x5bf22c
// (render/terrain_walk.h) over per-tile LODs picked through the real leaves
// (ComputeTileVertices @0x5bdec4 -> ComputeTileCenterRadius @0x5bef08 bound ->
// ComputeLodLevel @0x5ba438 with the 0x5bd44c thresholds Floor+7268=20.0 /
// +7272=40.0 -> StitchTileLod @0x5bef08), axes rotated into view space exactly
// as the city objects transform (R_view = Transpose(MatrixFromEuler(-rot)),
// view = R_view*(world - eye)). The appended draw list is flushed through the
// REAL VIBE_Render_RasterizeMeshList @0x5AEC88 with the same six-plane clip set
// + SetupViewTransform reproject scalars the city object flush uses.
//
// NAMED GAPS (rule 8):
//  * Tile TEXTURES: VIBE_Floor_LoadTexture @0x5bd010 (slot-name -> texture) and
//    VIBE_TextureCache_GetOrBuildTile @0x5ba1e8 were NOT captured — the
//    getOrBuildTile hook stays inert (0 == untextured), so ground polys render
//    through the engine's untextured path (the 16bpp LEVEL-shaded 1x1 white
//    default, BindActive @0x5db564 slot==0 — the established in-tree behaviour).
//  * Floor+0x1C per-cell type/light bytes: filled at runtime by
//    VIBE_Floor_AllocLightBuffers @0x5bced8 + the light stamps (fill not
//    captured). HOST SEED: the min-normalized texture-grid byte (the per-cell
//    terrain-slot index 0..7) — it feeds BuildTileVertex's (type&0x7F)*2 light
//    term, giving each terrain class a distinct level shade.
//  * Light scale Floor+208/212/216: writer not captured; host seed 1.0. The sun
//    ambient/bias ARE captured: flt_64A074/78/7C = 200.0, flt_64A084/88/8C = 0.0.
//  * Per-frame visibility change-detect (VIBE_Floor_UpdateTileVisibility
//    @0x5bef08 whole-walk): the LOD leaves above are real; the change-detect is
//    forced to "rebuild" each frame (our tile buffers are per-frame).
//  * Polygon::flags38 bit0 is this repo's texture-translucency PROXY in the
//    flush; the walk's Pass A reuses the same bit as the quad-split selector
//    (the engine keeps them in separate records). The split is baked into the
//    vertex pointers during the build, so the bit is cleared before the flush.
//  * ROW WINDING (re-verify @0x5bf22c when the MCP returns): the in-tree walk's
//    Pass-A quad order — translated from a decompile that is no longer in the
//    captured evidence — winds CLOCKWISE for a floor viewed from ABOVE under
//    the engine projection (all polys backface-culled, both azimuths, real
//    AUGSBURG data). The original observably draws the ground from above, so
//    the ground pass feeds the ROW-MIRRORED lattice (rows reversed; origin' =
//    origin + (N-1)*axisV, axisV' = -axisV): the identical world surface with
//    counter-clockwise-from-above traversal. The walk itself is untouched.
//  * Floor+0x08 is the LINEAR cell mask N*N-1 (0x5bd44c @0x5bd54e: (N-1)|
//    (N*N-1) -> +8; N-1 -> +12) — the TerrainFloor.mask doc comment in
//    render/terrain_walk.h says "size-1", which collapses the walk's linear
//    cell arithmetic onto the first grid row; the ground pass feeds N*N-1
//    (handoff note for the terrain_walk owner).
// =============================================================================
#include "render/object_project.h"   // ObjectProjectScalars (flt_13FCD0C..)
#include "render/scene_floor.h"      // SceneFloorBlock / DeriveFloorPlacement
#include "render/terrain_mesh.h"     // TileElevationSummary (0x5bbdb0 summary)
#include "render/terrain_walk.h"     // TerrainFloor/TerrainTile/RenderTerrain
#include "render/tile_lighting.h"    // TileLightSource (Floor+0x1A64 names)
#include "render/floorwater.h"       // BuildWaterRegions / WaterRegions (0x5ba95c)
#include "render/water_render.h"     // RenderWaterSurface / WaterDrawInput (0x5be668)
#include "render/water_vertices.h"   // AnimateWaterVertices / WaterMesh (0x5be428)

namespace guild::render { struct Frustum; struct DrawListEntry; struct Texture; struct FogState; }

namespace guild::play {

// ---------------------------------------------------------------------------
// GROUND TILE TEXTURE BIND (wave-5 W5-TX) — the per-tile-type texture the
// ground raster samples. VIBE_Floor_RenderTerrain @0x5bf22c per quad calls
// VIBE_TextureCache_GetOrBuildTile @0x5ba1e8 (-> the slot's loaded texture
// record) and stamps it on the poly (poly+0x14/+0x3c), then assigns the quad's
// per-vertex UVs from the flt_13FE540 corner-inset table (+subTexId*0x60, the
// @0x5c1fcd/0x5c1ff5 reads; subTexId == 0 in the shipped binary -> the single
// 24-float record BuildTerrainUvTable builds). The texture FETCH (slot name ->
// loaded record) is the W5-TILE FloorTextureResolver, installed by CityView3D
// as TerrainRenderHooks::getTileTexture. This binder turns the resolver's
// render::Texture* into the (texels + 565 palette) the textured span samples.
//   getTileTextureRec : type byte -> the loaded render::Texture* (== the active
//                       getTileTexture hook return; null = untextured).
//   palette565        : the texture's 256-entry RGB565 LUT (built once per
//                       record by CityView3D from the decoded source palette).
// A null binder / null record -> the established white-default level-shaded
// fallback (byte-identical to the inert path).
// ---------------------------------------------------------------------------
struct GroundTexBinder {
    const render::Texture* (*getTileTextureRec)(u8 typeByte) = nullptr;
    const u16*             (*palette565)(const render::Texture* rec) = nullptr;
};

// ---------------------------------------------------------------------------
// The REAL parsed city floor, reduced to the engine Floor fields the walk reads
// (every field cites its Floor offset / 0x5bd44c derivation).
// ---------------------------------------------------------------------------
struct FloorGround {
    i32 size     = 0;          // floor+0   N (grid edge)
    i32 tileSpan = 0;          // floor+4   N/8 (@0x5bd7c1 signed idiv)
    std::vector<u8> heights;   // floor+16  N*N elevation bytes ("d3_fl:Height")
    std::vector<u8> texGrid;   // floor+20  N*N min-normalized slot indices
    u8  gridMin = 0;           // the 0x5bd44c min/max scan results
    u8  gridMax = 0;
    float origin[3] = {0,0,0}; // floor+144/148/152 (stream origin override applied)
    float axisU[3]  = {0,0,0}; // floor+160/164/168 = (cellScale, 0, 0)
    float axisV[3]  = {0,0,0}; // floor+176/180/184 = (0, 0, -cellScale)
    float axisH[3]  = {0,0,0}; // floor+192/196/200 = (0, heightScale, 0)
    float cellScale = 0;       // ctx+144 (the floor+160 axis length)
    float heightScale = 0;     // ctx+148 (the floor+196 axis length)
    render::TileLightSource typeNames[8] = {};  // floor+0x1A64 64-byte slots

    // WATER (wave-6 W6-WR): the inputs VIBE_FloorWater_PrepareRegions @0x5ba95c
    // reads. waterHeights == floor+0x18 (a1[6]) the AnimateWaterVertices/water
    // render arm sample; waterType == the min-normalized texGrid byte whose
    // typeName is "WASSER" (the loc_5CB930 name match the builder takes as a
    // parameter); hasWater == the block's waterFlag.
    std::vector<u8> waterHeights;  // floor+0x18 N*N water-height bytes (may be empty)
    u8   waterType = 0xFF;         // the min-normalized WASSER slot (0xFF == none)
    bool hasWater = false;         // the block waterFlag

    bool valid() const {
        return size >= 8 && tileSpan > 0 && (size & (size - 1)) == 0 &&
               (i32)heights.size() == size * size &&
               (i32)texGrid.size() == size * size;
    }
};

// Build the walk inputs from a parsed floor block: requires b.ok + accepted
// heights + accepted texture grid (the 0x5bd44c gate `if (floor+20 && floor+16)`;
// without both the engine frees the floor and returns 0 — mirrored as an
// invalid FloorGround). Applies DeriveFloorPlacement (@0x5bd44c), the stream
// origin override (@0x5e7d28) and NormalizeFloorTextureGrid (@0x5bd8ab..).
FloorGround BuildFloorGroundFromBlock(const render::SceneFloorBlock& b);

// Ground world Y at world (x,z): inverts the 0x5bd44c placement (cell = (wx -
// originX)/axisU.x, row = (wz - originZ)/axisV.z, both ConvertX-truncated and
// wrapped through mask) and samples worldY = origin.y + h*axisH.y. Returns false
// when the ground is invalid or the axes are degenerate. (The engine's bilinear
// query lives in WorldToTileWithHeight @0x5c6644 over the heightmap; this is the
// raw floor-lattice sample used by the build/e2e checks.)
bool FloorGroundWorldY(const FloorGround& g, float wx, float wz, float* outY);

// ---------------------------------------------------------------------------
// Per-frame view/projection parameters — the SAME camera/projection the city
// objects render with (CityView3D doSceneWalk fills these from its options).
// ---------------------------------------------------------------------------
struct GroundViewParams {
    float eye[3] = {0,0,0};       // camera node +76/+80/+84
    float rot[3] = {0,0,0};       // camera node +132/+136/+140 euler
    const render::Frustum* frustum = nullptr;     // BuildEngineFrustum result
    const float (*clipPlanes)[4] = nullptr;       // the 6-plane flush clip set
    int clipPlaneCount = 0;
    render::ObjectProjectScalars proj{};          // flt_13FCD0C/D18/AF8/D10
    // Sun light seeds (Phase-0 of @0x5bf22c): ambient flt_64A074/78/7C and bias
    // flt_64A084/88/8C are the CAPTURED binary values (200,200,200 / 0,0,0);
    // the Floor+208 scale writer is the named gap (host seed 1).
    float sunAmbient[3] = {200.0f, 200.0f, 200.0f};
    float sunBias[3]    = {0.0f, 0.0f, 0.0f};
    float sunScale[3]   = {1.0f, 1.0f, 1.0f};
    // WAVE-7 W7-FOGPIX: the per-frame D3D vertex-fog state the textured terrain/water
    // spans compute each vertex's per-pixel factor from (ComputeFogFactor over the
    // view-space depth). Null (or !enabled) -> no per-vertex fog (byte-identical).
    const render::FogState* fog = nullptr;
};

struct GroundRenderStats {
    int  tilesDrawn  = 0;   // tiles with a nonzero LOD this frame
    int  lodCounts[5] = {0,0,0,0,0};  // histogram over LOD 1/2/4
    i32  appended    = 0;   // draw-list entries the @0x5bf22c walk appended
    int  rasterTris  = 0;   // polys the @0x5AEC88 flush iterated
    i32  vertsBuilt  = 0;   // st.vertsThisFrame
};

// ---------------------------------------------------------------------------
// GroundFrame — owns the engine Floor/tile records + per-tile vertex/poly
// buffers + the draw list for one bound FloorGround (the engine's Floor
// allocations, VIBE_Floor_AllocTileBuffers @0x5BCB38 class), plus the
// flt_13FE540 UV-table image (BuildTerrainUvTable @0x5b94cc) rebuilt at bind.
// ---------------------------------------------------------------------------
class GroundFrame {
public:
    // Bind the walk buffers to `g` (kept by pointer; must outlive the frame).
    bool Bind(const FloorGround* g);

    // Drop every baked transition tile (a SEASON change swaps the slot
    // textures — stale bakes would blend last season's art).
    static void InvalidateTransitionBakes();

    // The Floor+0x1C per-cell LIGHT map (LIVE-CAPTURED model, see Bind): a
    // smoothed hillshade over the N*N height grid — flat ground 26, slopes
    // shaded by the fitted gradient response. Exposed for tests.
    static void BuildTerrainLightMap(const u8* heights, i32 n,
                                     std::vector<u8>& out);

    // TEST SEAMS for the transition-tile bake (the @0x5ba1e8 reconstruction —
    // the implementations live behind this module's internal linkage):
    //   TestBakeTransitionTile: bake (or fetch) the tile for a 3x3 type block
    //     (row-major, blk[4] = the cell) -> bake index, -1 = unresolvable.
    //   TestBakedTileRecord: the baked 8-bit texture record for an index.
    //   TestGetOrBuildTile: the walk hook body (type grid fetch + bake gate).
    static int TestBakeTransitionTile(const u8 blk[9]);
    static const render::Texture* TestBakedTileRecord(int idx);
    static u32 TestGetOrBuildTile(const u8* texSrc, i32 width, i32 u, i32 v);
    bool bound() const { return g_ != nullptr; }
    const FloorGround* ground() const { return g_; }
    const float* uvTable() const { return uvTable_; }   // flt_13FE540 image (24)

    // Run the @0x5bf22c walk + the @0x5AEC88 flush into `fb` under `vp`.
    // `frameFlags` mirrors the BeginUniverseFrame a2 byte (the water-anim gate).
    GroundRenderStats Render(render::Surface* fb, const GroundViewParams& vp,
                             char frameFlags);

    // Bind the per-tile-type texture source (wave-5 W5-TX). When set AND the
    // active TerrainRenderHooks::getTileTexture resolves a texture for a tile's
    // type byte, the ground polys flush TEXTURED (the flt_13FE540 corner UVs over
    // the real slot BMP); when null/unbound, the established white-default
    // level-shaded fallback runs (byte-identical). ADDITIVE: an unbound binder
    // leaves the flush exactly as before.
    void SetTexBinder(const GroundTexBinder* b) { texBinder_ = b ? *b : GroundTexBinder{}; }

    // WATER PIPELINE (wave-6 W6-WR). Build the animated water region meshes from
    // the bound floor (VIBE_FloorWater_PrepareRegions @0x5ba95c via
    // render::BuildWaterRegions) and keep them alongside this frame. Called once
    // after Bind() (or on floor reload). `loadTexture`/`ctx` is the present-
    // coupled water-texture loader (EF_WASS_06A_2T_W_AN0; null == headless white
    // default). Returns the number of water regions built (0 == no water).
    int BuildWater(render::WaterTextureLoadFn loadTexture = nullptr, void* ctx = nullptr);
    bool hasWater() const { return waterMeshCount_ > 0; }
    int  waterRegionCount() const { return waterMeshCount_; }
    // Per-frame water vertex animation (VIBE_Floor_AnimateWaterVertices @0x5be428).
    // `time` is the engine clock (dword_62EB38). Advances each region's wave grid
    // + texture member; safe to call every frame. `findGroupMember`/`ctx` inject
    // the texture-group member lookup (null == no texture advance, wave grid only).
    void AnimateWater(i32 time, render::FindGroupMemberFn findGroupMember = nullptr,
                      void* ctx = nullptr);
    // Stats from the last Render() water sub-pass (water-render wave-6).
    int waterPolysAppended() const { return waterStats_.appended; }
    int waterVertsBuilt() const { return waterStats_.vertsBuilt; }

private:
    const FloorGround* g_ = nullptr;
    render::TerrainFloor floor_{};
    std::vector<render::TerrainTile>          tiles_;
    std::vector<std::vector<render::Vertex>>  vbufs_;
    std::vector<std::vector<render::Polygon>> pbufs_;
    std::vector<render::DrawListEntry>        dl_;
    // (wave-5: the row-mirror lattice copies were removed — the floor is fed in
    //  its true row order now that the @0x5bf22c winding is reconstructed 1:1.)
    render::TileElevationSummary elev_{};   // 0x5bbdb0 per-tile min/max
    // The FULL flt_13FE540 image: 64 records x 24 floats (record 0 = the
    // corner-inset full tile; 1..63 = the random rotated sub-quads).
    float uvTable_[64 * 24] = {};
    std::vector<u8> subTex_;                    // byte_13DCE58 image (65536)
    std::vector<std::vector<float>> polyUvBufs_; // per-tile poly UV records (6/poly)
    std::vector<u8> lightBytes_;                 // Floor+0x1C per-cell light map
    u8  pendingLod_[64] = {};               // tile+97 debounce state
    int lodCounter_[64] = {};               // tile+88
    GroundTexBinder texBinder_{};           // wave-5 W5-TX (default: untextured)

    // ---- WATER state (wave-6 W6-WR) -----------------------------------------
    render::WaterRegions          water_{};        // BuildWaterRegions output (owns mesh storage)
    int                           waterMeshCount_ = 0;  // == water_.regionCount
    const void*                   waterTexture_ = nullptr; // WaterMesh+4 handle
    std::vector<render::Vertex>   waterVbuf_;       // water surface vertex buffer
    std::vector<render::Polygon>  waterPbuf_;       // water surface poly buffer
    render::WaterDrawStats        waterStats_{};    // last Render() water pass
};

} // namespace guild::play
