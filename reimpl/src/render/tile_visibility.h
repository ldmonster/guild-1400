#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — TERRAIN TILE VISIBILITY + LOD. Faithful 1:1 reconstruction of:
//
//   0x5bef08  VIBE_Floor_UpdateTileVisibility  (per-tile bound + cull + LOD select
//                                               + neighbour LOD-seam stitch + dirty)
//   0x5ba438  VIBE_Floor_ComputeLodLevel       (distance-based, debounced LOD pick)
//
// The floor's tile array is an 8x8 grid of 100-byte tile records based at
// Floor+0x0E0 (224); each tile row is 800 bytes (recovered from
// VIBE_Floor_InvalidateTiles @0x5ba704 and the UpdateTileVisibility walk). Tile
// record fields used here (ORIGINAL byte offsets):
//   +0x00/04/08  bbox-centre x/y/z (averaged from the 8 corner vertices)
//   +0x48 (72)   per-tile LOD distance bias term
//   +0x4C (76)   bounding radius (|centre|)
//   +0x50 (80)   bbox-min x  (+= 16.0 padding in the bound pass)
//   +0x54 (84)   bbox-min y  (+= 16.0)
//   +0x58 (88)   LOD-change debounce counter
//   +0x5E (94)   current LOD byte
//   +0x5F (95)   previous-frame LOD byte
//   +0x61 (97)   pending LOD byte
//   +0x62 (98)   cull-flags byte (0x40 == fully culled)
// Floor record fields: +0xA0 (160) world tile scale; +7268/+7272 LOD distance
// thresholds; +7280 flags; +7281 minimum-LOD nibble.
//
// SCOPE: the corner-vertex build (VIBE_Floor_ComputeTileVertices @0x5bdec4) and the
// frustum classify (VIBE_Render_ClassifyBoundingBoxPlanes @0x5ad1f4, in
// render/scenegraph) are separate modules — the self-contained pieces here are the
// 8-corner bound (centre+radius), the distance LOD pick, and the seam stitch.
// =============================================================================
namespace guild::render {

// gilde.exe 0x5bef08 (bound pass) — average the 8 corner vertices of a tile into a
// centre point and a bounding radius. `corners` is 8 vertices, each an 80-byte
// (20-float) record laid out x,y,z,... at floats [0],[1],[2] (the engine strides
// `+= 20` floats from corner 0 to corner 8). The centre is the sum * 0.125
// (flt_628B40); the radius is |centre| = sqrt(cx^2+cy^2+cz^2). Writes outCentre[3]
// and returns the radius (stored at tile+76 by the original).
float ComputeTileCenterRadius(const float* corners, float outCentre[3]);

// flt_628B40 == 0.125 (1/8 average weight), flt_628B44 == 16.0 (bbox-min padding).
constexpr float kTileAvgWeight = 0.125f;   // flt_628B40
constexpr float kTileBboxPad   = 16.0f;    // flt_628B44

// gilde.exe 0x5ba438 — VIBE_Floor_ComputeLodLevel. Distance-based LOD pick with an
// 8-frame debounce. Inputs (all recovered from the decompile):
//   floorFlags  : Floor+7280  (if (flags & 0x1C) -> forced LOD = (8*flags)>>5)
//   hasEntries  : Floor+36 != 0 (0 -> LOD 0 / not built)
//   thrFar      : Floor+7272   (far LOD-1 threshold)         flt
//   thrNear     : Floor+7268   (near LOD-2 threshold)        flt
//   tileScale   : Floor+160    (world tile scale)            flt
//   tileBias    : tile+72      (per-tile distance bias)      flt
//   radius      : tile+76      (bounding radius)             flt
//   areaAvg     : a3 (sum of visible radii) / a4 (visible count) — the frame's
//                 mean tile radius; the original passes SLODWORD(sumRadius) and the
//                 count, computing v8 = (40.0 - count) * 0.4 (flt_628758=40,
//                 flt_62875C=0.4) as a crowd-scaling term.
//   prevLod     : tile+95   pendingLod : tile+97   counter : tile+88 (debounce)
// Picks 4 (closest), 2, or 1 from the thresholds, then debounces vs prevLod over 8
// frames. Returns the chosen LOD byte and updates *counter/*pendingLod.
//   metric = radius / tileScale;
//   if (thrFar  + tileBias + crowd >= metric)
//       lod = (thrNear + tileBias + crowd >= metric) ? 1 : 2;
//   else lod = 4;
u8 ComputeLodLevel(u32 floorFlags, bool hasEntries, float thrFar, float thrNear,
                   float tileScale, float tileBias, float radius,
                   int visibleCount, u8 prevLod, u8* pendingLod, int* counter);

// gilde.exe 0x5bef08 (stitch pass) — resolve a 2:1 LOD seam between a tile and its
// already-resolved left/up neighbour, then clamp to the floor's minimum LOD. The
// original rule (decompiled): when a tile at LOD 4 borders a LOD-1 neighbour (or
// vice-versa) the higher-detail tile is forced to LOD 2 to transition the seam:
//   if (myLod==4 && neighbourLod==1) || (myLod==1 && neighbourLod==4)  -> myLod = 2
// applied for the left neighbour (when col>0) and the up neighbour (when row>0).
// Then myLod = max(myLod, minLod) where minLod = 1 << (floorMinNibble & 0xF)
// (Floor+7281). Returns the resolved LOD. `leftLod`/`upLod` are the neighbours'
// already-resolved LODs (pass 0 to disable an edge, i.e. col==0 / row==0).
u8 StitchTileLod(u8 myLod, u8 leftLod, u8 upLod, u8 floorMinNibble);

} // namespace guild::render
