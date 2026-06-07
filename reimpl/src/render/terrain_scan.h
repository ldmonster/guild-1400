#pragma once
#include "guild/common/types.h"

// guild::render — terrain heightmap row-range scan, the inner primitive of the
// terrain triangle height-range rasterizer (VIBE_Terrain_ScanLineHeightRange /
// VIBE_Terrain_ScanSegmentHeightRange, which walk a projected triangle edge by
// edge and call ScanRowHeightRange for each scanline). Faithful 1:1 of:
//
//   0x426da0  VIBE_Terrain_ScanRowHeightRange
//
// The two edge-walking callers (ScanLineHeightRange @0x426e2c,
// ScanSegmentHeightRange @0x427370) are DEFERRED: their Hex-Rays output relies
// on register-passed VIBE_Coord_ConvertX results whose stack/register ordering
// is ambiguous in the decompile (see the module report). The row primitive they
// share is self-contained and golden-vector verifiable, so it lives here.
namespace guild::render {

// ---------------------------------------------------------------------------
// Terrain height grid descriptor as ScanRowHeightRange indexes it. The function
// reads two int-sized fields off its `a1` base:
//   a1[0]  (offset +0x00) = width   — the grid edge / row stride in bytes
//   a1[4]  (offset +0x10) = heights — base of the width*height elevation bytes
// (Other terrain-root fields exist around these but are untouched by the row
// scan.) Cell (x,row) elevation = heights[row*width + x].
// ---------------------------------------------------------------------------
struct TerrainHeightGrid {
    i32       width;    // +0x00  a1[0]  row stride (grid edge)
    i32       _pad04;   // +0x04
    i32       _pad08;   // +0x08
    i32       _pad0c;   // +0x0C
    const u8* heights;  // +0x10  a1[4]  width*height elevation bytes (row-major)
};

// gilde.exe 0x426da0 — VIBE_Terrain_ScanRowHeightRange
//   (__userpurge (grid@eax, lowOut@edx, xStart@ecx, highOut@ebx, xEnd, row)).
// Scans terrain row `row` over the inclusive column span [xStart, xEnd] (the
// caller may give them swapped; they are normalised here) and folds the cell
// elevation bytes into a running range:
//   *lowOut  = min(*lowOut,  cell)   — the running minimum elevation
//   *highOut = max(*highOut, cell)   — the running maximum elevation
// Callers seed *lowOut = 255 and *highOut = 0 before the first scanline.
//
// Returns 1 (no cells scanned / clipped out) when:
//   - row is outside [0, width)            (the grid is square: height == width)
//   - the normalised span lies wholly off the row (high end < 0, or low end >=
//     width while high end is also clamped past the last column)
// Otherwise the span is clamped to [0, width-1], every cell is folded, and 0 is
// returned (range was updated). The accumulators are NOT touched on the early-out
// paths, so the caller's seed values survive a fully-clipped row.
int ScanRowHeightRange(const TerrainHeightGrid* grid, int* lowOut, int xStart,
                       int* highOut, int xEnd, int row);

} // namespace guild::render
