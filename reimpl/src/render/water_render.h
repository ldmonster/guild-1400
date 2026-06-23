#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"   // Vertex / Polygon / DrawListEntry
#include "render/mesh.h"             // DrawList (the draw-list sink)
#include "render/floorwater.h"       // WaterStripSpan / WaterPoly / WaterRegions
#include "render/water_vertices.h"   // WaterMesh (the 344-byte animated record)

#include <vector>

// =============================================================================
// guild::render — THE WATER RENDER ARM of the city terrain pass. Faithful 1:1
// reconstruction of:
//
//   0x5BE668  VIBE_Floor_TransformTileGeometry
//             __usercall (floor@eax, tile@edx) -> int
//
// This is the function VIBE_Floor_RenderTerrain @0x5bf22c calls PER TILE in its
// Phase-2 tail (the call site is 0x5c17fd, inside the per-tile draw-list append
// loop). Despite the generic IDA name it is specifically the WATER sub-pass: it
// builds the animated water-region surface vertices for the tile, projects them,
// and appends the water polys to the global draw list keyed with the water
// texture. (RenderTerrain itself only ANIMATES water — VIBE_Floor_AnimateWater
// Vertices @0x5be428 at 0x5c2a6a — the DRAW lives here.)
//
// The function does FOUR things (decompile-traced; addresses in the .cpp):
//
//   1. SHADE PREP (0x5be6a5..0x5be73a): normalise the floor's height axis in view
//      space (dword_13FD520/524/528 == st.axisH), and compute
//        waterShade = flt_628B2C - |axisH_norm . sunDir|   (flt_628B2C == 0.5,
//        sunDir == flt_5CA2B0/B4/B8 == (0,0,1)). This scales the per-region
//        WaterMesh+20 amplitude (v15 = mesh[20] * waterShade).
//
//   2. WATER VERTEX BUILD (the span loop, 0x5be74d..0x5bea49): walk the tile's
//      20-byte WaterStripSpan list (tile+52). For each span:
//        region = span.marker (the FindRegionOffset-stamped region id);
//        mesh   = floor[1656] (Floor+0x19E0) + 344*region;
//        base   = originView + row*axisV_view + col0*axisU_view
//                 (row == span+4, col0 == span+8; flt_1404250.., flt_13FFD40..,
//                  flt_13FD500..).
//      Then per column c in [span.lo, span.hi]:
//        wh     = waterHeights[ col0_base + (c & mask) ]   (Floor+0x18, signed
//                 i16 byte); the waterHeight*axisH_view displacement is
//                 (dword_13FD520/524/528) * wh.
//        wave   = mesh.waveOut[ (row&3)*4 + (c&3) ].xyz    (mesh+0x38 grid; the
//                 16-vec4 AnimateWaterVertices output).
//        amp0   = mesh[20]*waterShade + (i16)mesh[8]       (a per-region base
//                 amplitude; v18). The vertex's depth/height adds wave.w + amp0.
//        vertex = base + wh*axisH_view + wave.xyz; height term += wave.w + amp0.
//      The vertex is written into the tile vertex buffer (tile+28), its per-vertex
//      colour bytes (+64/65/66/67) lit from the terrain type byte (types[] high
//      bit = shadow branch; flt_628B30 == 2.0 scales the type index) exactly as
//      the ground pass lights its tiles.
//
//   3. PROJECT (0x5bea4f..0x5bee30): ComputeVertexClipFlags @0x5ad614 then the
//      per-vertex perspective reproject (1/z, the flt_13FCD0C/D18/AF8/D10 scalars
//      + the byte_649DD8 fog-shade branch) — identical math to the ground Phase-C.
//
//   4. APPEND (0x5bec1d..0x5bef03): for each visible water poly (poly+36 high bit),
//      resolve its WaterMesh (floor[1656] + 344*poly.regionId), read the water
//      texture handle mesh[1] (mesh+4), append a draw-list entry with sort key
//      ((tex-texBase)>>7)+1 and stamp the per-frame texture stamp (dword_649D58).
//
// REUSE / HEADLESS
// ---------------------------------------------------------------------------
// This module owns the WATER half only; it reuses the SAME view-space axes /
// projection scalars / light params the ground GroundFrame already gathers
// (render::TerrainRenderState), and the SAME render::Vertex/Polygon/DrawList
// records the ground draw list flushes. The present-coupled texture handle is the
// WaterMesh+4 the builder stored (0 in headless builds -> the white-default leaf,
// exactly the engine's `if (!tex) sortKey = 0` branch at 0x5bef03).
// =============================================================================
namespace guild::render {

struct TerrainRenderState;   // terrain_walk.h (the gathered view globals)

// ---------------------------------------------------------------------------
// WaterDrawInput — the live Floor fields the water arm reads, gathered (each
// field cites its Floor offset). The geometry (spans/polys/waterMeshes) is the
// render::WaterRegions the builder produced; the per-cell water heights + terrain
// types come from the parsed floor block.
// ---------------------------------------------------------------------------
struct WaterDrawInput {
    i32        size = 0;              // Floor+0   N (grid edge)
    i32        mask = 0;             // Floor+0x0C  N-1 (per-axis cell mask)
    const u8*  waterHeights = nullptr; // Floor+0x18 (a1[6]) per-cell water height
    const u8*  types = nullptr;       // Floor+0x1C (a1[7]) per-cell type/shadow byte
    // Floor+0x19E0 (a1[1656]) — the RAW 86-float (344-byte) WaterMesh records the
    // builder produced (render::WaterRegions::waterMeshes). Read as float[]: the
    // waveOut grid is float[14..77] (+0x38), texPtr float[0], member float[1].
    const float* meshes = nullptr;
    u32        meshCount = 0;         // Floor+0x1C6D / regionCount
    // The compacted span/poly geometry (render::WaterRegions). Spans are walked
    // per region row; each poly resolves its region through poly.regionId.
    const WaterStripSpan* spans = nullptr;
    i32        spanCount = 0;
    const WaterPoly* polys = nullptr;
    i32        polyCount = 0;
};

// ---------------------------------------------------------------------------
// The water surface texture handle the builder stored into every WaterMesh+0/+4
// ("EF_WASS_06A_2T_W_AN0"). 0 == headless (the engine's null-texture append
// branch at 0x5bef03; the flush then draws the white default). When non-null the
// arm carries it as the draw-list sort key + the bound texture record.
// ---------------------------------------------------------------------------
struct WaterDrawStats {
    int  spansWalked  = 0;   // WaterStripSpan records visited
    int  vertsBuilt   = 0;   // water surface vertices emitted (one per span point)
    int  polysBuilt   = 0;   // water polys assembled (2 tris per WaterPoly cell)
    i32  appended     = 0;   // draw-list entries appended (visible, front-facing)
    int  texturedPolys= 0;   // ... of which carried a non-null water texture
    int  linkedPolys  = 0;   // polys whose 4 corners ALL resolved through the
                             // FindRegionOffset point linkage (pTL/pBR/pBL/pTR>=0)
    float waterShade  = 0;   // the 0.5 - |axisH.sun| amplitude scale (introspection)
    // The animated texture-scroll members the APPEND loop copies out of the
    // region WaterMesh into each water poly (result+28/+32 = mesh[82]/mesh[83],
    // @0x5beee9/0x5beef5). float[82]/[83] are texAccumA/texAccumB — the scrolling
    // members AnimateWaterVertices advances each frame; the textured water span
    // adds them to its UVs to scroll the EF_WASS_06A_2T_W_AN0 ripple. Reported
    // here for the LAST appended textured poly (introspection / golden checks).
    float scrollU     = 0;   // mesh[82] (+0x148) of the last appended poly
    float scrollV     = 0;   // mesh[83] (+0x14C) of the last appended poly
};

// gilde.exe 0x5BE668 — VIBE_Floor_TransformTileGeometry (the water render arm).
//
// Build + project + append the animated water region surface for the whole floor
// into `vbuf`/`pbuf` (the tile vertex/poly buffers the engine reused per tile;
// here one shared buffer for the whole water surface, the same draw list the
// ground flush consumes). `st` carries the view-space axes / projection scalars /
// light params Phase-0 already gathered (RenderTerrain Phase-0). `dl` is the
// draw list the ground pass flushes; water polys are appended after the ground
// tiles, before the flush (the engine's per-tile tail position). `waterTexture`
// is the WaterMesh+4 handle (0 == headless white default). Returns stats.
//
// The geometry math is the exact 0x5be668 decompile (base = originView +
// row*axisV + col*axisU; per-column waveOut displacement + waterHeight*axisH;
// the per-vertex shadow-branch lighting; the signed-area-cull + reproject; the
// ((tex-texBase)>>7)+1 sort key). `vbuf`/`pbuf` MUST be large enough for the
// whole water surface (the caller sizes them from spanCount/polyCount).
// `waterTexture` is the per-region WaterMesh+4 handle (0 == headless white
// default). `texBase`/`texStride` reproduce the engine's sort-key arithmetic
// `((tex - dword_1406A84) >> 7) + 1` (texBase == dword_1406A84, the texture
// record array base; texStride == 128, the 128-byte record stride): when
// `texBase` is supplied AND `waterTexture` lies in the record array the key is
// the exact record index + 1; when `texBase` is null the key is 1 for any
// non-null handle (the opaque-handle headless/bridge path), 0 for a null handle
// (the engine's `if(!tex)` white-default branch @0x5bef03). `frameStamp` is the
// per-frame texture stamp (dword_649D58) the engine writes into the bound
// texture +84 — carried as a stat (no live texture record to stamp here).
WaterDrawStats RenderWaterSurface(const WaterDrawInput& in, const TerrainRenderState& st,
                                  Vertex* vbuf, i32 vbufCap, Polygon* pbuf, i32 pbufCap,
                                  DrawList& dl, const void* waterTexture,
                                  const void* texBase = nullptr, u32 texStride = 128);

// ---------------------------------------------------------------------------
// Exposed sub-steps (golden-vector testable in isolation).
// ---------------------------------------------------------------------------

// gilde.exe 0x5be73a — waterShade = flt_628B2C - |axisHview_norm . (0,0,1)|.
// `axisHView` is st.axisH (the floor height axis rotated into view space). The
// axis is L2-normalised first (VIBE_Math_VectorNormalize @0x5cb148). flt_628B2C
// == 0.5; the sun direction flt_5CA2B0/B4/B8 == (0,0,1).
float ComputeWaterShade(const float axisHView[3]);

// gilde.exe 0x5be8b3..0x5be909 — the per-water-vertex world position: base +
// waterHeight*axisHView + waveOut.xyz (and the height/depth term carries
// waveOut.w + ampBase). `base` is the span base (originView + row*axisV +
// col*axisU); `axisHView` is st.axisH (NOT normalised — the raw view axis, the
// dword_13FD520 read); `wave` is the 4-float waveOut vec at the cell; `wh` is the
// signed-i16 water-height byte; `ampBase` is mesh[20]*waterShade + (i16)mesh[8].
// Writes the 3 world components to `outPos`.
void BuildWaterVertexPos(const float base[3], const float axisHView[3],
                         const float wave[4], i16 wh, float ampBase, float outPos[3]);

} // namespace guild::render
