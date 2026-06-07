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

namespace guild::render {

// Drop record is identical in shape to SnowFlake (40 bytes, 10 floats):
//   [0..2] px,py,pz ; [3] size ; [4..5] dir weights ; [6..9] two screen points.
using RainDrop = SnowFlake;

struct RainSystem {
    i32 count = 0;        // dword[0]
    i32 capacity = 0;     // dword[1]
    RainDrop* drops = nullptr; // dword[4]  (rain stores list ptr at +0x10)
};

// gilde.exe 0x429098 — VIBE_Rain_Create (seed path). Fills `count` drop slots
// with RandNext()-jittered values. Allocation / camera snapshot out of scope.
void RainSeedDrops(RainSystem& sys);

// gilde.exe 0x4294d4 — VIBE_Rain_UpdateDrop (__userpurge a1@eax = system,
// a2 = dt). Integrates + clamps + wraps + projects every active drop.
void RainUpdateDrop(RainSystem& sys, float dt, const SnowCamera& cam,
                    const SnowViewport& vp);

} // namespace guild::render
