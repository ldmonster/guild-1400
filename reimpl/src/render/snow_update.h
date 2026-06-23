#pragma once
#include "render/snow.h" // SnowSystem/SnowFlake/SnowCamera/SnowViewport (reused)

// =============================================================================
// guild::render — snow flake per-frame integrator (wave-21 companion module).
//
//   0x42a644  VIBE_Snow_UpdateFlake   (already reconstructed in snow.cpp)
//
// The faithful 1:1 reconstruction of VIBE_Snow_UpdateFlake lives in
// src/render/snow.cpp (function `SnowUpdateFlake`). To avoid an ODR clash this
// module does NOT redefine it; it re-exports the existing symbol and adds a
// deterministic golden-vector driver (`SnowGoldenStep`) used by the wave-21
// trajectory pin tests.
//
// Math recap (verified against the decompile @0x42a644, get_bytes constants
// @0x6117F0..0x61181C):
//   * gravity (0,-1,0) and the per-system velocity basis (sysVelX,0,sysVelZ) are
//     rotated into camera space by the active view rotation (a1+396..+436);
//   * the anchor billboard offset (0,0,1.9) is rotated by the Euler matrix built
//     from (anchor - eye);
//   * each flake's normalized position integrates  p += dt*vel + drift + off,
//     then wraps into [-1,1) on x/y (dbl_611804/-1.0, dbl_61180C/2.0,
//     dbl_611814/-2.0) and on z with its own increment (flt_6117F4/2.0) and a
//     raw-float-bit upper wrap (`>= 1065353216` == >= 1.0f, flt_61181C/-2.0);
//   * projection: proj = (maxHalf*3) / (pz*2 + 3); sx = px*proj + cx;
//     sy = cy - py*proj; tail = ((1-pz)*13.5 + 1)*size; sx2=sx+tail; sy2=sy+tail.
// =============================================================================
namespace guild::render {

// Re-export of the reconstructed integrator (defined in snow.cpp). Declared here
// for callers that include only snow_update.h; the body is shared, no ODR clash.
// (SnowUpdateFlake is already declared in snow.h — this is the same symbol.)

// Deterministic golden driver: seed a system of `count` flakes with crt::RandNext
// from the current RNG state (caller seeds it), run `steps` integration ticks of
// `dt` with the supplied camera/viewport, and leave the system's flake array as
// the result. Pure wrapper over SnowSeedFlakes + SnowUpdateFlake for the pin test.
void SnowGoldenStep(SnowSystem& sys, int steps, float dt, const SnowCamera& cam,
                    const SnowViewport& vp);

} // namespace guild::render
