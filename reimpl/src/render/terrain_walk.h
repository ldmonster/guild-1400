#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"
#include "render/mesh.h"            // DrawList sink
#include "render/terrain_render.h"  // SelectTileMeshLod / TileSubdivCount / Floor LOD core
#include "render/tile_geometry.h"   // BuildTileVertex / ClampLightByte / TileLightParams

// =============================================================================
// guild::render — the COMPLETE terrain whole-walk. Faithful 1:1 reconstruction of:
//
//   0x5BF22C  VIBE_Floor_RenderTerrain   (the LARGEST fn in gilde.exe, 0x386D bytes,
//                                          283 basic blocks — the per-frame terrain
//                                          render driver)
//
// This is the assembled whole-walk that drives the per-tile sub-systems (already
// reconstructed in tile_visibility / tile_geometry / tile_lighting / water_anim /
// texture_cache / meshlist). The original is a single __usercall fn over a 7281-byte
// Floor record and an 8x8 grid of 100-byte tile records, reading ~80 file-scope
// render-state globals. Those globals are gathered into TerrainRenderState below;
// the Floor / Tile records are modelled byte-for-byte (every field carries its
// ORIGINAL +0xNN offset).
//
// THE FOUR PHASES (decompiled line ranges)
// ---------------------------------------------------------------------------
//   Phase 0  (setup, lines 497..578): gate (byte_649D70 && dword_13FCD1C); read the
//            Floor geometry fields; pick the light params (flt_13FD4F0..518/530..538,
//            either flat-lit defaults or the sun globals flt_64A074..08C); rotate the
//            four floor axis vectors (origin/U/V/H) through the view matrix
//            (dword_13FCD1C+396..436) into flt_1404250.., flt_13FFD40.., flt_13FD500..,
//            dword_13FD520...
//   Phase 1  (if Floor+7280 & 1 == "build geometry", lines 579..1845):
//     Pass A (lines 582..812): per-tile LOD byte (Tile+94), subdiv counts
//            (TileSubdivCount), build the tile vertex grid (BuildTileVertex), and when
//            UpdateTileVisibility reports a change, build the tile's quad poly array
//            (TextureCache_GetOrBuildTile per cell + the two-triangle split).
//     Pass B (lines 813..1736): the 2:1 LOD-seam STITCH — for each tile that borders a
//            lower-LOD neighbour (right/up/left/down edges) emit the extra stitch
//            triangles so the seam has no T-junction crack.
//     Pass C (lines 1737..1844): ComputeVertexClipFlags per tile, perspective-project
//            each clipped vertex (flt_13FCD0C/D10/D18 + flt_13FCAF8 scalars + the
//            byte_649DD8 fog-shade branch), then signed-area backface cull per poly.
//     Then clear Floor+7280 bit0.
//   Phase 2  (tail, present-coupled, lines 1847..1934): if a2, AnimateWaterVertices;
//            then per visible tile update the floor bbox bounds (flt_13FD168/13FCF3C),
//            bump the per-frame poly/vertex counters (dword_13FC574/4E0/54C), and
//            append every front-facing poly to the global PolyList (the
//            ((tex-dword_1406A84)>>7)+1 sort key + the per-texture mipmap build on
//            first use), then VIBE_Floor_TransformTileGeometry. The DDraw lock the
//            present tail performs is routed through the present shim.
//
// REUSE
// ---------------------------------------------------------------------------
//   * TileSubdivCount / SelectTileMeshLod  (terrain_render)
//   * BuildTileVertex / ClampLightByte / AppendTilePolysToDrawList (tile_geometry)
//   * the visibility/LOD pick + seam stitch math (tile_visibility) is driven through
//     the injected UpdateTileVisibility callback (the per-tile picks live in the Tile
//     record's +94/+95 LOD bytes the original wrote there)
//   * TileCache::GetOrBuildTile (texture_cache) — injected as a callback
//   * AnimateWaterTexture (water_anim) — driven by the caller's water meshes
//
// DEFERRED (data-coupled leaves the walk *calls*; see module report):
//   * VIBE_Render_ComputeVertexClipFlags (0x5AD614) — per-vertex frustum outcodes,
//     a separate scenegraph module; injected as a callback (default no-op).
//   * VIBE_Floor_TransformTileGeometry (0x5BE668) — the projected-vertex transform +
//     final draw-list emit; injected as a callback (the project/append core is
//     reproduced inline here, this hook is the engine's post-emit fixup).
//   * byte_13DCE58 (256-stride per-cell sub-texture id table) and dword_13DB398
//     (26-dword-stride per-type light table) are opaque data tables; the per-cell
//     texture sub-id and light-table pointer they feed are passed in.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// TerrainRenderState — the ~80 file-scope render-state globals the walk reads,
// gathered into one re-entrant record. Each field names its ORIGINAL global.
// (Phase-0 fills the *derived* fields from the Floor + view matrix; the engine
// stored them in these globals so the per-tile passes could read them back.)
// ---------------------------------------------------------------------------
struct TerrainRenderState {
    // ---- engine gates (Phase-0 entry guard) --------------------------------
    u8  engineOn      = 1;   // byte_649D70: software-raster path enabled (gate)
    u8  fogShade      = 0;   // byte_649DD8: Phase-C extra per-vertex fog-shade branch
    u8  copyAfterMip  = 0;   // byte_64A02C: copy surface pixels after mipmap build

