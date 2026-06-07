#include "render/mirror.h"

namespace guild::render {

// flt_62C3A0 = 2.0 — the reflection scale (0x62c3a0: 00 00 00 40).
static constexpr float kReflectScale = 2.0f;

// gilde.exe 0x5f6148 — reflect a point across the mirror plane.
void ReflectPointAcrossPlane(const float P[3], const MirrorPlane& m, float out[3]) {
    // v7 = -(P·n - d) * 2.0 ; out = P + v7·n
    float t = -(P[0] * m.nx + P[1] * m.ny + P[2] * m.nz - m.d) * kReflectScale;
    out[0] = t * m.nx + P[0];
    out[1] = t * m.ny + P[1];
    out[2] = t * m.nz + P[2];
}

// gilde.exe 0x5f6148 — VIBE_Mirror_ClipPolygonToPlanes box clip-cull.
bool ClipReflectedBox(const float* reflected, int cornerStrideFloats,
                      const MirrorClipPlane* planes, int planeCount,
                      float* runningNear, float* runningFar) {
    // For each plane, count corners that are OUTSIDE (n·P < plane.d). The original
    // breaks out of the corner loop on the FIRST inside corner; if it scanned all 8
    // without finding one inside (v11 >= 8), the mirror is fully culled.
    for (int p = 0; p < planeCount; ++p) {
        const MirrorClipPlane& pl = planes[p];
        int outside = 0;
        for (int c = 0; c < 8; ++c) {
            const float* P = reflected + c * cornerStrideFloats;
            // inside when n·P >= plane.d (the `>= v23[5]` test -> break).
            if (P[0] * pl.nx + P[1] * pl.ny + P[2] * pl.nz >= pl.d) break;
            ++outside;
        }
        if (outside >= 8) return false;   // fully outside this plane => culled
    }
    // Expand the running depth bounds over the reflected corners' z (the v18[j+2]
    // loop): flt_13FD168 = min, flt_13FCF3C = max.
    for (int c = 0; c < 8; ++c) {
        float z = reflected[c * cornerStrideFloats + 2];
        if (runningNear && z < *runningNear) *runningNear = z;
        if (runningFar && z > *runningFar) *runningFar = z;
    }
    return true;
}

// gilde.exe 0x5f637c — VIBE_Mirror_BuildMirroredGeometry reflected-poly append.
i32 AppendMirroredPolys(const Polygon* polys, i32 polyCount,
                        const u32* texSortId, u32 mirrorViewTexId,
                        const bool* anyVertexNear, DrawList* out) {
    // Remaining capacity clamp: min(dword_13ECE80 - dword_13FC770, polyCount).
    i32 remain = out->capacity - out->count;
    i32 limit = (remain <= polyCount) ? remain : polyCount;
    i32 appended = 0;
    for (i32 i = 0; i < limit; ++i) {
        const Polygon& p = polys[i];
        u32 tex = texSortId[i];
        if (tex == mirrorViewTexId) continue;        // skip the mirror surface itself
        if (!anyVertexNear[i]) continue;             // all verts beyond the near gate
        // Reflected signed screen area (winding flipped by the reflection):
        //   (x0-x2)*(y0-y1) > (x0-x1)*(y0-y2)
        const Vertex& a = *p.v0;
        const Vertex& b = *p.v1;
        const Vertex& c = *p.v2;
        bool noCull = (p.flags38 & 4) != 0;
        bool frontFacing = (a.screenX - c.screenX) * (a.screenY - b.screenY) >
                           (a.screenX - b.screenX) * (a.screenY - c.screenY);
        if (!noCull && !frontFacing) continue;
        DrawListEntry& e = out->entries[out->count];
        e.poly = const_cast<Polygon*>(&p);
        // sortKey = ((tex - base) >> 7) + 1 when textured (caller folds the >>7+1
        // into texSortId), else 0.
        e.sortKey = tex ? tex : 0u;
        ++out->count;
        ++appended;
    }
    return appended;
}

} // namespace guild::render
