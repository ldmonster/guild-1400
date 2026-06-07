#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — TERRAIN TILE LIGHTING: the deferred-inner-detail of the big
// VIBE_Floor_RenderTerrain walk that is self-contained enough to translate 1:1.
// Faithful reconstruction of:
//
//   0x5bc294  VIBE_Floor_StampLightCircle            (per dynamic light: stamp a
//                                                      0x80 "lit" bit into a circle
//                                                      of the per-cell type grid)
//   0x5bc45c  VIBE_Floor_BuildTilePolys              (slope-based dynamic light
//                                                      stamp + quad poly-visibility
//                                                      build)
//   0x5c4718  VIBE_Heightmap_ComputeTileIllumination (8-light-source illumination
//                                                      lookup — the per-tile half)
//   0x5c47dc  VIBE_Heightmap_BuildLitTileGeometry    (heightmap mip downsample +
//                                                      per-tile illumination fill)
//
// LIGHT-SOURCE struct (recovered from ComputeTileIllumination @0x5c4718 disasm):
//   The Floor record carries an array of 8 dynamic light sources beginning at
//   Floor+0x1A64, stride 0x40 (64) bytes. The disasm walks:
//     ebp = Floor;  for i in [0,8): name = *(Floor + 0x1A64 + 0x40*i)
//                                   active = name[0] != 0
//   i.e. the light source's NAME (a C string) is at +0x00 of the 64-byte record,
//   and "active" is simply name[0] != 0. The build half matches each active name
//   against 15 wildcard patterns (stride 9: "_ill*", "_unk*", ...) via a
//   '*'-wildcard matcher and stores the matched pattern index into an 8-entry
//   illumination array byte_1405100[i] (default 1 when no pattern matches). That
//   name->pattern build is data-coupled (the pattern table + wildcard matcher) and
//   is LISTED deferred; here we model the recovered record + the per-tile LOOKUP.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Dynamic light-source record — 64-byte stride, array of 8 at Floor+0x1A64.
// Recovered from VIBE_Heightmap_ComputeTileIllumination @0x5c4718:
//   +0x00  name : char[]  (the source name; name[0]==0 => slot inactive)
// (only the name field participates in illumination; the remaining 64 bytes hold
//  the source's transform/colour which the deferred build half consumes.)
// ---------------------------------------------------------------------------
struct TileLightSource {
    char name[64];   // +0x00  source name (empty => inactive)
};

// The 8-entry per-light-source illumination array (gilde.exe byte_1405100[8]).
// Entry i = the illumination value for light source i: 1 by default, or the
// matched wildcard-pattern index from the deferred name-match build half. The
// per-tile lookup (ComputeTileIllumination tail) indexes this by the tile's
// type byte. Modeled as an explicit struct so the lookup is testable.
struct TileIlluminationTable {
    u8 value[8];     // byte_1405100[0..7]
};

// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination (the per-tile lookup
// half, recovered from disasm; the once-per-floor name-match build is deferred).
//   cell = (mask & x) + size * (mask & y)          (mask == size-1)
//   t    = types[cell]                              (the per-cell type byte)
//   if (t & 0x80)  return 0;                        (high bit => hole/unlit)
//   return illum.value[t];                          (type byte selects a source)
// `size` is the grid edge (power of two); `types` the size*size type-byte grid;
// `illum` the precomputed 8-entry illumination table. `x`/`y` wrap via mask.
u8 ComputeTileIllumination(const u8* types, i32 size, i32 x, i32 y,
                           const TileIlluminationTable& illum);

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc294 — VIBE_Floor_StampLightCircle (the type-grid stamp core).
// For a dynamic light whose ground-projected position is tile (cx,cy) with an
// integer pixel radius `r`, OR the 0x80 "lit" bit into every type-grid cell whose
// squared distance to (cx,cy) is < r2cap. The original derives r/r2cap from the
// light's world radius and the floor scale, then:
//   for row in [cy-r, cy+r] clamped to [0, size):     (y span)
//     col span = [cx-r, cx+r] clamped to [0, size):    (size-1 upper clamp == v3[0]-1)
//       if (col-cx)^2 + (row-cy)^2 < r2cap:  types[row*size + col] |= 0x80
// When r <= 1 the original stamps only the single centre cell types[cy*size+cx].
// `size` is the grid edge; this is the byte-for-byte inner stamp. (The world-space
// radius derivation that produces cx/cy/r/r2cap drives off the scene-graph pick
// and bone-chain transform and is part of the deferred walk; here those come in
// already computed so the stamp itself is testable.)
//   r    : integer radius (v19)
//   r2cap: squared-distance cap (v24 = (int)(r*r + 0.5), clamped to <= size span)
// ---------------------------------------------------------------------------
void StampLightCircle(u8* types, i32 size, i32 cx, i32 cy, i32 r, i32 r2cap);