    // ---- per-channel terrain light (Phase-0; flat-lit defaults or sun globals) --
    // Flat-lit (Floor+7280 & 2): ambient=0, scale=1, shadowBias=0.
    // Lit: ambient = flt_64A074/078/07C, scale = Floor+208/212/216,
    //      shadowBias = flt_64A084/088/08C.
    float ambient[3]   = {0,0,0};   // flt_13FD4F0 / flt_13FD4F4 / flt_13FD4F8
    float lightScale[3]= {1,1,1};   // flt_13FD510 / flt_13FD514 / flt_13FD518
    float shadowBias[3]= {0,0,0};   // flt_13FD530 / flt_13FD534 / flt_13FD538

    // ---- the four floor axis vectors rotated into view space (Phase-0) -----
    float originView[3] = {0,0,0};  // flt_1404250 / flt_1404254 / flt_1404258 (tile (0,0))
    float axisU[3]      = {0,0,0};  // flt_13FFD40 / flt_13FFD44 / flt_13FFD48 (per tile X)
    float axisV[3]      = {0,0,0};  // flt_13FD500 / flt_13FD504 / flt_13FD508 (per tile Y)
    float axisH[3]      = {0,0,0};  // dword_13FD520/524/528 (per height byte)

    // ---- projection scalars (Phase-C reproject; set by SetupViewTransform) --
    float projXScale = 1.0f;        // flt_13FCD0C  (screenX = D0C*x*(1/z) + D18)
    float projXOff   = 0.0f;        // flt_13FCD18
    float projYScale = 1.0f;        // flt_13FCAF8  (screenY = AF8*y*(1/z) + D10)
    float projYOff   = 0.0f;        // flt_13FCD10
    // fog-shade branch (byte_649DD8 != 0):
    float fogNear    = 0.0f;        // flt_13FC544  (z2 <= near -> shade 255)
    float fogStart   = 0.0f;        // flt_13FC5AC  (subtracted from sqrt(z2))
    float fogScale   = 0.0f;        // flt_13FC58C  (* the above)

    // ---- draw-list cursors / counters (Phase-2; the global PolyList) -------
    DrawList*  drawList = nullptr;  // dword_13FC570 cursor / dword_13FC584 base /
                                    // dword_13FC770 count / dword_13ECE80 capacity
    u32 frameStamp = 0;             // dword_649D58 (stamped into each used texture +84)
    u32 texBaseStride = 0x80;       // dword_1406A84 stride for ((tex-base)>>7)+1 key

    // ---- floor bbox accumulators (Phase-2) ---------------------------------
    float bboxMinX = 1e30f;         // flt_13FD168[0]: min over visible tiles' +80
    float bboxMaxY = -1e30f;        // flt_13FCF3C:    max over visible tiles' +84

