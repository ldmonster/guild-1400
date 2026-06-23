#include "render/mesh_transform_walk.h"

#include "render/vertex_lighting.h"   // TransformPointByWorldMatrix (the per-vertex kernel)

// =============================================================================
// guild::render — the static object-vertex transform walk of
// VIBE_Mesh_InterpolateMorphVertices @0x5c953c. See the header.
// =============================================================================
namespace guild::render {

MeshTransformResult TransformMeshVerticesByMatrix(
    Vertex* verts, int count, const float world[16],
    bool trackDepth, float nearSeed, float farSeed) {
    MeshTransformResult r;
    r.nearZ = nearSeed;   // flt_13FD168[0] seed (1e10 in the engine)
    r.farZ  = farSeed;    // flt_13FCF3C seed (0 in the engine)
    if (!verts || count <= 0 || !world)
        return r;

    for (int i = 0; i < count; ++i) {
        Vertex& v = verts[i];
        const float src[3] = {v.x, v.y, v.z};   // engine: source from vertex +72 ptr
        float out[3];
        TransformPointByWorldMatrix(src, world, out);
        v.x = out[0];
        v.y = out[1];
        v.z = out[2];
        if (trackDepth) {
            // flt_13FD168[0] = min(flt_13FD168[0], z) ; flt_13FCF3C = max(flt_13FCF3C, z).
            if (out[2] < r.nearZ) r.nearZ = out[2];
            if (out[2] > r.farZ)  r.farZ  = out[2];
        }
        ++r.count;
    }
    return r;
}

} // namespace guild::render
