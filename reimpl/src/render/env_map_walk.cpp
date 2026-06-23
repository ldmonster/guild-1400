#include "render/env_map_walk.h"

#include "render/vertex_lighting.h"   // ComputeEnvMapReflectionUv, UnpackSkinNormal

#include <cstddef>

// =============================================================================
// guild::render — the env-map reflection-UV walk of VIBE_Mesh_ComputeVertexLighting
// @0x5c9054. See the header. The per-vertex math is the reconstructed kernel; this
// is the object loop + the skinned/non-skinned normal source selection.
// =============================================================================
namespace guild::render {

int ComputeEnvMapVertexUvs(Vertex* verts, int count, const EnvMapWalkInputs& in) {
    if (!verts || count <= 0 || !in.m3x3)
        return 0;
    const bool skinned = (in.skinNormalBytes != nullptr);
    if (!skinned && in.vertexNormals == nullptr)
        return 0;

    int done = 0;
    for (int i = 0; i < count; ++i) {
        if (in.reflectiveFlags && in.reflectiveFlags[i] == 0)   // engine: gate on vertex +77
            continue;

        Vertex& v = verts[i];
        float normal[3];
        if (skinned) {
            // SKINNED: keyframe +184 byte triple -> unit normal (n = (b-128)*2/255).
            UnpackSkinNormal(in.skinNormalBytes + (std::size_t)i * 3, normal);
        } else {
            // NON-SKINNED: per-vertex source normal.
            normal[0] = in.vertexNormals[(std::size_t)i * 3 + 0];
            normal[1] = in.vertexNormals[(std::size_t)i * 3 + 1];
            normal[2] = in.vertexNormals[(std::size_t)i * 3 + 2];
        }

        const float pos[3] = {v.x, v.y, v.z};
        float uv[2];
        ComputeEnvMapReflectionUv(pos, normal, in.m3x3, uv);
        v.u = uv[0];   // vertex +32
        v.v = uv[1];   // vertex +36
        ++done;
    }
    return done;
}

} // namespace guild::render
