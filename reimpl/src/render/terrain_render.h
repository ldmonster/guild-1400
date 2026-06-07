#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — TERRAIN LOD selection + the Floor/Tile record layouts that the
// big terrain render walk drives. Faithful 1:1 reconstruction of the LOD core of:
//
//   0x5BF22C  VIBE_Floor_RenderTerrain         (the LARGEST fn in gilde.exe,
//                                               0x386D bytes — the LOD/tile walk)
//   0x5C5610  VIBE_Heightmap_BuildTerrainMesh  (the grid-scale -> LOD clamp)
//
// SCOPE / DEFERRED INNER DETAIL
// ---------------------------------------------------------------------------
// The full body of VIBE_Floor_RenderTerrain (1937 decompiled lines, 283 basic
// blocks) is a thicket of: a 7281-byte Floor struct, ~80 file-scope render-state
// globals (flt_13FD4F0.., flt_13FFD40.., flt_1404250.., the view matrix at
// dword_13FCD1C+396..436), the texture-tile LRU cache
// (VIBE_TextureCache_GetOrBuildTile), the water animation
// (VIBE_Floor_AnimateWaterVertices) and the present-coupled tail
// (VIBE_Floor_TransformTileGeometry). Per AGENT_GUIDE that whole-walk body is
// LISTED as deferred (see the module report). What is reconstructed here is the
// self-contained, deterministic LOD MATH that the walk is built around:
//
//   * SelectTileMeshLod      — the size-derived 1..4 LOD clamp from BuildTerrainMesh
//   * TileSubdivCount        — the per-tile subdivision count (v179/v180) the walk
//                              uses to step the tile's vertex/poly grid, including
//                              the row/col==7 edge stitch (the `- 4/lod` term)
//   * Floor / TileRecord     — the recovered byte-for-byte record layouts
//
// These are the numbers that decide HOW MANY vertices/polys each tile emits; the
// tile_geometry module consumes them to build the actual geometry.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Floor (terrain root) record. Recovered from VIBE_Floor_RenderTerrain @0x5bf22c
// (the *(result+OFF) accesses at the top of the walk) and VIBE_Floor_BuildTilePolys
// @0x5bc45c / VIBE_Floor_TransformTileGeometry @0x5be668. The live struct is
// 7281+ bytes; only the fields the LOD/geometry core reads are modelled, each with
// its ORIGINAL byte offset.
// ---------------------------------------------------------------------------
struct Floor {
    i32   size;        // +0x00  (v377) grid edge length (power of two)
    i32   tileSpan;    // +0x04  (v376) samples per tile edge before LOD subdivision
    i32   mask;        // +0x08  (v378) wrap mask == size-1 (torus)
    // +0x0C..+0x0F
    u8*   heights;     // +0x10  (v381) size*size elevation bytes (height samples)
    u8*   textures;    // +0x14  (v382) per-cell texture id base
    u8*   types;       // +0x1C  (v380) per-cell terrain-type byte base (signed: <0 hole)
    float origin[3];   // +0x90  (+144/148/152) world origin of tile (0,0)
    float axisU[3];    // +0xA0  (+160/164/168) world step vector per tile X
    float axisV[3];    // +0xB0  (+176/180/184) world step vector per tile Y
    float axisH[3];    // +0xC0  (+192/196/200) world step vector per height byte
    float lightScale[3];//+0xD0  (+208/212/216) per-channel sun light scale
    // +0xE0 (+224) .. tile records (stride is engine-specific, see TileRecord)
    u32   flags;       // +0x1C70 (+7280) bit0 = build geometry, bit1 = flat-lit
};

// ---------------------------------------------------------------------------
// Per-tile record. The walk steps the tile array from Floor+224 (v430). Recovered
// from VIBE_Floor_RenderTerrain @0x5bf22c (the v428+OFF accesses inside the walk):
//   +16  cached subdiv product (v179*v180), rebuilt when LOD or visibility changes
//   +24  pointer to the tile's 80-byte Vertex buffer (v183 cursor)
//   +32  poly/draw bookkeeping (cleared on rebuild)
//   +40  pointer to the tile's 80-byte-stride poly build buffer (v192 cursor)
//   +94  current LOD byte (v372); v372>>1 selects the level table index (v375)
//   +95  previous-frame LOD byte (compared to +94 to detect a LOD change)
// ---------------------------------------------------------------------------
struct TileRecord {
    i32   subdivCached; // +0x10  (v179*v180) cached subdivision product
    float worldX;       // +0x18  (+24) tile world base x  (unused by the LOD core)
    void* vertexBuf;    // +0x18  alias documented; the engine reused +24 as the cursor
    void* polyBuf;      // +0x28  (+40) per-tile poly build buffer cursor (v192)
    u8    lod;          // +0x5E  (+94) current LOD byte
    u8    prevLod;      // +0x5F  (+95) previous-frame LOD byte
};

// gilde.exe 0x5c5610 — VIBE_Heightmap_BuildTerrainMesh (the LOD clamp, lines
// computing v51/v53/v54/v181). After deriving the world<->tile X scale
//   scaleX = (maxX - minX) / (size + flt_628BA4)        flt_628BA4 = -1.75
// the mesh builder picks a global mesh LOD:
//   v51 = flt_628BAC / scaleX                            flt_628BAC = 50.0
//   v53 = v51 + dbl_628BB4                               dbl_628BB4 = 0.5
//   t   = (int)v53                                       (ConvertX: trunc toward 0)
//   v54 = (t - (signfix)) >> 2                           (arithmetic /4 of t)
//   LOD = clamp(v54, 1, 4)
// Returns the clamped LOD in [1,4]. `scaleX` is the world-units-per-tile-X step.
i32 SelectTileMeshLod(float scaleX);

// Convenience: derive scaleX then the LOD straight from the world AABB + size,
// exactly as BuildTerrainMesh does (scaleX = (maxX-minX)/(size-1.75)).
i32 SelectTileMeshLodFromBounds(float minX, float maxX, i32 size);

// gilde.exe 0x5bf22c — VIBE_Floor_RenderTerrain (the per-tile subdivision count,
// lines 609..618). Given a tile's LOD byte (`lod` == TileRecord.lod, v372) and the
// floor's tileSpan (v376), the walk computes how many vertex rows/cols the tile
// emits this frame:
//   base = tileSpan / lod + 1                            (v179 / v180)
// and, for the LAST tile in a row (col index == 7) or column (row index == 7), it
// stitches the seam by subtracting 4/lod:
//   if (colIndex == 7) cols = base - 4/lod;
//   if (rowIndex == 7) rows = base - 4/lod;
// `lod` must be nonzero (the walk guards `if (v372)`); returns `base` when lod==0
// is passed defensively is undefined in the original — we guard and return 0.
// Returns the subdivision count along one axis for the tile at (colIndex,rowIndex).
i32 TileSubdivCount(i32 tileSpan, u8 lod, i32 colIndex, i32 rowIndex, bool isCol);

} // namespace guild::render
