#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — sky DOME MESH builder (gilde.exe d3_sky.c).
//
//   gilde.exe 0x5ef980 — VIBE_Sky_BuildDomeMesh  (__usercall eax=fn(sky@eax, rebuild@dl))
//
// This is the GEOMETRY builder for the sky object created by VIBE_Sky_Create
// @0x5efd28 (a 0xACC-byte record). It is distinct from the time-of-day sky
// COLOUR path in render/sky.{h,cpp} (VIBE_SkyColor_* @0x5b85e4..) — that path
// fills the clear colour; THIS builds the 6x8 lat/long vertex grid of the dome.
//
// Layout recovered from the disasm @0x5ef980 (no named UDT in the IDB):
//   * vertex array  : sky + 0x5FC, 16 bytes / vertex  (the @esi cursor; field
//                     +0 = ... , +0 stores the atan2 longitude at +0x5FC).
//   * texcoord array: sky base @ecx, 32 bytes / entry. Each entry writes
//       [-0x20] = U  (the running phi sum, flt_62C148-stepped column accumulator)
//       [-0x1C] = V  (the row value v21, flt_62C14C-stepped)
//       [-0x18] = 1.0f (3F800000)   (homogeneous / weight)
//       [-0x14] = 1.0f (3F800000)
//   * sky + 0xAA8 (field 681) is reset to -1 at entry (cached-state invalidate).
//
// The mesh is a 6 (row) x 8 (col) grid. For each row r in [0,6) the engine
// lerps two corner edges of the sky tile rect by tRow=r*0.2 (flt_62C14C), and
// for each col c in [0,8) lerps along that row by tCol=c*(1/7) (flt_62C148),
// normalizes the resulting direction and takes atan2(dir.x, dir.z) as the
// vertex's stored longitude. The U accumulator advances by uStep per column;
// the V value advances by vStep per row.
//
// The corner vectors (flt_13DCE00/10/20/30) and the sky rect (dword_13ECE58..64)
// are RUNTIME data (BSS, populated by scene load) — not statically recoverable;
// they are inputs to BuildDome().
// =============================================================================
namespace guild::render {

// Recovered float constants (verified via get_global_value):
//   flt_62C148 = 0x3E124925 = 1.0/7.0  (column phi/U step, 8 cols span 0..1)
//   flt_62C14C = 0x3E4CCCCD = 0.2      (row V step, 6 rows: 0,.2,.4,.6,.8,1.0)
constexpr float kSkyColStep = 0.14285714924335480f; // flt_62C148 (1/7)
constexpr float kSkyRowStep = 0.20000000298023224f; // flt_62C14C (1/5)

constexpr int kSkyDomeRows = 6; // outer loop count (v2 < 6)
constexpr int kSkyDomeCols = 8; // inner loop count (v3 < 8)

// One produced dome vertex (what BuildDome writes per grid cell).
struct SkyDomeVertex {
    // texcoord-array entry (32-byte stride in the binary; we keep the 4 written
    // floats; the remaining 16 bytes are untouched per cell).
    float u;       // [ecx-0x20]  running column accumulator (uStart + c*uStep)
    float v;       // [ecx-0x1C]  row value (vStart + r*vStep)  (== v21)
    float one0;    // [ecx-0x18]  3F800000 == 1.0f
    float one1;    // [ecx-0x14]  3F800000 == 1.0f
    // vertex-array entry (16-byte stride; only the longitude float is written
    // here, at +0x5FC in the live sky object).
    float longitude; // [esi+0x5FC]  atan2(dir.x, dir.z)
};

// Inputs to the dome build (the runtime BSS the original reads):
//   corners: the four edge vectors the row/col double-lerp interpolates between.
//     c30 = flt_13DCE30 (row start, hi)   c10 = flt_13DCE10 (row end,   hi)
//     c20 = flt_13DCE20 (row start, lo)   c00 = flt_13DCE00 (row end,   lo)
//   rect: dword_13ECE58/5C/60/64 (xmin,ymin,xmax,ymax) — used for uStep/vStep
//     and uStart/vStart, all integer pixel coords promoted to float.
struct SkyDomeInputs {
    float c30[3]; // flt_13DCE30  (a := VectorLerp(c30, c10, tRow)) start
    float c10[3]; // flt_13DCE10
    float c20[3]; // flt_13DCE20  (b := VectorLerp(c20, c00, tRow)) start
    float c00[3]; // flt_13DCE00
    i32   rectXmin; // dword_13ECE58
    i32   rectYmin; // dword_13ECE5C
    i32   rectXmax; // dword_13ECE60
    i32   rectYmax; // dword_13ECE64
};

// gilde.exe 0x5ef980 — build the dome's 6x8 vertex grid into `out` (row-major,
// 48 vertices). `out` must hold kSkyDomeRows*kSkyDomeCols entries. Returns the
// number of vertices written (48), or 0 when rebuild==false (the original's
// early-out: it only resets +0xAA8 and returns without touching geometry).
//
// This is the pure math core: same VectorLerp/VectorNormalize/Atan2 sequence,
// same uStep/vStep/uStart/vStart derivation, same 1.0f weights, as the binary.
int BuildDome(const SkyDomeInputs& in, bool rebuild, SkyDomeVertex* out);

} // namespace guild::render
