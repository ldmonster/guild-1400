#pragma once
#include "guild/common/types.h"
#include "render/snow.h" // SnowSystem / SnowFlake / SnowViewport
#include "render/rain.h" // RainSystem / RainDrop
#include <vector>

// =============================================================================
// guild::render — weather render kernels: build the D3D TLVERTEX draw buffer for
// the snow/rain particle systems from their projected screen positions.
//
//   0x42b5b0  VIBE_Snow_Render   (3 verts/flake triangle billboard + viewport clip)
//   0x429c38  VIBE_Rain_Render   (2 verts/drop line segment + viewport clip)
//
// FRAME FLOW (recovered): each render function (1) advances the system's fade
// counters by interpolating its [start..end] tick window against the global
// frame clock dword_62EB38, (2) integrates the particles via
// VIBE_Snow_UpdateFlake / VIBE_Rain_UpdateDrop, (3) brackets the D3D submission
// with BeginScene/SetBlendMode/EndScene, and (4) walks the particles, emitting
// per-visible-particle TLVERTEX records and flushing them to the device's
// DrawPrimitive (vtbl+112) in batches (snow: 192 verts as TRIANGLELIST=4; rain:
// 128 verts as LINELIST=2).
//
// The D3D device call (dword_64A320 vtbl) is a vendor leave — forward-declared
// and stubbed in tests. The RECOVERABLE, testable core reproduced here is the
// vertex-buffer BUILD: the per-particle viewport-clip test + the 8-float
// TLVERTEX layout (x, y, z, rhw, diffuse, specular, u, v) the original writes.
//
// CONSTANTS (get_bytes, bit-exact):
//   flt_611990 = 0.1      snow dt scale ((frame-last)*0.1)
//   flt_611994 = 0.025    snow z->screen-z fade   ((1-pz)*0.025)
//   flt_611998 = 0.5      snow billboard X midpoint
//   flt_611764 = 0.1      rain dt scale
//   flt_611768 = 0.0005000000237  rain count fade (1 - count*0.0005)
//   flt_61176C = 96.0     rain streak X projection
//   flt_611770 = 128.0    rain streak Y projection
//   flt_611774 = 0.025    rain z->screen-z fade
// =============================================================================
namespace guild::render {

// Recovered constants.
constexpr float kSnowDtScale  = 0.1f;     // flt_611990
constexpr float kSnowZFade    = 0.025f;   // flt_611994
constexpr float kSnowXMid     = 0.5f;     // flt_611998
constexpr float kRainDtScale  = 0.1f;     // flt_611764
constexpr float kRainCountFade= 0.0005000000237f; // flt_611768
constexpr float kRainStreakX  = 96.0f;    // flt_61176C
constexpr float kRainStreakY  = 128.0f;   // flt_611770
constexpr float kRainZFade    = 0.025f;   // flt_611774

// A TLVERTEX as the original lays it out (8 floats / 32 bytes, stride 28? — the
// DrawPrimitive call passes vertex size 0x1C=28; the original writes 8 dwords
// per vertex but advances the index by 8, so the last word overlaps the next
// vertex's first — a quirk preserved by emitting the 8 fields and letting the
// consumer read 28-byte stride). We expose all 8 named fields.
struct Tlvertex {
    float x;        // screen X
    float y;        // screen Y
    float z;        // depth (fade)
    float rhw;      // 1/w   (1.0f)
    u32   diffuse;  // packed colour (snow: 0x506E4FE4)
    u32   specular; // 0
    float u;        // texture U
    float v;        // texture V
};

// gilde.exe 0x42b5b0 — VIBE_Snow_Render (vertex build). For each flake whose head
// screen point (sx,sy) and tail (sx2,sy2) are inside the viewport, emit 3
// TLVERTEX forming a billboard triangle. Returns the vertex count appended to
// `out`. `z = (1 - pz) * 0.025` (flt_611994). Diffuse = 0x506E4FE4 (1348756580).
int BuildSnowVertices(const SnowSystem& sys, const SnowViewport& vp,
                      std::vector<Tlvertex>& out);

// gilde.exe 0x429c38 — VIBE_Rain_Render (vertex build). For each drop whose head
// (sx,sy) and tail (sx2,sy2) are inside the viewport, emit 2 TLVERTEX forming a
// streak line. Returns vertex count. `z = (1 - pz) * 0.025` (flt_611774). The
// drop colour packs the rain streak-length fixed value into the diffuse word.
int BuildRainVertices(const RainSystem& sys, const SnowViewport& vp,
                      std::vector<Tlvertex>& out, u32 diffuse);

// gilde.exe 0x42b5b0 / 0x429c38 head — fade counter interpolation. Given a
// [start,end] tick window, the current value at `now` linearly interpolates a..b
// when now is inside the window (clamped to the start value before it). Matches
// `result[3]*(now-start)/(end-start) + (end-now)*result[2]/(end-start)`.
i32 InterpolateFade(i32 start, i32 end, i32 valA, i32 valB, i32 now);

} // namespace guild::render
