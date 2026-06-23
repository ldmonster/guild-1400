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
// THE 8-NAME TABLE AT Floor+0x1A64 (corrected, terrain-ground wave 4):
//   The Floor record carries 8 64-byte NAME slots at Floor+0x1A64 (== +6756).
//   VIBE_Floor_LoadFromHeightmap @0x5bd44c (v144 = floor+6756, the @0x5bd810..
//   0x5bd89d loop) copies the floor block's 8 terrain-type/texture-slot names
//   ("SAND", "EINFACHER_WEG", ...; ctx+164+64*i of LoadFloorRegions @0x5e78a8)
//   into these slots — they are the floor TEXTURE-SLOT names, NOT dynamic light
//   sources (the earlier interpretation). ComputeTileIllumination @0x5c4718
//   walks them:  for i in [0,8): name = Floor + 0x1A64 + 0x40*i; active = name[0].
//
// THE NAME -> TERRAIN-CLASS BUILD (was deferred; now captured in full):
//   For each non-empty slot name the build half matches the name against the 15
//   9-byte-stride patterns at 0x5c4690 ('_ill*','_unk*','SAND','ERDE','WIESE',
//   'MOOR','PFLASTER','KIESEL','FELS','EIS','WASSER','WEG','WEG','_ill*','')
//   via VIBE_Util_StrToUpper @0x5e9f50 + the strstr at loc_5CB930 and stores the
//   FIRST matching pattern INDEX into byte_1405100[i] (slot default 1 when the
//   name is empty; pattern 14 is the empty string, which strstr always matches,
//   so every non-empty name resolves to an index <= 14). dword_64A04C caches the
//   floor the table was last built for. Reconstructed below as
//   BuildTileIlluminationTable. (StrToUpper's body was not captured; its
//   uppercase-the-name semantics follow from its name + the call shape — the
//   shipped slot names are already uppercase, so the inference is inert there.)
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
    char name[64];   // +0x00  slot name (empty => inactive); see header note:
                     // these are the floor texture-slot/terrain-type names the
                     // 0x5bd44c loader copies to Floor+0x1A64.
};

// The 15 terrain-class match patterns at gilde.exe 0x5c4690 (9-byte stride,
// dumped byte-for-byte: get_bytes(0x5c4690, 9*15)). Pattern index == the
// terrain class stored into the illumination table:
//   0 '_ill*'  1 '_unk*'  2 'SAND'  3 'ERDE'  4 'WIESE'  5 'MOOR'
//   6 'PFLASTER'  7 'KIESEL'  8 'FELS'  9 'EIS'  10 'WASSER'  11 'WEG'
//   12 'WEG'  13 '_ill*'  14 ''  (empty: matches everything)
extern const char kTerrainTypePatterns[15][9];

// The 8-entry per-light-source illumination array (gilde.exe byte_1405100[8]).
// Entry i = the illumination value for light source i: 1 by default, or the
// matched wildcard-pattern index from the deferred name-match build half. The
// per-tile lookup (ComputeTileIllumination tail) indexes this by the tile's
// type byte. Modeled as an explicit struct so the lookup is testable.
struct TileIlluminationTable {
    u8 value[8];     // byte_1405100[0..7]
};

// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination, the once-per-floor
// NAME -> TERRAIN-CLASS table build (the @0x5c475c..0x5c479e slot loop). For each
// of the 8 64-byte name slots (Floor+0x1A64):
//   table[i] = 1;                              (default, @0x5c475c)
//   if (name[0]) for k in [0,15):              (@0x5c477f pattern loop)
//       if (strstr(StrToUpper(name), pattern[k])) { table[i] = k; break; }
// Pattern 14 is the empty string (strstr always matches), so every non-empty
// unmatched name lands on 14; only an EMPTY slot keeps the default 1. `names` is
// the 8-slot 64-byte-stride name block (Floor+0x1A64 == the typeNames the
// 0x5bd44c loader copied there). The dword_64A04C per-floor cache is the
// caller's (build once per floor).
TileIlluminationTable BuildTileIlluminationTable(const TileLightSource names[8]);

// gilde.exe 0x5c4718 — VIBE_Heightmap_ComputeTileIllumination (the per-tile lookup
// half, recovered from disasm).
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
//    same buffer to anti-alias the 2:1 tile seams; that in-place pyramid blend is
//    now reconstructed in full inside BuildLitTileGeometry below.)
i32 MipDownsample(const u8* src, i32 n, u8* dst);

// ---------------------------------------------------------------------------
// gilde.exe 0x5c47dc — VIBE_Heightmap_BuildLitTileGeometry, the COMPLETE driver
// (was deferred; the full decompile is now captured). Inputs:
//   a1 = the Floor:    +0   N (grid edge)        +16 heights (N*N bytes)
//                      +148 originY (float)      +196 scaleH (axisH.y, float)
//                      +20  texture grid          (via ComputeTileIllumination)
//   a2 = the Heightmap: +4 originY  +20 scaleY  +32 size  +36 entries (24-byte
//                      stride, terrain-class byte at +0)  +40 heights.
//
// Branch 1 (hm.size >= floor.N, the `if (v93/v89)` arm): for every floor cell
// (tx,ty) write
//   hm.heights[(ty*r)*S + tx*r] = BuildTileElevationByte(h, scaleH, originY,
//                                                        hm.originY, 1/hm.scaleY)
//   hm.entries[24*((ty*r)*S + tx*r)] = ComputeTileIllumination(tx, ty, floor)
// (r = hm.size/floor.N, S = hm.size; the heights store index, whose register the
// decompile lost to the ConvertX clobber, is reconstructed from the entries
// cursor symmetry 24*S*ty*r stepping 24*r), then run the in-place midpoint
// pyramid (`while (r > 1)`): per coarse cell A=(x,y), with B=(x,y+r), C=(x+r,y),
// D=(x+r,y+r) (x+r / y+r wrapped through mask S-1):
//   avg4                    = (A+B+C+D) >> 2          (signed-shift /4)
//   heights[x+r/2, y]       = (avg4 + 2*(A+C)) / 5    (top-edge midpoint)
//   heights[x,     y+r/2]   = (avg4 + 2*(A+B)) / 5    (left-edge midpoint)
//   heights[x+r/2, y+r/2]   = avg4                    (centre)
//   entries byte at all three midpoints = entries[A]
//
// Branch 2 (floor.N > hm.size): per hm cell box-average the ratio^2 projected
// floor elevations (sum of ConvertX-truncated per-cell bytes, / ratio^2) and
// majority-vote the ratio^2 ComputeTileIllumination classes over 15 bins
// (first-max wins; all-zero bins store 0).
//
// `illum` is the BuildTileIlluminationTable result for this floor (the engine
// builds it lazily inside ComputeTileIllumination via the dword_64A04C cache).
// Returns the original's (meaningless) loop-counter eax.
// ---------------------------------------------------------------------------
struct LitFloorView {
    i32       size    = 0;        // floor+0   N
    const u8* heights = nullptr;  // floor+16  N*N elevation bytes
    const u8* texGrid = nullptr;  // floor+20  N*N texture-slot indices
    float     originY = 0.0f;     // floor+148 world height origin
    float     scaleH  = 0.0f;     // floor+196 world units per height byte (axisH.y)
};
struct Heightmap;  // render/heightmap.h
u32 BuildLitTileGeometry(const LitFloorView& floor, Heightmap* hm,
                         const TileIlluminationTable& illum);

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
