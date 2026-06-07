#include "render/mirror_project.h"

namespace guild::render {

// flt_62C39C = 2.0f (0x40000000) — the reflection scale (same constant as
// flt_62C3A0 used by ReflectPointAcrossPlane).
static constexpr float kReflectScale = 2.0f;

// gilde.exe 0x5F6084 — VIBE_Mirror_ProjectReflectedVertices.
void ReflectAndProjectVertices(MirrorVertex* verts, int count,
                               const MirrorPlane& plane,
                               const ProjectionParams& proj) {
    for (int i = 0; i < count; ++i) {
        MirrorVertex& v = verts[i];
        // v5 = -(x*nx + y*ny + z*nz - d) * 2.0
        float t = -(v.x * plane.nx + v.y * plane.ny + v.z * plane.nz - plane.d)
                  * kReflectScale;                       // v12 = v5
        // Reflect in place: P' = P + t*n  (order: x, then y, then z).
        v.x = t * plane.nx + v.x;
        float vx = proj.projX * v.x;                     // v6 = flt_13FCD0C * x'
        v.y = t * plane.ny + v.y;
        float vy = proj.projY * v.y;                     // v7 = flt_13FCAF8 * y'
        v.z = t * plane.nz + v.z;
        float invZ = 1.0f / v.z;                         // v9 = 1.0 / z'
        // Outputs: screenX = vx*invZ + offX ; screenY = invZ*vy + offY ; t.
        v.t = t;                                         // *(v2-13)
        v.screenY = invZ * vy + proj.offY;               // v11 = v9*v7 + flt_13FCD10
        v.screenX = vx * invZ + proj.offX;               // v10 + flt_13FCD18
    }
}

} // namespace guild::render
