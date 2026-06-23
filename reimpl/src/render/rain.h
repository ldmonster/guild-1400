#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — rain weather particle system (gilde.exe, d3_sky.c / weather).
//
//   0x429098  VIBE_Rain_Create     (alloc + seed drops via RandNext)
//   0x4294d4  VIBE_Rain_UpdateDrop (per-frame integrate + clamp + wrap + project)
//
// Mirrors the snow system: a flat array of 40-byte (10-float) drop records. Each
// drop's normalized position integrates with wind+gravity transformed into
// camera space, is clamped to [-1000,1000] (reset to 0 on overflow — the
// original also logged a debug string there, dropped here), wrapped into the
// unit cube, then projected to a screen line segment (drop streak).
//
// RNG FIDELITY: Create seeds each drop with six crt::RandNext() draws in the
// order px,py,pz,size,vel4,vel5, scaled by 1/32767 (flt_6115E0). Truncation via
// render::TruncToward. See snow.h for the camera-coupling rationale; SnowCamera
// / SnowViewport are reused.
// =============================================================================
#include "render/snow.h" // SnowCamera, SnowViewport, SnowFlake-shaped record
#include "render/types.h" // Surface

namespace guild::render {

// Drop record is identical in shape to SnowFlake (40 bytes, 10 floats):
//   [0..2] px,py,pz ; [3] size ; [4..5] dir weights ; [6..9] two screen points.
using RainDrop = SnowFlake;

// ---------------------------------------------------------------------------
// Rain system header (gilde.exe 0x429098 VIBE_Rain_Create — alloc 0x60 = 96 B).
// Only the fields VIBE_Rain_UpdateDrop @0x4294d4 reads/writes are modelled, at
// their ORIGINAL byte offsets:
//   +0x00 [0]  count         active drop count (loop bound = *(int*)a1)
//   +0x04 [1]  capacity      allocated slots ( >=1 )   (dwords 0..3 all = count)
//   +0x10 [4]  drops         RainDrop* base
//   +0x1C [7]  windX         live wind direction X  (Create init 1.0)
//   +0x20 [8]  windZ         live wind direction Z  (Create init 0.0)
//   +0x40 [16] prevAnchor.x  rain's snapshot of the camera anchor  (+132)
//   +0x44 [17] prevAnchor.y                                        (+136)
//   +0x48 [18] prevAnchor.z                                        (+140)
//   +0x50 [20] prevEye.x      rain's snapshot of the camera eye     (+76)
//   +0x54 [21] prevEye.y                                            (+80)
//   +0x58 [22] prevEye.z                                            (+84)
// The +0x40/+0x50 blocks are per-frame camera-motion compensation: the integrator
// drifts each drop by (prevEye - cameraEye) transformed into camera space, then
// re-snapshots prevAnchor:=cameraAnchor / prevEye:=cameraEye for next frame.
// Create initialises prevAnchor:=cameraAnchor and prevEye:=cameraEye (0x4290..).
struct RainSystem {
    i32 count = 0;        // +0x00  [0]
    i32 capacity = 0;     // +0x04  [1]
    RainDrop* drops = nullptr; // +0x10 [4]
    float windX = 1.0f;   // +0x1C  [7]  (Create: 1.0)
    float windZ = 0.0f;   // +0x20  [8]  (Create: 0.0)
    float prevAnchor[3] = {0.0f, 0.0f, 0.0f}; // +0x40 [16..18]
    float prevEye[3]    = {0.0f, 0.0f, 0.0f}; // +0x50 [20..22]
};

// gilde.exe 0x429098 — VIBE_Rain_Create (seed path). Fills `count` drop slots
// with RandNext()-jittered values. Allocation / camera snapshot out of scope.
void RainSeedDrops(RainSystem& sys);

// gilde.exe 0x4294d4 — VIBE_Rain_UpdateDrop (__userpurge a1@eax = system,
// a2 = dt). Integrates + clamps + wraps + projects every active drop.
//
// Per frame, for the whole system it first computes:
//   * an anchor billboard offset `off` = 2.5 * eulerMatrix(anchor-eye)[col2],
//     using VIBE_Math_MatrixFromEuler on (prevAnchor - cameraAnchor);
//   * a camera-motion drift `drift` = (prevEye - cameraEye) * camMatrix * 0.0025;
//   * the wind basis vector `wind` = (windX,0,windZ) * camMatrix;
//   * the gravity basis vector `grav` = (0,-0.75,0) * camMatrix;
// then re-snapshots prevAnchor:=cameraAnchor, prevEye:=cameraEye. Each drop's
// velocity is `vel = d1*grav + d0*wind`; position integrates by
// `p += dt*vel + drift + off` (Z uses `drift.z*2.0` instead of off.z — the
// binary's deliberate asymmetry), is reset to 0 on |p|>1000, then wrapped into
// [-1,1) per axis. Finally each drop projects to a head point (sx,sy) and a
// velocity-scaled tail point (sx2,sy2) forming the streak segment. No float->int
// conversion occurs in the integrator (the only fild/fistp are on int viewport
// extents and the int drop count); ConvertX truncation applies only to the
// render colour pack (RainStreakDiffuse).
void RainUpdateDrop(RainSystem& sys, float dt, const SnowCamera& cam,
                    const SnowViewport& vp);

// ---------------------------------------------------------------------------
// gilde.exe 0x429c38 — VIBE_Rain_Render colour pack (head of the render loop).
// The per-frame streak diffuse word is derived from the active drop count:
//   f = 1.0 - count * 0.0005000000237       (flt_611768)
//   a = round(f * 96.0)                      (flt_61176C, frndint via 0x5c6b08)
//   b = round(f * 128.0)                     (flt_611770)
//   diffuse = 0x80000000 | (a<<16) | (b<<8) | b
// i.e. ARGB A=0x80, R=a, G=b, B=b — a dimming-with-density bluish-white streak.
// (Rounding is round-to-nearest; values are NOT clamped — the original lets a/b
//  exceed 255 only if count<0, which never happens.)
u32 RainStreakDiffuse(int count);

// ---------------------------------------------------------------------------
// gilde.exe 0x429c38 tail — software rasterisation of the projected streak
// field onto `surf`. The original submits a D3D LINELIST (DrawPrimitiveUP,
// type=2, FVF=452, stride=28) of the per-drop head->tail line segments produced
// by RainUpdateDrop, in batches of 128 verts. The present path is Vulkan/SDL in
// this port; here we reproduce the OBSERVABLE result — each visible drop drawn
// as a line (sx,sy)->(sx2,sy2) in the streak colour — directly onto the 16/32bpp
// software Surface via render::SurfaceDrawLine. The viewport clip is the same
// all-four-corners-inside test the original applies (and BuildRainVertices in
// particle_render.cpp). Returns the number of streaks drawn.
//   `vp` is the device viewport rect (dword_13ECE58/5C/60/64).
//   `diffuse` is RainStreakDiffuse(sys.count) (ARGB 0x80RRGGBB; alpha ignored on
//   the opaque software surface, matching the additive look as best the sw path
//   allows — the colour bytes are taken verbatim).
int RainRenderToSurface(const RainSystem& sys, const SnowViewport& vp,
                        u32 diffuse, Surface* surf);

} // namespace guild::render
