#pragma once
#include "guild/common/types.h"

#include <vector>

// Water-surface support for the guild::render terrain layer (d3_engine.c floor/water).
//
//   VIBE_FloorWater_FloodFillMask     @0x5ba750  (4-way recursive region flood fill)
//   VIBE_FloorWater_FillHeightGradient@0x5ba898  (linear height ramp along a span)
//   VIBE_Floor_AnimateWaterVertices   @0x5be428  (per-quad sine/cosine wave offsets)
//
// The region-extraction driver VIBE_FloorWater_PrepareRegions (@0x5ba95c, 0x1295 —
// the second-largest in the module) is still LISTED deferred (render/data-coupled,
// 1248-instruction allocation-heavy driver). VIBE_FloorWater_FindRegionOffset
// (@0x5ba824) IS reconstructed below: its region-descriptor layout is recoverable
// (20-byte span records terminated by a negative next-link).
namespace guild::render {

// ---------------------------------------------------------------------------
// A water region span descriptor — the 20-byte record FindRegionOffset walks
// (Floor+52 points at the first; records are contiguous, stride 20). RECOVERED
// from the address math in VIBE_FloorWater_FindRegionOffset.
//   +0x00 base    row-base column index into the water buffer
//   +0x04 type    region type tag (matched against the query `type`)
//   +0x08 lo      inclusive low bound of the span (query >= lo)
//   +0x0C hi      inclusive high bound of the span (query <= hi)
//   +0x10 marker  visited-marker byte (0xFF == unmarked; stamped on first hit)
// The list is terminated when a record's NEXT record's +0x04 (peeked as
// *(int*)(rec+0x18)) is negative; the first record's +0x04 < 0 means "no regions".
// ---------------------------------------------------------------------------
struct WaterRegionSpan {
    i32 base;    // +0x00
    i32 type;    // +0x04
    i32 lo;      // +0x08
    i32 hi;      // +0x0C
    u8  marker;  // +0x10
    u8  pad[3];  // +0x11..0x13 (record stride is 20 bytes)
};

// gilde.exe 0x5ba824 — VIBE_FloorWater_FindRegionOffset
//   (__usercall (floor@eax, query@edx, marker@cl, type@ebx) -> int).
// Linear-scans the region span list for the span whose `type` matches and whose
// [lo,hi] contains `query`. On a match: if the span is unmarked (marker==0xFF) it
// stamps `marker`, then returns the water-buffer byte offset
//   80 * (span.base + query - span.lo) + bufferBase
// where bufferBase == Floor+28 (water cell array, 80-byte cell stride). Returns 0
// when there are no regions (first span's type < 0) or the list ends (a span whose
// successor's type < 0) without a match.
// `spans`/`spanCount` model the contiguous 20-byte records at Floor+52; `bufferBase`
// is Floor+28. The original terminates on a negative NEXT-link rather than a count;
// we keep that semantics and additionally bound the scan by `spanCount`.
i32 FindRegionOffset(WaterRegionSpan* spans, i32 spanCount, i32 bufferBase,
                     i32 query, u8 marker, i32 type);

// gilde.exe 0x5ba750 — VIBE_FloorWater_FloodFillMask
//   (__userpurge (mask@eax, stride@edx, y@ecx, x@ebx, from, to)).
// 4-connected recursive flood fill over a byte mask of dimension `stride` (the grid
// is stride*stride). Sets mask[y*stride + x] = to, then recurses into +x/-x/+y/-y
// neighbours whose current value equals `from`. Bounds: x in [0,stride), y in
// [0,stride). The original is tail-recursive on -y; we keep the same traversal.
void FloodFillMask(u8* mask, int stride, int y, int x, u8 from, u8 to);

// gilde.exe 0x5ba898 — VIBE_FloorWater_FillHeightGradient
//   (__userpurge (?, lo@cl, hi@ebx, hiVal)). Writes a linear ramp of byte heights
//   into the cells dst[lo..hi] (inclusive), interpolating from `loVal` at index
//   `lo` to `hiVal` at index `hi`. Each written value is round(v + 0.5) truncated
//   (flt_628760 == 0.5). No-op when hi < lo. `dst[i]` is the cell at index i.
void FillHeightGradient(u8* dst, int lo, int hi, u8 loVal, u8 hiVal);

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (wave displacement core).
//   The original loops over each water mesh: it advances texture animation (deferred,
//   needs the global texture group table) and, per mesh, recomputes a 4x4 grid of
//   16 vertex displacement vectors from layered sin/cos of the cell index plus three
//   per-mesh phase offsets (a3[78..81]) and a global time-derived amplitude `t`.
//
// This routine reproduces exactly the 16-vertex displacement loop (the visible water
// ripple). For cell index c in [0,16):
//   out[c].x = amp[0] * sin( cos(c*2.7) * t + c + phase[0] )
//   out[c].y = amp[1] * cos( sin(pi - c*2.4) * t + c + phase[1] )
//   out[c].z = amp[2] * sin( c - cos(c*2.5) * t + 2.2 + phase[2] )
//   out[c].w = amp[3] * cos( c - sin(c*2.6 + pi) * t + 4.0 + phase[3] )
// where amp[0..3] are a3[10..13] and phase[0..3] are a3[78..81]. `out16x4` is a flat
// array of 16 vec4 (64 floats). The original passes t == 2*pi (dbl_628AF4) as the
// trig amplitude multiplier; it is a parameter here for testability.
void AnimateWaterWaveGrid(float* out16x4, const float amp[4], const float phase[4],
                          double t);

// ===========================================================================
// gilde.exe 0x5ba95c — VIBE_FloorWater_PrepareRegions (the water-region builder).
//
// This is the INPUT producer for VIBE_Floor_AnimateWaterVertices (@0x5be428,
// water_vertices.cpp): it turns the floor's water-mask grid into the array of
// animated water-region meshes the per-frame animator walks, i.e. the contents
// of Floor+0x19E0 (waterMeshes, the 344-byte WaterMesh records) and the count
// byte Floor+0x1C6D (waterMeshCount).
//
// The original operates IN PLACE on the live Floor record. Its passes:
//   1. (loc_5BAA1D)  WATER-MASK + DILATE: alloc an N*N "d3_fl:Water(water_mask)"
//      scratch; for every interior cell (y,x in [0,N-4)) whose waterMaskGrid
//      cell == the water-type byte (Floor+7276), stamp that cell AND a 3x3
//      wrap-around dilation block to 0xFF, set the global anyWater flag, and set
//      per-tile/edge corner flags inside the Floor tile blocks (Floor-coupled —
//      see WaterRegionTileFlags). Cell linear index uses (x + y*N).
//   2. (loc_5BACD0)  HEIGHTS: if Floor+0x18 (waterHeights) exists, AND it with
//      the mask (and where mask==0xFF and height==0, copy 0xFF in); else alloc a
//      fresh "d3_fl:Water(height)" buffer = a copy of the mask. Then per ROW,
//      VIBE_FloorWater_FillHeightGradient ramps the height across each run of
//      0xFF cells, seeding the ramp endpoints from the neighbouring terrain
//      heightmap (Floor+0x10) clamped to >= 1 (val-2, min 1).
//   3. (loc_5BAE61)  EDGE BITCODE: per cell, a 4-neighbour water test (self<<3 |
//      up<<2 | right<<1 | diag) drives a switch that sets/clears bit7 (0x80) of
//      the Floor+0x24 (a1[9]) per-cell edge buffer.
//   4. (loc_5BAF56)  REGIONS: alloc an N*N "d3_fl:Water(CalcRegions)" buffer init
//      to 0xFF; mark every interior cell whose own height and its +1 row/+1 col
//      neighbours are all water as 0xFE; then VIBE_FloorWater_FloodFillMask each
//      0xFE seed with an incrementing region id (Floor+7277). regionCount =
//      number of distinct flood-filled regions.
//   5. (loc_5BB169)  STRIP/POLY/MESH build: per Floor tile (8x8 tiles, each
//      tileSpan==N/8 cells) with a water corner, scan its cell rows building
//      20-byte horizontal SPAN records ("d3_fl:Water(strips)"), 80-byte POINT
//      records ("d3_fl:Water(points)"), and 40-byte POLY records
//      ("d3_fl:Water(polys)") whose corner offsets resolve through
//      VIBE_FloorWater_FindRegionOffset; plus the 344-byte WaterMesh array
//      ("d3_fl:Water(Regions)") loaded with the water texture "EF_WASS_06A_2T_W
//      _AN0" (VIBE_Texture_LoadByName @0x5da714, present-coupled — hooked by
//      callback). The per-region span list is then compacted (removing spans
//      whose marker stayed 0xFF — never referenced by a poly).
//
// Reconstructed here as a clean builder over the PARSED grid (W5-WATER handoff):
// the grid-level passes (1 mask/dilate, 2 height gradient, 3 edge bitcode, 4
// region flood-fill) are reproduced exactly; the per-tile strip/poly geometry is
// reconstructed as pure data; the 344-byte WaterMesh array is produced ready for
// AnimateWaterVertices. The Floor-tile-block side effects (pass-1 corner flags,
// the Floor+0x24 edge buffer write, and the in-Floor allocation churn) are
// modelled as documented outputs (the terrain-walk owner installs them — see the
// install handoff in progress/water-regions-wave5.md). Present-coupled leaves
// (the DDraw texture upload VIBE_Texture_UploadToSurface @0x5db234) are reached
// only through the injected callback.
// ===========================================================================

// One horizontal water span record — the 20-byte "d3_fl:Water(strips)" record
// the strip builder emits and VIBE_FloorWater_FindRegionOffset walks. Matches
// WaterRegionSpan but produced (not consumed) here.
//   +0x00 base    running cumulative point index (sum of prior span lengths)
//   +0x04 row     the row this span lives on (== a1[1]*tileRow + relative)
//   +0x08 lo      inclusive low column of the run
//   +0x0C hi      inclusive high column of the run
//   +0x10 marker  0xFF (unmarked; FindRegionOffset stamps a region id later)
struct WaterStripSpan {
    i32 base;   // +0x00
    i32 row;    // +0x04 (the original stores the row at +4; type/row are the same field)
    i32 lo;     // +0x08
    i32 hi;     // +0x0C
    u8  marker; // +0x10
    // ---- reconstruction helper (NOT a 32-bit-record field) ------------------
    // The engine's strips/points/polys buffers are PER FLOOR TILE (v190[7]/[11]/
    // [13], allocated per (tileRow,tileCol) inside the 0x5bb169 build loop), so a
    // span's `base` is a per-tile cumulative POINT index (it restarts at 0 each
    // tile). The compacted WaterRegions::spans/polys are flattened across all
    // tiles; this id restores the tile grouping the render arm needs to resolve
    // FindRegionOffset point indices into the right per-tile point buffer.
    i32 tile = 0;   // tileRow*8 + tileCol (the v211/v212 tile this span belongs to)
};

// One 40-byte water poly record (the "d3_fl:Water(polys)" stride). The original
// holds two vec3+uv corner triplets resolved through FindRegionOffset; we keep
// the fields the build writes that are observable downstream.
//   regionId   = the CalcRegions byte at the cell (rec+37 / rec+77)
//   cell       = the linear cell index (kk) this poly covers
//   col        = the cell column (kk)
//   off0..off3 = the four FindRegionOffset results (rec+0/+4/+8 ; +40/+44/+48)
struct WaterPoly {
    u8  regionId;
    i32 cell;
    i32 col;
    i32 off0, off1, off2, off3;
    // ---- reconstruction helpers (NOT 32-bit-record fields) ------------------
    // The tile this poly belongs to (matches WaterStripSpan::tile); off0..off3
    // are FindRegionOffset results into THIS tile's point buffer (bufferBase 0,
    // so off/80 is the per-tile point index). The render arm forms the same two
    // triangles the engine writes (the 80-byte poly == two 40-byte sub-polys):
    //   tri0 = P(kk,prow), P(kk+1,prow+1), P(kk,prow+1)
    //          = off0/80,   off1/80,        off2/80
    //   tri1 = P(kk,prow), P(kk+1,prow),   P(kk+1,prow+1)
    //          = off0/80,   off3/80,        off1/80
    // (0x5bb50f.. : v87+0 = off(kk,prow); v87+4 = off(kk+1,prow+1); v87+8 =
    //  off(kk,prow+1); v87+40 = off(kk,prow); v87+44 = off(kk+1,prow); v87+48
    //  = off(kk+1,prow+1).)
    i32 tile = 0;
    // The four resolved per-tile point indices (off/80) for the corner lookups,
    // or -1 when FindRegionOffset returned 0 (no matching span — a boundary
    // corner the engine would have culled by the span point list).
    i32 pTL = -1;   // P(kk,   prow)    == off0/80
    i32 pBR = -1;   // P(kk+1, prow+1)  == off1/80
    i32 pBL = -1;   // P(kk,   prow+1)  == off2/80
    i32 pTR = -1;   // P(kk+1, prow)    == off3/80
};

// Injected water-texture loader (VIBE_Texture_LoadByName @0x5da714 +
// VIBE_Texture_UploadToSurface @0x5db234, present-coupled). Returns an opaque
// non-null handle for the loaded "EF_WASS_06A_2T_W_AN0" texture (stored at
// WaterMesh.float[0]/[1]), or 0 when no backend is present (headless build).
//   name  == "EF_WASS_06A_2T_W_AN0"
//   flags == 172 (0xAC; the original's VIBE_Texture_LoadByName(name,172,0,0)).
using WaterTextureLoadFn = void* (*)(const char* name, int flags, void* ctx);

// The full result of the builder — every grid pass output plus the geometry.
struct WaterRegions {
    int  n = 0;                      // grid edge N (Floor+0)
    bool anyWater = false;           // v215 — at least one water cell found
    u8   regionCount = 0;            // Floor+7277 — distinct flood-filled regions

