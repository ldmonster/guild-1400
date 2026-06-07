#pragma once
#include "guild/common/types.h"

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

} // namespace guild::render