    // ---- per-frame stat counters (Phase-2) ---------------------------------
    i32 vertsThisFrame = 0;         // dword_13FC574
    i32 vertsAlt       = 0;         // dword_13FC4E0
    i32 polysThisFrame = 0;         // dword_13FC54C
};

// ---------------------------------------------------------------------------
// Floor (terrain root) record — the fields the whole-walk reads, each with its
// ORIGINAL +0xNN offset (recovered from VIBE_Floor_RenderTerrain @0x5bf22c).
// The live struct is 7281+ bytes; only the modelled fields participate. The 8x8
// tile array begins at +0x0E0 (224), stride 100 (a row is 800).
// ---------------------------------------------------------------------------
struct TerrainTile;  // fwd

struct TerrainFloor {
    i32   size;        // +0x00 (v377) grid edge length (power of two)
    i32   tileSpan;    // +0x04 (v376) samples per tile edge before LOD subdivision
    i32   mask;        // +0x08 (v378) wrap mask == size-1 (torus)
    // +0x0C
    const u8* heights; // +0x10 (v381) size*size elevation bytes
    // (+0x14 v382 == the texture source the cache reads; modelled as texSrc below)
    const u8* texSrc;  // +0x14 (v382) per-cell texture source buffer
    // +0x18
    const u8* types;   // +0x1C (v380) per-cell type byte base (signed: <0 shadow/hole)
    // ... light-table mip levels at +0x24+ (dword_13DB398 driven; opaque) ...
    float lightSunScale[3]; // +0xD0 (+208/212/216) per-channel sun light scale
    // The 8x8 array of 100-byte tile records, based at +0x0E0 (224):
    TerrainTile* tiles; // +0xE0 logical base of the 64-entry tile grid (row stride 800)
    // edge-LOD neighbour table the stitch pass reads at +318+: modelled in Tile.edgeLod
    // mip-level texture-source pointers indexed *(Floor + 4*(lod>>1) + 36) — v379:
    const u8* mipTexSrc[4]; // (v379) per-LOD texture-source base (lod>>1 selects)
    u8    flatLit;     // +0x1C70 (+7280) bit1 = flat-lit, bit0 = build-geometry
    u8    minLodNibble;// +0x1C71 (+7281) low nibble => minimum LOD (1<<nibble)
    // water anim hookup (+6624 mesh ptr table, +7277 mesh count) is driven by caller
};

// ---------------------------------------------------------------------------
// Per-tile record — 100-byte stride, 8x8 grid at Floor+224 (row stride 800).
// Recovered from VIBE_Floor_RenderTerrain @0x5bf22c + VIBE_Floor_UpdateTileVisibility.
// ---------------------------------------------------------------------------
struct TerrainTile {
    i32     subdivCached; // +0x10 (+16) v179*v180 cached subdivision product
    void*   vertexBuf;    // +0x18 (+24) tile's 80-byte Vertex buffer base (v183/v465..)
    i32     polyCount;    // +0x20 (+32) emitted poly count (cleared on rebuild)
    void*   polyBuf;      // +0x28 (+40) tile's 40-byte poly build buffer base (v192..)
    void*   clipList;     // +0x30 (+48) tri-vertex-ptr scratch list (v483, stride 3)
    i32     vertCount;    // +0x38 (+56) emitted vertex count (cleared on rebuild)
    void*   drawData;     // +0x3C (+60) per-tile 24-byte UV/draw record base (v19/v379)
    // +0x48..0x5D bound/LOD bookkeeping (tile_visibility owns the writes):
    float   bboxMinX;     // +0x50 (+80) bbox-min x (Phase-2 bound accum)
    float   bboxMinY;     // +0x54 (+84) bbox-min y
    u8      lod;          // +0x5E (+94) current LOD byte (v372/v454)
    u8      prevLod;      // +0x5F (+95) previous-frame LOD byte
    u8      clipFlag;     // +0x62 (+98) cull/clip flags (low 6 bits index light table)
    // neighbour edge LODs the stitch pass reads (modelled from *(tile + 318/-100/+100..)):
    u8      edgeRightLod = 0; // *(v437 + ...+318): right neighbour LOD
    u8      edgeDownLod  = 0; // *(v438 + ...+318): down neighbour LOD
    u8      edgeUpLod    = 0; // *(v439 + ...+318): up neighbour LOD (the +318/+442 reads)
    u8      edgeLeftLod  = 0;
};

