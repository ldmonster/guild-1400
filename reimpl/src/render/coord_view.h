#pragma once
#include "guild/common/types.h"
#include "render/heightmap.h"

// =============================================================================
// guild::render — small world/coordinate helper around the camera that the view
// setup path uses. Faithful 1:1 reconstruction of:
//
//   0x4525b4  VIBE_Coord_ComputeViewScale  (clamp the camera zoom to [5,16])
//
// This sits next to render/camera.{h,cpp} (the perspective ProjectPoint).
//
// NOTE: VIBE_Coord_Distance3D (0x4865b4) — world distance between two terrain
// tiles — is ALREADY translated as guild::sim::CoordDistance3D in
// src/sim/combat_escape.{h,cpp}; per the ODR/reuse rule it is NOT redefined here.
// The cull tests reuse that existing definition.
// =============================================================================
namespace guild::render {

// gilde.exe 0x4525b4 — VIBE_Coord_ComputeViewScale (__spoils<ecx,st0>).
//   v0 = flt_641DAC * dbl_619130;            // dbl_619130 == 0.006
//   trunc(v0) toward zero (ConvertX);
//   if (v0 < 5) return 5; if (v0 > 16) return 16; return (int)v0;
// `zoom` is the camera zoom magnitude (flt_641DAC). Returns the integer view scale
// clamped to [5,16]. (kViewScaleFactor == dbl_619130 == 0.006.)
constexpr double kViewScaleFactor = 0.006;
i32 ComputeViewScale(float zoom);

} // namespace guild::render