// ---------------------------------------------------------------------------
// gilde.exe 0x5bc45c — VIBE_Floor_BuildTilePolys, the SLOPE-BASED DYNAMIC LIGHT
// stamp (decompiled inner loop). For each interior cell the engine builds the
// cell's surface normal from the 4-neighbour height differences, dots it with the
// (rotated) light direction, and when the dot is negative (facing the light) looks
// up a falloff value and ORs the resulting 0..127 intensity into the cell's
// light-accumulator byte:
//   dx = h[x+1] - h[x-1]      (wrapped with mask)   -> nx = dx * sx
//   dz = h[y+1] - h[y-1]      (wrapped with mask)   -> nz = dz * sz
//   ny = sy   (constant)
//   inv = 1 / sqrt(nx*nx + ny*ny + nz*nz)
//   d   = (nx*inv)*L0 + (ny*inv... )                (dot with light dir L[3])
//   if (d < 0):
//       idx  = (int)(d * -1023.0)                   (dbl_62887C, clamp into LUT)
//       lit  = (int)(scaleHi * falloffLut[idx])
//       cell |= min(lit, 127)
// where sx = scaleH*(-4.0)*axisVz, sz = scaleHt*2.0*axisVz, sy = scaleHt*2.0*scaleH
// (flt_628878=-4.0, flt_628874=2.0). The normal scales sx/sy/sz and the light dir
// L are computed once from the floor + the dominant light's hierarchy rotation;
// they are passed in here (the rotation uses util::transform, part of the deferred
// setup). `scaleHi` (v53 = light[37] * 0.5, flt_62886C) scales the falloff. This
// reproduces the per-cell slope-light stamp 1:1.
// ---------------------------------------------------------------------------
struct SlopeLightParams {
    float normalScaleX;  // sx (v54): flt_628878(-4) * axisH? folded; passed computed
    float normalScaleY;  // sy (v55): constant Y normal term
    float normalScaleZ;  // sz (v52): Z slope scale
    float lightDir[3];   // L[0..2]: rotated light direction (RotateVectorByHierarchy)
    float falloffScale;  // v53: scaleHi multiplier (light[37] * 0.5)
};

// Stamp slope-based dynamic light into `accum` (the per-cell light byte grid,
// size*size). `heights` is the size*size height-byte grid; `falloffLut` the
// 1024-entry cosine falloff LUT (gilde.exe flt_1405110, the deferred acos table).
// Coordinates wrap with mask = size-1.
void StampSlopeLight(u8* accum, const u8* heights, i32 size,
                     const SlopeLightParams& p, const float* falloffLut);

// ---------------------------------------------------------------------------
// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, the per-cell HEIGHT
// -> ELEVATION-BYTE fill (the first loop, recovered cleanly from the decompile):
//   elev = ((double)(int16)h * scaleH + originY - tileMinY) * (1 / tileScaleY)
//   elevGrid[cell] = (int)elev                 (ConvertX truncate toward zero)
// alongside elevGrid the engine writes illumGrid[cell] = ComputeTileIllumination(..).
// `h` is the raw height byte (sign-extended through int16), `scaleH` the floor's
// per-byte height scale (Floor+196), `originY` the floor height origin (Floor+148),
// `tileMinY` the tile's world Y min (heightmap+4), `invScaleY` = 1/(heightmap+20).
// Returns the clamped elevation byte. (faithful to the inner v28 computation.)
u8 BuildTileElevationByte(u8 h, float scaleH, float originY, float tileMinY,
                          float invScaleY);

// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, MIP DOWNSAMPLE. The
// engine reduces the per-cell elevation/illumination grids by 2x each pyramid level
// (the `while (v94 > 1)` loop) so coarser LODs reuse averaged samples. The clean,
// well-defined reduce is a 2x2 box average: out[i][j] = (g[2i][2j] + g[2i][2j+1] +
// g[2i+1][2j] + g[2i+1][2j+1]) / 4 (unsigned truncating division by 4, matching the
// engine's `v36 >> 2` of the 4-sample sum). `src` is an n x n byte grid (n even);
// `dst` receives the (n/2) x (n/2) reduced grid. Returns n/2.
//   (The engine ALSO writes a seam-blended (sum/4 + 2*edgePair)/5 variant into the
//    same buffer to anti-alias the 2:1 tile seams; that in-place pyramid blend with
//    its interleaved vertex-grid indexing is LISTED deferred — see report.)
i32 MipDownsample(const u8* src, i32 n, u8* dst);

// gilde.exe 0x5bc45c — VIBE_Floor_BuildTilePolys, the QUAD POLY-VISIBILITY build.
// For each quad of 4 corner type bytes (each cell's high bit = "hole/unwalkable"),
// the engine classifies the 4-corner hole pattern (h0=c00<0, h1=c01<0, h2=c10<0,
// h3=c11<0; "<0" == high bit 0x80 set == hole) and either CLEARS 0x80 on the
// quad's poly flag (visible), SETS 0x80 (hidden), or leaves it UNCHANGED. The exact
// branch tree (recovered from the disasm cascade at 0x5bca69..0x5bcb30) matches 9
// specific corner patterns; every other pattern leaves the flag untouched.
enum class QuadPolyAction { Unchanged = 0, Visible = 1, Hidden = 2 };

// Classify a quad from its 4 corner-hole booleans. Byte-for-byte the disasm tree:
//   Hidden  : h0 && !h1 && !h2 && !h3   |  h0 && !h1 &&  h2 &&  h3
//           : !h0 && h1 && !h2 && !h3   | !h0 && !h1 &&  h2 &&  h3
//   Visible : h0 &&  h1 && !h2 &&  h3   | !h0 &&  h1 &&  h2 && !h3
//           : h0 &&  h1 &&  h2 && !h3   | !h0 && !h1 && !h2 &&  h3
//           : h0 &&  h1 && !h2 && !h3
//   else      Unchanged
QuadPolyAction QuadPolyVisible(bool h0, bool h1, bool h2, bool h3);

} // namespace guild::render
