#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — TERRAIN MESH BUILD: heightmap tile tessellation + the dirty /
// uniform / slope flag passes that precede it. Faithful 1:1 reconstruction of:
//
//   0x5bdec4  VIBE_Floor_ComputeTileVertices  (one tile -> 8 world-space corner
//                                              vertices: a 4-corner XZ quad lifted
//                                              by two height samples)
//   0x5ba704  VIBE_Floor_InvalidateTiles      (mark every tile dirty)
//   0x5bbc74  VIBE_Floor_MarkUniformTiles     (per-LOD uniform-region flag bit)
//   0x5bbdb0  VIBE_Floor_ComputeSlopeFlags    (slope/uniform flags + per-tile
//                                              min/max elevation summary)
//
// These functions drive the same 7281-byte Floor record and 8x8 / 100-byte tile
// grid as render/tile_visibility.{h,cpp} and render/tile_geometry.{h,cpp}; the
// vertex math is the geometry the (deferred) terrain render walk emits. The
// runtime view/axis state the original keeps in file-scope globals (flt_13FFD40..,
// flt_1404250.., flt_13FD500.., flt_13FD520..) is gathered here into an explicit
// TileBuildParams record so the build is re-entrant and golden-vector testable;
// each field names the global it mirrors.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Per-frame tile-build transform state (the view-rotated tile axes + origin the
// terrain walk recomputes each frame). Mirrors the file-scope globals that
// VIBE_Floor_ComputeTileVertices reads:
//   axisU      : flt_13FFD40/44/48  world step per tile COLUMN sample
//   axisRow    : flt_13FD500/04/08  world step per tile ROW    sample
//   axisHeight : flt_13FD520/24/28  world step per height byte
//   origin     : flt_1404250/54/58  world position of sample (0,0)
// ---------------------------------------------------------------------------
struct TileBuildParams {
    float axisU[3];       // flt_13FFD40/44/48   per-column step
    float axisRow[3];     // flt_13FD500/04/08   per-row step
    float axisHeight[3];  // flt_13FD520/24/28   per-height-byte step
    float origin[3];      // flt_1404250/54/58   world origin of sample (0,0)
};

// gilde.exe 0x5bdec4 — VIBE_Floor_ComputeTileVertices
//   (__usercall (floor@eax, tileRec@edx, colIndex@ecx, rowIndex@ebx) -> float*).
// Builds the 8 corner vertices of one terrain tile. The tile spans `tileSpan`
// samples in each axis (Floor+4). The 4 XZ corners are
//   col*tileSpan, (col+1)*tileSpan  by  row*tileSpan, (row+1)*tileSpan
// each mapped to world space by origin + col*axisU + row*axisRow, then lifted by
// TWO height samples:
//   h1 = tileRec[+92]                               (the tile's stored corner height)
//   h2 = floor[100*col + 800*row + 317]             (a second grid height byte)
// producing 8 vertices written as two rows of 4 (corners A,B,C,D for h1 then h2):
//   A=(col,row)  B=(col+1,row)  C=(col+1,row+1)  D=(col,row+1).
// `out` receives 8 vertices * 3 floats = 24 floats (x,y,z per vertex), matching
// the engine's 80-byte Vertex slots flt_1404E74.. (only x/y/z are written here).
// `tileRec` is the tile's 100-byte record (height byte at +92); `floorHeights` is
// the floor base the second sample is read from (byte at 100*col+800*row+317).
void ComputeTileVertices(const TileBuildParams& p, i32 tileSpan, u8 cornerHeight,
                         u8 gridHeight, i32 colIndex, i32 rowIndex, float out[24]);

