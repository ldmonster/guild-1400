#include "render/morph_blend_walk.h"

#include "render/vertex_lighting.h"   // InterpolateMorphVertex (the per-vertex blend kernel)

#include <cstddef>

// =============================================================================
// guild::render — the multi-layer morph-blend walk of
// VIBE_Mesh_InterpolateMorphVertices @0x5c953c. See the header.
// =============================================================================
namespace guild::render {

namespace {
// Identity world matrix: InterpolateMorphVertex applies a world transform after the
// blend; the engine defers the world transform to the static tail, so we run the
// kernel through identity to get the BLEND ONLY, then accumulate.
const float kIdentityWorld[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
} // namespace

int AccumulateMorphBlend(Vertex* verts, int count, const MorphLayer* layers, int layerCount) {
    if (!verts || count <= 0 || !layers || layerCount <= 0)
        return 0;

    int applied = 0;
    bool first = true;
    for (int L = 0; L < layerCount; ++L) {
        const MorphLayer& ly = layers[L];
        // Engine gate: a clip (+104) present AND a non-zero weight (+64 & 0x7FFFFFFF).
        if (!ly.kf0.points || !ly.kf1.points || ly.layerWeight == 0.0f)
            continue;

        const float lw = ly.layerWeight;
        const float s0[3] = {ly.kf0.scale[0] * lw, ly.kf0.scale[1] * lw, ly.kf0.scale[2] * lw};
        const float b0[3] = {ly.kf0.bias[0] * lw,  ly.kf0.bias[1] * lw,  ly.kf0.bias[2] * lw};
        const float s1[3] = {ly.kf1.scale[0] * lw, ly.kf1.scale[1] * lw, ly.kf1.scale[2] * lw};
        const float b1[3] = {ly.kf1.bias[0] * lw,  ly.kf1.bias[1] * lw,  ly.kf1.bias[2] * lw};

        for (int i = 0; i < count; ++i) {
            // Quantized point bytes (engine reads unsigned byte, casts to i16 0..255).
            const i16 p0[3] = {(i16)ly.kf0.points[(std::size_t)i * 3 + 0],
                               (i16)ly.kf0.points[(std::size_t)i * 3 + 1],
                               (i16)ly.kf0.points[(std::size_t)i * 3 + 2]};
            const i16 p1[3] = {(i16)ly.kf1.points[(std::size_t)i * 3 + 0],
                               (i16)ly.kf1.points[(std::size_t)i * 3 + 1],
                               (i16)ly.kf1.points[(std::size_t)i * 3 + 2]};
            float blend[3];
            InterpolateMorphVertex(p0, s0, b0, p1, s1, b1, ly.w0, ly.w1, kIdentityWorld, blend);

            Vertex& v = verts[i];
            if (first) {            // first active layer SETS
                v.x = blend[0]; v.y = blend[1]; v.z = blend[2];
            } else {                // subsequent layers ACCUMULATE
                v.x += blend[0]; v.y += blend[1]; v.z += blend[2];
            }
        }
        first = false;
        ++applied;
    }
    return applied;
}

} // namespace guild::render