// ---------------------------------------------------------------------------
// Injected leaf hooks (the data-coupled subsystems the walk *calls*).
// ---------------------------------------------------------------------------
struct TerrainWalkHooks {
    // 0x5BEF08 — VIBE_Floor_UpdateTileVisibility: returns nonzero when any tile's LOD
    // or visibility changed this frame (drives the geometry rebuild). Defaults to a
    // hook returning 1 (always rebuild) so the walk is exercisable standalone.
    i32 (*updateVisibility)(TerrainFloor* floor) = nullptr;

    // 0x5BA1E8 — VIBE_TextureCache_GetOrBuildTile: bind/build the tile texture for a
    // (texSrc,size,u,v) region; returns the bound texture record id/ptr (stored into
    // each poly +20 and +60). Default returns 0 (untextured).
    u32 (*getOrBuildTile)(const u8* texSrc, i32 width, i32 u, i32 v, i32 lod,
                          void* cacheState) = nullptr;
    void* cacheState = nullptr;

    // 0x5AD614 — VIBE_Render_ComputeVertexClipFlags: per-tile frustum outcodes. The
    // walk reads each vertex +76 flag the original wrote (clip bit 0x10, on-screen
    // sign bit). Default no-op (treat every vertex as visible, +76 high bit set).
    void (*computeClipFlags)(u8 clipFlag, void* vbuf, void* pbuf, i32 vcount,
                             i32 pcount) = nullptr;

    // 0x5BE668 — VIBE_Floor_TransformTileGeometry: the engine's post-emit fixup the
    // tail calls per tile after the inline poly-append. Default no-op.
    void (*transformTileGeometry)(TerrainFloor* floor, TerrainTile* tile) = nullptr;

    // The 26-dword-stride per-type light table base (dword_13DB398). The append
    // writes &table[26 * (tile.clipFlag & 0x3F)] into each poly +12. Default null.
    void* lightTable = nullptr;
};

// gilde.exe 0x5BF22C — VIBE_Floor_RenderTerrain. Run the COMPLETE per-frame terrain
// walk over `floor` using the gathered `st` render-state and the injected `hooks`.
// `animateWater` mirrors the a2 argument (the per-frame water-vertex animate gate).
// Returns the number of polygons appended to the draw list this frame (0 when the
// engine gate is closed). The four phases run in order exactly as the original.
i32 RenderTerrain(TerrainFloor* floor, TerrainRenderState& st, const TerrainWalkHooks& hooks,
                  bool animateWater);

// ---- exposed sub-steps (so the unit test can drive each phase) -------------

// Phase-0 light-param select (lines 505..531). Fills st.ambient/lightScale/shadowBias
// from the flat-lit default (Floor+7280 & 2) or the sun globals. `sunScale`/`sunAmbient`/
// `sunBias` are the runtime sun globals (flt_64A074..08C + Floor+208..216).
void SetupTerrainLight(TerrainRenderState& st, bool flatLit, const float sunScale[3],
                       const float sunAmbient[3], const float sunBias[3]);

// Phase-0 axis-vector rotation (lines 532..578). Rotate a floor axis vector
// (origin/U/V/H) through the 3x3 view rotation (view+396..436) into view space,
// writing out[3]. `view` is the 4x4 row-major matrix base (dword_13FCD1C); the
// rotation columns are at +396/+400/+404 (x row), +412/+416/+420, +428/+432/+436.
void RotateFloorAxis(const float in[3], const float* viewMat3x3, float out[3]);

} // namespace guild::render