// ---------------------------------------------------------------------------
// Floor (terrain root) fields the dirty/uniform/slope passes touch. ORIGINAL byte
// offsets; only the fields these functions read are modelled (the live record is
// 7281+ bytes — the same Floor the tile_visibility / terrain_render modules use).
//   +0x00 size       grid edge (number of height samples per axis)
//   +0x0C mask       wrap mask (== size-1, the grid is a power-of-two torus)
//   +0x10 heights    size*size elevation bytes (row-major, stride=size)
//   +0x18 overlay    optional second height/overlay byte grid (0 == none)
//   +0x24 flagBase   per-cell flag byte base the uniform/slope passes write
//   +0x1C70 flags    +7280: bit0 = geometry dirty
// The tile grid (8x8 of 100-byte records, 800/row) is based at Floor+224 and is
// modelled by the explicit FloorTileView below for the dirty + slope summary pass.
// ---------------------------------------------------------------------------
struct FloorGrid {
    i32       size;       // +0x00  grid edge
    i32       mask;       // +0x0C  wrap mask (size-1)
    const u8* heights;    // +0x10  size*size height bytes
    const u8* overlay;    // +0x18  optional overlay height bytes (null == absent)
    u8*       flagBase;   // +0x24  per-cell flag byte base (uniform/slope writes)
    u32       flags;      // +0x1C70 (+7280) dirty/state flags
};

// gilde.exe 0x5ba704 — VIBE_Floor_InvalidateTiles. Sets Floor flag bit0 (geometry
// dirty) and writes 0xFF to the LOD-state byte (tile record +219, i.e. tile+119
// within the 100-byte record starting at +100) of all 64 tiles. `tiles` points at
// the 8x8 grid base (Floor+224); `tileLodByteOut[i]` receives 0xFF for i in [0,64).
// We model the side effect: clears (sets dirty) the per-tile LOD cache. Returns the
// new Floor flags value (bit0 set).
u32 InvalidateTiles(u32 floorFlags, u8* tileLodStates, u32* outFlags);

// gilde.exe 0x5bbc74 — VIBE_Floor_MarkUniformTiles. For each of three subdivision
// spans {1,2,4} (the engine's static byte table at 0x5b8ce3) it walks the grid in
// span-sized blocks; a block whose terrain-type bytes are all identical
// (VIBE_Floor_TileIsUniform) gets bit 0x40 set in its per-cell flag byte, else the
// bit is cleared. The three spans write into three separate flag planes (flagBase
// pointers `flagPlanes[0..2]`, the engine's *(a1+36 + 4*k)). Sets Floor dirty bit0.
// `types` is the size*size terrain-type byte grid; `size` is the grid edge; `mask`
// the wrap mask. Returns the (dirty-flagged) Floor flags.
u32 MarkUniformTiles(const u8* types, i32 size, i32 mask, u8* flagPlanes[3],
                     u32 floorFlags, u32* outFlags);

// ---------------------------------------------------------------------------
// View of the per-tile min/max-elevation summary the slope pass writes (the third
// loop of VIBE_Floor_ComputeSlopeFlags). The original writes into the 8x8 tile
// grid; we expose a flat 64-entry summary so the pass is testable in isolation.
//   minHeight[t] = tile+316 (lowest elevation byte over the tile's sample block)
//   maxHeight[t] = tile+317 (highest)        t = row*8 + col
// ---------------------------------------------------------------------------
struct TileElevationSummary {
    u8 minHeight[64];   // tile+316
    u8 maxHeight[64];   // tile+317
};

// gilde.exe 0x5bbdb0 — VIBE_Floor_ComputeSlopeFlags (per-tile min/max elevation).
// Reconstructs the SELF-CONTAINED third pass: for each of the 64 tiles (8x8) it
// scans the tile's (tileSpan+1)^2 sample block and records the min/max elevation
// byte, considering both the primary height grid and (when present) the overlay
// grid. Coordinates wrap with `mask`. The two slope-flag passes that precede it and
// the VIBE_FloorWater_PrepareRegions call between them are render/water-data coupled
// and DEFERRED (see floorwater.h). `heights`/`overlay` are the grids (overlay may be
// null); `size`/`mask` the grid edge/wrap; `tileSpan` the samples per tile axis.
void SummarizeTileElevations(const u8* heights, const u8* overlay, i32 size,
                             i32 mask, i32 tileSpan, TileElevationSummary* out);

} // namespace guild::render
