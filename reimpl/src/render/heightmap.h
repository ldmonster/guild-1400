#pragma once
#include "guild/common/types.h"

#include <cstring>

// Heightmap (terrain elevation grid) for the guild::render present/terrain layer
// (d3_engine.c "d3_sm:" — "smooth map"). Recovered from the raw *(type*)(base+off)
// access in gilde.exe (the IDB had no named UDTs).
//
//   VIBE_Heightmap_TileToWorld            @0x5c65d4  (tile -> world point)
//   VIBE_Heightmap_WorldToTileWithHeight  @0x5c6644  (world -> tile + bilinear h)
//   VIBE_Heightmap_FloodFillTileType      @0x5c5530  (4-neighbour tile-type fill)
//   VIBE_Terrain_AverageAreaHeight        @0x427468  (8x8 box-average height)
//   VIBE_Heightmap_Free                   @0x5c6438  (release buffers)
//
// The deeply render-coupled builders are LISTED in the module report as deferred
// (VIBE_Heightmap_BuildTerrainMesh @0x5c5610 grid-scale setup is translated below;
// its draw-list walk + raster submission are deferred — see Heightmap_DeriveGridScale).
namespace guild::render {

namespace detail {
// Exact float reconstruction from the binary's dword (C++17: memcpy bit-cast).
inline float F32FromBits(u32 bits) {
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
} // namespace detail

// flt_628BA8 @0x628BA8 — the BuildTerrainMesh @0x5c5610 height normalization:
//   scaleY = (maxY - minY) * flt_628BA8        (fmul @0x5c5be6)
// EXACT dword 0x3B81848E = 0.00395257212f, whose reciprocal is 252.99977
// (i.e. ~ 1/253.0 — NOT the previously documented "1/252.85"; verified with
// get_int over the binary).
inline const float kTerrainScaleYNorm = detail::F32FromBits(0x3B81848Eu);

// ---------------------------------------------------------------------------
// Heightmap record — gilde.exe "d3_sm:Create" (0x30 = 48 bytes). Recovered from
// VIBE_Heightmap_Create @0x5c63a0, TileToWorld @0x5c65d4, WorldToTileWithHeight
// @0x5c6644 and BuildTerrainMesh @0x5c5610.
//
//   world( tileX, tileY ) =
//     x = tileX*scaleX + originX
//     y = heights[tileY*size + tileX]*scaleY + originY
//     z = tileY*scaleZ + originZ
//
// `heights` is a size*size byte grid (one elevation byte per cell). `entries` is a
// size*size array of 24-byte tile records (terrain-type / lighting; built by the
// deferred mesh builder). `size` is the square grid edge length.
// ---------------------------------------------------------------------------
struct Heightmap {
    float originX;   // +0x00  world X of tile (0,*)          (*(a1))
    float originY;   // +0x04  world Y (height) base          (*(a1+4))
    float originZ;   // +0x08  world Z of tile (*,0)          (*(a1+8))
    float _pad0c;    // +0x0C
    float scaleX;    // +0x10  world units per tile step in X (*(a1+16))
    float scaleY;    // +0x14  world units per height byte    (*(a1+20))
    float scaleZ;    // +0x18  world units per tile step in Z (*(a1+24))
    float _pad1c;    // +0x1C
    i32   size;      // +0x20  grid edge (width == height == size) (*(a1+32))
    u8*   entries;   // +0x24  size*size * 24-byte tile records    (*(a1+36))
    u8*   heights;   // +0x28  size*size elevation bytes           (*(a1+40))
    float _pad2c8;   //        (padding so flag2d lands at +0x2D in a 32-bit record)
    u8    flag2d;    // +0x2D  v5[45]: creation flag (a3 in Create)
    //   *a3   = tileX*scaleX + originX
    //   a3[1] = heights[tileY*size + tileX]*scaleY + originY
    //   a3[2] = tileY*scaleZ + originZ
};

// gilde.exe 0x5c65d4 — VIBE_Heightmap_TileToWorld
//   (__usercall (hm@eax, tileX@edx, out@ecx, tileY@ebx) -> al).
// Converts an integer tile coordinate to a world point (out[0..2] = x,y,z).
// Returns false if hm is null or (tileX,tileY) is outside [0,size). The height
// byte is sampled directly (no interpolation): heights[tileY*size + tileX].
bool TileToWorld(const Heightmap* hm, int tileX, int tileY, float out[3]);

// gilde.exe 0x5c6644 — VIBE_Heightmap_WorldToTileWithHeight
//   (__userpurge (hm@eax, world@edx, tileXOut@ebx, heightOut)).
// Maps a world point (world[0]=x, world[2]=z) back to the containing tile
// (tileXOut, tileYOut) and, if heightOut != null, computes the BILINEARLY
// interpolated terrain height at the exact world position. Tile indices are
// truncated toward zero (ConvertX). Neighbour cells wrap with `(size-1) & (i+1)`
// (the original masks, which is exact only when size is a power of two — it is).
// Returns false when off the map or when the out pointers are null.
bool WorldToTileWithHeight(const Heightmap* hm, const float world[3],
                           int* tileXOut, int* tileYOut, float* heightOut);

// gilde.exe 0x5c5530 — VIBE_Heightmap_FloodFillTileType
//   (__usercall (hm@eax, fromType@dl, toType@bl)).
// For every interior cell whose terrain-type byte == fromType, retypes it to
// toType when all four 4-neighbours are already fromType or toType. One pass over
// the entries grid (24-byte stride; type byte at +0). Returns size-1.
int FloodFillTileType(Heightmap* hm, u8 fromType, u8 toType);

// gilde.exe 0x427468 — VIBE_Terrain_AverageAreaHeight
//   (__usercall (root@ecx, ?@edx, world@eax) -> st0).
// Averages the elevation over the 8x8 block of cells centred on the tile that
// contains `world`, returning a world-space height. Cells on/over the grid edge
// fall back to the bilinear sample at `world`. The 1/64 weight is flt_6115B4.
// `hm` is the heightmap; `world` is the query point (x,_,z).
double AverageAreaHeight(const Heightmap* hm, const float world[3]);

// gilde.exe 0x5c6438 — VIBE_Heightmap_Free: release entries/heights then the record.
// Caller owns the allocation policy; we just null the pointers and report freed.
void Free(Heightmap* hm);

// ---- Grid-scale setup (the testable core of BuildTerrainMesh @0x5c5610) -------
// Derives the world<->tile mapping fields (originX/Y/Z, scaleX/Y/Z) of a heightmap
// from a computed world bounding box, exactly as VIBE_Heightmap_BuildTerrainMesh
// does after the scene-graph bounds walk. The full builder's draw-list walk and
// raster submission are DEFERRED (render-coupled); this is the byte-for-byte scale
// math, which the sampling functions above depend on.
//   minX/minY/minZ, maxX/maxZ are the world AABB (Y handled by caller);
//   scaleX = (maxX-minX) / (size + flt_628BA4)   flt_628BA4 @0x628BA4 = 0xBFE00000 (-1.75)
//   scaleY = (maxY - minY) * flt_628BA8          flt_628BA8 @0x628BA8 = 0x3B81848E
//                                                (= 0.0039525721, ~1/253.0; kTerrainScaleYNorm)
//   scaleZ = (minZw - maxZw) / (flt_628BA4 + size)
//   originX = minX; originY = minY + 1.0 (@0x5c5b95 fld1; fadd minY); originZ = maxZw
// (We expose the X/Z derivation, which is self-contained and verifiable.)
void DeriveGridScaleXZ(Heightmap* hm, float minX, float maxX,
                       float originZ, float minZ);

} // namespace guild::render
