#pragma once
#include "render/geometry_types.h"

// Mesh vertex transform + projection for the guild::render pipeline (d3_engine.c).
// This is the heart of the per-object geometry stage: it perspective-projects a
// mesh's vertices to screen space, computes each polygon's normal and backface
// flag, and appends visible (on-screen, front-facing) polygons to the global
// draw list with a light-index sort key.
//
//   VIBE_Mesh_ProjectVerticesToScreen @0x5c5120
//   VIBE_Math_TriangleNormal          @0x5cb824  (reused from util/math)
//
// SCOPE NOTE: this ends the geometry stage at the projected + appended draw list.
// The draw list is radix-sorted and rasterized by the separate raster module.
namespace guild::render {

// View parameters consumed by the projection, mirroring the per-call globals the
// original read from a2 (=v35) and file scope:
//   eye[0..2]    : *(v35+0..8)  camera/world origin subtracted from each vertex
//   invDepth[0..2]: 1.0 / *(v35+16..24) — per-axis perspective reciprocals;
//                   invDepth[1] (v30[4]) scales X, scaleY/scaleX below scale Y/light
//   biasX        : flt_628B94 == 0.875 (screen-x additive bias)
//   scaleY       : v31 — Y depth scale used for the light/shade index
//   scaleX       : v32 — secondary scale for the screen-y term
//   lightCap     : flt_628B98 == 254.0 (light index clamp ceiling)
//   screenW      : *(v35+32) — on-screen width clamp for the cull test
struct ProjectParams {
    float eye[3];
    float invDepth[3];  // 1/ *(v35+16), 1/ *(v35+20), 1/ *(v35+24)
    float biasX;        // flt_628B94 = 0.875
    float scaleY;       // v31
    float scaleX;       // v32
    float lightCap;     // flt_628B98 = 254.0
    float screenW;      // *(v35+32)
};

// A minimal, re-entrant draw-list sink replacing the global PolyList cursor
// (dword_13FC570) + count (dword_13FC770) + capacity (dword_13ECE80).
struct DrawList {
    DrawListEntry* entries;  // base (dword_13FC584 in the original)
    i32            count;    // dword_13FC770 (running, also used as start offset)
    i32            capacity; // dword_13ECE80 (max polys)
};

// gilde.exe 0x5c5120 — VIBE_Mesh_ProjectVerticesToScreen
//   (__usercall fn(object@eax, view@edx)). Faithful reconstruction:
//   1. For each vertex: screenX = (x-eye[0])*invDepth[1] + 0.875;
//      screenY = ((z-eye[2])*invDepth?) + 0.875 (see cpp for exact term order);
//      compute the per-vertex light/shade index from the depth term, clamped to
//      [1, lightCap] (254) and truncated toward zero (ConvertX), stored at +66.
//   2. For each polygon: compute the triangle normal; if |normal.y| >= 0.05 (or
//      the double-sided flag is set) compute the signed screen-space area; set the
//      backface bit (+36 bit7) accordingly; else mark fully culled (+36 |= 0xC0).
//   3. Front-facing polygons whose three projected vertices are all within
//      [0, screenW) are appended to `out` with sortKey = 768 * max(vertex light
//      index), up to the remaining capacity.
//   `objFlags530` is the object's +530 render-flags byte (0x10 force, 0x20, 0x40
//   double-sided). `viewCull42` is *(view+42) (the >>24 backface gate). Returns 1.
char ProjectVerticesToScreen(MeshGeometry* geom, const ProjectParams& p,
                             u8 objFlags530, i32 viewCull42, DrawList* out);

} // namespace guild::render
