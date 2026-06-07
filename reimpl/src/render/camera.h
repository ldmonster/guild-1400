#pragma once
#include "render/geometry_types.h"

// Camera / point-projection primitives for the guild::render 3D pipeline
// (d3_engine.c / coord helpers). These are the small perspective-divide routines
// every geometry path funnels through.
//
//   VIBE_Coord_ProjectPoint      @0x407428  (project a single world point)
//   VIBE_Coord_ProjectFramePoint @0x407488  (tile->world->screen wrapper)
//
// The original truncated the projected coordinate toward zero via
// VIBE_Coord_ConvertX (x87 FRNDINT, RC=truncate) — modeled here with
// guild::util::ConvertX (== std::trunc) so the integer screen coords are bit-exact.
namespace guild::render {

// gilde.exe 0x407428 — VIBE_Coord_ProjectPoint
//   (__usercall fn(camera@eax, point@edx, out@ebx)). Perspective-divides `point`
// against the camera:
//     inv   = 1.0 / camera[4]              ; camera[4] = view depth
//     out[0]= trunc((point[0]-camera[0]) * inv + 0.5)
//     out[1]= trunc((point[2]-camera[2]) * inv + 0.5)
// Note the *world* X and Z map to screen x,y (Y is the up axis, dropped here).
// The +0.5 bias is dbl_610784. Writes two ints to `out`.
void ProjectPoint(const float* camera, const float* point, i32* out);

// gilde.exe 0x407488 — VIBE_Coord_ProjectFramePoint
//   (__fastcall fn(camera, tileX, out, tileY) — register-mapped in the original).
// Converts a tile coordinate to a world point via the heightmap, then projects it.
// Returns 0 if the tile->world conversion failed (off the map), else 1 with the
// projected screen coords written to `out`.
//
// The heightmap tile->world conversion (VIBE_Heightmap_TileToWorld) belongs to the
// terrain module; we take it as a function-pointer dependency so this stays
// self-contained and testable.
using TileToWorldFn = bool (*)(int tileX, int tileY, float* worldOut, int param);
int ProjectFramePoint(const float* camera, int tileX, int tileY, i32* out,
                      TileToWorldFn tileToWorld, int param);

} // namespace guild::render
