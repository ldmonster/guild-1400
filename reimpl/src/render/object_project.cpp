#include "render/object_project.h"

// =============================================================================
// guild::render — the per-object perspective project + backface cull extracted
// from gilde.exe 0x5ac970 (the IDB names it VIBE_Particle_UpdateBillboards). This
// reconstructs two self-contained arms of that function, verified against disasm:
//   * the simple per-vertex projection arm @loc_5ACB5B (byte_649DD8 == 0 branch):
//       gate test [edx+4Ch]&0x80 ; invZ = 1/[edx+8] ;
//       [edx+10h] = flt_13FCD0C*[edx]*invZ + flt_13FCD18   (screenX)
//       [edx+14h] = flt_13FCAF8*[edx+4]*invZ + flt_13FCD10 (screenY)
//     (the alpha/ConvertX byte_649DD8 != 0 arm is NOT modelled here — out of scope);
//   * the per-polygon backface cull @loc_5ACAF6 (signed projected-area test, the
//     flags +36/+38 logic). Both are pure float math (no float->int sites).
// =============================================================================
namespace guild::render {

int ProjectObjectVertices(Vertex* verts, int count, Polygon* polys, int polyCount,
                          const ObjectProjectScalars& s, bool projectAll) {
    int projected = 0;

    // ---- per-vertex perspective projection (front-flagged vertices) ----
    if (verts && count > 0) {
        for (int i = 0; i < count; ++i) {
            Vertex& v = verts[i];
            // Gate: *(v4+76) < 0 (the sign bit set by ComputeVertexClipFlags).
            if (!projectAll && (v.clipFlags & 0x80) == 0)
                continue;
            if (v.z == 0.0f)            // guard the 1/z divide (engine assumes z != 0)
                continue;
            const float invZ = 1.0f / v.z;
            v.screenX = s.xScale * v.x * invZ + s.xOffset;   // +16
            v.screenY = s.yScale * v.y * invZ + s.yOffset;   // +20
            ++projected;
        }
    }

    // ---- per-polygon backface cull by projected signed area ----
    if (polys && polyCount > 0) {
        for (int p = 0; p < polyCount; ++p) {
            Polygon& poly = polys[p];
            if ((poly.flags36 & 0x80) == 0)        // only front-candidates (sign bit set)
                continue;
            if (poly.flags36 & 0x10) {             // (v20 & 0x10): mark +0x40, keep
                poly.flags36 = (u8)(poly.flags36 | 0x40);
                continue;
            }
            if (!poly.v0 || !poly.v1 || !poly.v2)
                continue;
            const Vertex* a = poly.v0;
            const Vertex* b = poly.v1;
            const Vertex* c = poly.v2;
            // (v0.sx - v2.sx)*(v0.sy - v1.sy) > (v0.sx - v1.sx)*(v0.sy - v2.sy)  -> back.
            const float lhs = (a->screenX - c->screenX) * (a->screenY - b->screenY);
            const float rhs = (a->screenX - b->screenX) * (a->screenY - c->screenY);
            if (lhs > rhs && (poly.flags38 & 4) == 0)
                poly.flags36 = (u8)(poly.flags36 & 0x7F);   // clear sign bit (cull)
        }
    }
    return projected;
}

} // namespace guild::render
