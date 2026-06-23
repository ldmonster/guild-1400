#include "render/anim_relight.h"

#include <cstddef>

namespace guild::render {

// gilde.exe 0x5d0020 -> 0x5c9054 glue — apply a frame's recomputed normals to the
// per-vertex source blocks, then run the env-map lighting walk.
//
// The engine's deform pass, after morphing a frame's positions, also rewrites each
// vertex's +12 source normal (VIBE_Anim_CalculateAnimNormals's per-frame output) so
// that VIBE_Mesh_ComputeVertexLighting reflects about the CURRENT-frame normal rather
// than the stale rest-pose one. MorphMeshBlock::sources is a read-only view for the
// lighting walk; the underlying per-vertex source records (*(vertex+72)) are mutable
// in the engine, so we write the frame normal into the +12 slot in place before the
// walk reads it back — exactly the engine's deform->relight ordering.
void RelightPosedFrame(MorphMeshBlock& mesh, const std::vector<float>& frameNormals,
                       const float* m3x3) {
    if (mesh.sources != nullptr && mesh.vertexCount > 0) {
        // *(vertex+72)->+12 is mutable in the engine; the const view is only how the
        // lighting walk reads it. Copy the supplied per-vertex unit normals into +12.
        VertexSource* src = const_cast<VertexSource*>(mesh.sources);
        std::size_t supplied = frameNormals.size() / 3;          // whole vec3s available
        std::size_t count = static_cast<std::size_t>(mesh.vertexCount);
        if (supplied < count) count = supplied;
        for (std::size_t i = 0; i < count; ++i) {
            src[i].normal[0] = frameNormals[3 * i + 0];
            src[i].normal[1] = frameNormals[3 * i + 1];
            src[i].normal[2] = frameNormals[3 * i + 2];
        }
    }

    // Relight the whole posed frame with the now-current per-frame normals.
    ComputeMeshVertexLightingNonSkinned(mesh, m3x3);
}

} // namespace guild::render
