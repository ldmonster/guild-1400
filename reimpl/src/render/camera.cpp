#include "render/camera.h"

#include "util/coord.h"  // ConvertX (x87 truncate-toward-zero)

namespace guild::render {

namespace {
// dbl_610784 == 0.5 (rounding bias added before truncation).
constexpr double kHalf = 0.5;
} // namespace

// gilde.exe 0x407428 — VIBE_Coord_ProjectPoint
//   v7 = point[0]-camera[0]; v9 = point[2]-camera[2];
//   v3 = 1.0 / camera[4];
//   out[0] = (int)trunc(v7*v3 + 0.5); out[1] = (int)trunc(0.5 + v3*v9);
// The two ConvertX calls truncate each (value + 0.5) toward zero.
void ProjectPoint(const float* camera, const float* point, i32* out) {
    double inv = 1.0 / camera[4];
    double sx = (double)(point[0] - camera[0]) * inv;
    double sy = inv * (double)(point[2] - camera[2]);
    out[0] = (i32)util::ConvertX(sx + kHalf);
    out[1] = (i32)util::ConvertX(kHalf + sy);
}

// gilde.exe 0x407488 — VIBE_Coord_ProjectFramePoint
//   if (!Heightmap_TileToWorld(...)) return 0;  ProjectPoint(...); return 1;
int ProjectFramePoint(const float* camera, int tileX, int tileY, i32* out,
                      TileToWorldFn tileToWorld, int param) {
    float world[5];
    if (!tileToWorld(tileX, tileY, world, param))
        return 0;
    ProjectPoint(camera, world, out);
    return 1;
}

} // namespace guild::render
