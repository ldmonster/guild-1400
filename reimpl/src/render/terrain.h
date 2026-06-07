#pragma once
#include "guild/common/types.h"

// Terrain tile-grid indexing + uniformity test for the guild::render present/terrain
// layer (d3_engine.c floor). The huge render walk VIBE_Floor_RenderTerrain (@0x5bf22c,
// 0x386D — the largest function in the binary) is deeply coupled to the rasterizer /
// scene-graph / texture cache and is LISTED as deferred in the module report. The
// self-contained tile-grid core that the walk and the heightmap share lives here.
namespace guild::render {

// ---------------------------------------------------------------------------
// Terrain tile grid — the small descriptor the per-tile helpers index through
// (recovered from VIBE_Floor_TileIsUniform @0x5bbbf4, which reads a1[0]/a1[3]/a1[5]).
//   a1[0]  = size (grid edge)
//   a1[3]  = wrap mask (== size-1; the grid is a power-of-two torus)
//   a1[5]  = base of the per-cell terrain-type byte array (size*size bytes)
// A cell (x,y) maps to type byte  types[(mask & y)*size + (mask & x)].
// ---------------------------------------------------------------------------
struct TileGrid {
    i32       size;   // a1[0]  grid edge
    i32       mask;   // a1[3]  wrap mask (size - 1)
    const u8* types;  // a1[5]  per-cell terrain-type bytes (row-major, stride=size)
};

// gilde.exe 0x5bbbf4 — VIBE_Floor_TileIsUniform
//   (__usercall (grid@eax, x0@edx, span@ecx, y0@ebx) -> al).
// Returns true (1) when every terrain-type byte in the wrapped square region is
// identical, else 0. Precisely (xEnd = x0+span, yEnd = y0+span; both inclusive):
//   for y in [y0, yEnd]:  for x in [x0, xEnd]:
//       t = types[(mask & y)*size + (mask & x)];
//       (the first visited cell seeds the reference; any mismatch -> false)
// Coordinates wrap via the mask. A degenerate region (y0 > yEnd) returns true.
bool TileIsUniform(const TileGrid* grid, int x0, int span, int y0);

// Wrapped cell -> type-byte index, mirroring the address math in TileIsUniform
//   idx = (mask & y) * size + (mask & x).
inline i32 TileTypeIndex(const TileGrid& g, int x, int y) {
    return (g.mask & y) * g.size + (g.mask & x);
}

// Type byte at wrapped cell (x,y).
inline u8 TileType(const TileGrid& g, int x, int y) {
    return g.types[TileTypeIndex(g, x, y)];
}

} // namespace guild::render