    std::vector<u8> mask;            // pass 1: N*N dilated water mask (0xFF/0x00)
    std::vector<u8> heights;         // pass 2: N*N gradient-filled water heights
    std::vector<u8> edge;            // pass 3: N*N edge bitcode buffer (bit7 set/clear)
    std::vector<u8> regionGrid;      // pass 4: N*N region-id grid (0xFF=none)

    std::vector<WaterStripSpan> spans; // pass 5: compacted horizontal span records
    std::vector<WaterPoly>      polys; // pass 5: per-cell poly records

    // pass 5: the 344-byte WaterMesh array (Floor+0x19E0). One record per region;
    // sized regionCount*344 bytes. `meshTexture` is the loaded texture handle
    // written into each record's float[0]/[1] (0 in headless builds).
    std::vector<u8> waterMeshes;
    void*           meshTexture = nullptr;
};

// gilde.exe 0x5ba95c — VIBE_FloorWater_PrepareRegions (builder form).
//   waterMaskGrid : N*N per-cell water-type bytes (Floor+0x14, a1[5]). The cell
//                   at (col,row) is water iff waterMaskGrid[col + row*N]==waterType.
//   terrainHeights: N*N elevation bytes (Floor+0x10, a1[4]) seeding the gradient.
//   waterHeights  : N*N water-height bytes (Floor+0x18, a1[6]) or null/empty to
//                   allocate fresh (== a copy of the mask).
//   waterType     : the water-type byte (Floor+7276) cells are matched against.
//   n             : grid edge N. tileSpan := n/8 (the Floor+4 per-tile cell span).
//   loadTexture/ctx : the present-coupled water-texture loader (may be null).
// Returns the fully-built WaterRegions (the Floor+0x19E0/+0x1C6D contents the
// per-frame AnimateWaterVertices consumes). The traversal is the exact decompile
// (cell index x + y*N, wrap mask N*N-1, the per-row gradient seeding, the 4-bit
// edge code, the 2x2 interior region seed, the per-tile span/poly build).
WaterRegions BuildWaterRegions(const u8* waterMaskGrid, const u8* terrainHeights,
                               const u8* waterHeights, u8 waterType, int n,
                               WaterTextureLoadFn loadTexture = nullptr,
                               void* ctx = nullptr);

} // namespace guild::render
