#pragma once
#include "guild/common/types.h"
#include "render/geometry_types.h"
#include "render/mesh.h"   // DrawList sink

// =============================================================================
// guild::render — TERRAIN TILE GEOMETRY build: turn a tile's height samples into
// projected-ready 80-byte Vertex records, and append the tile's polygons into the
// global draw list. Faithful 1:1 reconstruction of the tile-geometry cores of:
//
//   0x5BF22C  VIBE_Floor_RenderTerrain         (the per-tile vertex build loop,
//                                               decompiled lines ~651..705)
//   0x5BE668  VIBE_Floor_TransformTileGeometry (vertex transform + poly draw-list
//                                               append, the function tail)
//
// THE PER-VERTEX BUILD (RenderTerrain inner loop, byte-for-byte)
// ---------------------------------------------------------------------------
// For each height sample h (a byte) at tile-local position the walk computes a
// world position and a per-channel RGB light, then writes them into an 80-byte
// Vertex:
//   world.x = D520 * h + accX
//   world.y = D524 * h + accY      (D520/D524/D528 = the floor's height axis
//   world.z = D528 * h + accZ       rotated into view, flt_13FD520/524/528)
//   l = (typeByte & 0x7F) * 2.0     (flt_628B4C = 2.0)
//   if typeByte >= 0 (high bit clear):
//       R = clamp255( D510*l + 4F0 )    (+66)
//       G = clamp255( D514*l + 4F4 )    (+65)
//       B = clamp255( D518*l + 4F8 )    (+64)
//   else (high bit set => shadowed):
//       R = clamp255( D510*l + 530 + 4F0 )
//       G = clamp255( D514*l + 534 + 4F4 )
//       B = clamp255( D518*l + 538 + 4F8 )
// where D510/514/518 are the per-channel light scales (flt_13FD510/514/518) and
// 4F0/4F4/4F8 (flt_13FD4F0/4F4/4F8) the ambient adds, 530/534/538
// (flt_13FD530/534/538) the shadow bias. clamp255 truncates the float to int and
// caps at 255 (the `if (x > 255) x = -1` byte-store == 0xFF). The walk advances
// the world accumulator by the per-step axis vector after each vertex.
//
// THE POLY DRAW-LIST APPEND (TransformTileGeometry tail, byte-for-byte)
// ---------------------------------------------------------------------------
// For every front-facing poly (its +36 flag byte < 0, i.e. bit7 backface-visible
// set) up to the remaining draw-list capacity, the engine appends an 8-byte
// DrawListEntry: sortKey = ((tex - texBase) >> 7) + 1 when the poly has a texture,
// else 0; entry.poly = the poly. We reproduce the sortKey math and the
// capacity-clamped append.
// =============================================================================
namespace guild::render {

// The runtime light/transform state the terrain walk reads from file scope, gathered
// so the build is self-contained + testable. Each field names its original global.
struct TileLightParams {
    float heightAxis[3]; // flt_13FD520/524/528: height-byte step in view space
    float lightScale[3]; // flt_13FD510/514/518: per-channel R/G/B light scale
    float ambient[3];    // flt_13FD4F0/4F4/4F8: per-channel ambient add
    float shadowBias[3]; // flt_13FD530/534/538: per-channel shadow (high-bit) bias
};

// Build ONE terrain vertex from a height sample + its type/shadow byte and the
// world-space accumulator (the running per-row/col position). Writes world xyz into
// v.x/v.y/v.z and the three light bytes into v (R@+66 -> color via lightIdx slot
// naming; see note). `typeByte`'s high bit selects the shadow-biased branch.
// Returns the packed BGR light dword the engine stored at +64 (R<<16|...): exposed
// for the golden test. (faithful to VIBE_Floor_RenderTerrain @0x5bf22c inner loop)
u32 BuildTileVertex(Vertex& v, const TileLightParams& p, const float acc[3],
                    u8 heightSample, u8 typeByte);

// gilde.exe 0x5be668 — VIBE_Floor_TransformTileGeometry (poly draw-list append).
// Append the tile's front-facing polygons to `out`. `polys`/`polyCount` is the
// tile's poly array; for each poly whose flags36 high bit is set (visible), an
// entry is appended with sortKey = ((poly.texPtrId? ...)) — to keep this decoupled
// from the texture-cache pointer arithmetic we pass each poly's texture sort id
// directly via `texSortId` (== ((tex - dword_1406A84) >> 7) + 1, or 0 when the
// poly is untextured). Honors the remaining capacity (dword_13ECE80 - count).
// Returns the number of entries appended.
i32 AppendTilePolysToDrawList(const Polygon* polys, i32 polyCount,
                              const u32* texSortId, DrawList* out);

// Clamp a float light term to a 0..255 byte exactly as the engine did:
//   t = (int)x;  if (t > 255) return 0xFF;  else return (u8)t;
// (the engine never clamped the low end — negative values wrap as the cast does).
u8 ClampLightByte(float x);

// NOTE: PER-QUAD UV EMISSION (the @0x5bf22c poly+16/+56 UV pointers + the
// flt_13FE540 24-float corner-inset table) lives in render/terrain_uvtable.{h,cpp}
// (TerrainQuadUvT0/T1 + TerrainSubTexId + the flt_628B48=0.5 seam blend), driven by
// the terrain walk (terrain_walk.cpp Pass A stamps each quad's tri0/tri1 UV record,
// Pass B blends the seam midpoint). It is NOT a per-vertex build step, so it is not
// folded into BuildTileVertex here — the vertex build owns world xyz + RGB light only.

// NOTE: VIBE_Heightmap_FloodFillTileType @0x5c5530 (the single-pass 4-neighbour
// terrain-type fill, xref'd only from VIBE_Heightmap_BuildTerrainMesh @0x5c5610)
// is ALREADY reconstructed 1:1 over the 24-byte cell grid in render/heightmap.cpp
// (FloodFillTileType(Heightmap*, u8, u8)); it is NOT re-defined here (ODR / reuse).
// Verified against the decompile in wave-5 (see progress/tile-textures-wave5.md).

} // namespace guild::render
