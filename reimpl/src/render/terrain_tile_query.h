#pragma once
#include "guild/common/types.h"

// guild::render — terrain tile attribute / walkability queries on the heightmap
// tile grid. Faithful 1:1 of:
//
//   0x5c3418  VIBE_Heightmap_LookupTileAttribute   (bucketed attribute lookup)
//   0x5c6478  VIBE_Heightmap_FindNearestWalkableTile (diamond spiral search)
//
// These walk the heightmap's per-cell records. The original is 32-bit and stores
// list-head/entry *pointers as 32-bit ints* inside its grid array; we model the
// same layout with proper pointers so the reconstruction is portable to 64-bit
// while preserving the exact field offsets and traversal semantics.
namespace guild::render {

// ---------------------------------------------------------------------------
// One attribute run-list entry (20-byte record in the original, chained by
// advancing the entry pointer by 20 bytes — the records are contiguous).
//   +0x00 (link/unused here)   +0x04 row   +0x08 xMin   +0x0C xMax   +0x10 attr
// `row < 0` terminates the list.
// ---------------------------------------------------------------------------
struct TileAttrEntry {
    i32 _link;      // +0x00
    i32 row;        // +0x04  tile Y of this run (< 0 terminates)
    i32 xMin;       // +0x08  inclusive run start column
    i32 xMax;       // +0x0C  inclusive run end column
    u8  attribute;  // +0x10  attribute byte returned on a hit
};

// ---------------------------------------------------------------------------
// Attribute-bucket grid as LookupTileAttribute indexes it (base = a1, int*).
//   a1[0] (+0x00) = size       — grid edge (x and y must be in (0,size))
//   a1[1] (+0x04) = bucketSize — tiles per spatial bucket along each axis
//   a1[200*by + 69 + 25*bx]    — head of bucket (bx,by)'s entry list, where
//                                bx = x/bucketSize, by = y/bucketSize. (The
//                                bucket table is rows of 8 buckets, 25 ints
//                                each, hence 200 ints/row, based at int 69.)
// The list head and chained entries are TileAttrEntry pointers in this model.
// ---------------------------------------------------------------------------
struct TileAttributeGrid {
    i32 size;        // a1[0]
    i32 bucketSize;  // a1[1]
    // Bucket head pointers, indexed [200*by + 69 + 25*bx - 2] (the -2 drops the
    // two leading size/bucketSize ints so callers can size this array compactly).
    // For test/host construction, fill the slots you exercise.
    const TileAttrEntry** buckets;  // a1[2..] reinterpreted as head pointers
};

// gilde.exe 0x5c3418 — VIBE_Heightmap_LookupTileAttribute
//   (__usercall (grid@eax, x@edx, y@ebx) -> eax).
// Returns the terrain attribute byte (0..255) of tile (x,y), or -1 when:
//   - (x,y) is not strictly inside the grid (requires 0 < x,y < size), or
//   - no run in the containing bucket covers (x,y).
// Walks the bucket's entry list: stops at a null head or the first entry whose
// row field is negative; an entry matches when row == y and xMin <= x <= xMax.
int LookupTileAttribute(const TileAttributeGrid* grid, int x, int y);

// ---------------------------------------------------------------------------
// Walkability grid as FindNearestWalkableTile indexes it.
//   size  = *(a1+0x20)
//   cells = *(a1+0x24) — base of size*size 24-byte cell records; the cell's
//           terrain-type byte is at record+0 (stride 24, index x + y*size).
// A cell is "walkable" when its type byte is non-zero and not 13.
// ---------------------------------------------------------------------------
struct TileWalkGrid {
    i32       size;   // +0x20  *(a1+32)
    const u8* cells;  // +0x24  *(a1+36)  (24-byte stride; type byte at +0)
};

// gilde.exe 0x5c6478 — VIBE_Heightmap_FindNearestWalkableTile
//   (__userpurge (grid@eax, centerY@edx, foundX@ecx, centerX@ebx, foundY)).
// Searches outward from tile (centerX, centerY) in expanding diamonds (L1 / taxi
// rings of radius r = 0, 1, 2, ...) for the first walkable tile, scanning rows
// from centerY downward within each ring's clamped bounds. On success writes the
// found column to *foundX, the found row to *foundY and returns true; returns
// false if the grid/pointers are null or no walkable tile exists within size
// rings. Tiles off the grid edges are skipped (clamped, never wrapped).
bool FindNearestWalkableTile(const TileWalkGrid* grid, int centerY, int* foundX,
                             int centerX, int* foundY);

} // namespace guild::render
