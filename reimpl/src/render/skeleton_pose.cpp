#include "render/skeleton_pose.h"

#include "render/vertex_lighting.h"  // ComputeEnvMapReflectionUv, TransformPointByWorldMatrix
#include "util/math.h"               // VectorNormalize

namespace guild::render {

// The per-vertex +72 source block (*(vertex+72) in the original) is a distinct
// packed record: position @+0, normal @+12. The walks below read it through the
// MorphMeshBlock::sources parallel array (see skeleton_pose.h for why the 80-byte
// Vertex view can't host both the +72 pointer and the +77 flag).

namespace {
// Running-bounds update used by the attachClamp depth path (the flt_13FD168[0] /
// flt_13FCF3C running near/far clamps).
inline void TrackBounds(float z, float* outNear, float* outFar) {
    if (outNear && z < *outNear) *outNear = z;
    if (outFar && z > *outFar) *outFar = z;
}
} // namespace

// gilde.exe 0x5cd1d8 — per-track keyframe advance (forward playback core).
//   Mirrors the non-reverse branch: while phase >= duration(curFrame), subtract the
//   duration and step the frame via AdvanceFrameIndex; at the [first,last] boundary
//   apply the loop/clamp/hold mode and set boundary. The reverse (0x2) leg of the
//   original ping-pongs toward `first`; AdvanceFrameIndex already encodes both legs,
//   so we drive it with the live `mode` byte exactly as the engine does.
i32 AdvanceTrackPhase(TrackState& st, const i32* durations, i32 firstFrame,
                      i32 lastFrame, i32 frameCount) {
    int steps = 0;
    st.boundary = false;

    // Bound the loop defensively (the engine relies on monotonic phase consumption;
    // a degenerate zero-duration table could spin, so cap at frameCount*2 + 1).
    int guard = frameCount * 2 + 2;

    while (guard-- > 0) {
        i32 dur = durations[st.fromFrame];
        if (st.phase < dur)
            break;                       // phase fits within the current segment

        if (st.fromFrame + 1 < lastFrame) {
            // Mid-run: consume the segment and step forward one frame.
            st.phase -= dur;
            st.fromFrame += 1;
        } else {
            // At the end boundary: apply the mode.
            if ((st.mode & 0x10) != 0) {
                // Clamp-to-count one-shot: hold at the last segment, mark boundary.
                st.boundary = true;
                st.phase = (dur > 0) ? dur - 1 : 0;
                break;
            } else if ((st.mode & 1) != 0) {
                // Loop: flip into reverse (ping-pong) — set bit1, reflect the phase.
                st.mode |= 2;
                st.phase -= ((dur - 1) > 0 ? (dur - 1) : 0);
                st.fromFrame = st.toFrame;
            } else {
                // Hold: clear the boundary flag bit, wrap to first.
                st.phase -= dur;
                st.fromFrame = firstFrame;
            }
        }
        st.toFrame = AdvanceFrameIndex(st.mode, st.fromFrame, lastFrame, firstFrame,
                                       frameCount);
        ++steps;
    }
    return steps;
}

// gilde.exe 0x5c953c — non-morph vertex transform walk.
//   The original reads each vertex's +72 source-position pointer and writes the
//   world-transformed point to the vertex's +0/+4/+8. We surface the +72 source via
//   the Vertex's reserved region using a byte view so the math stays 1:1.
void TransformMeshVertices(MorphMeshBlock& mesh, const float* world,
                           bool attachClamp, float* outNear, float* outFar) {
    if (outNear) *outNear = 1.0e10f;   // flt_13FD168[0] reset analogue
    if (outFar)  *outFar  = -1.0e10f;  // flt_13FCF3C reset analogue

    for (i32 i = 0; i < mesh.vertexCount; ++i) {
        Vertex& v = mesh.vertices[i];
        // *(float**)(vertex+72) -> the source block's model-space position.
        const float* src = mesh.sources[i].pos;
        float out[3];
        TransformPointByWorldMatrix(src, world, out);
        v.x = out[0];
        v.y = out[1];
        v.z = out[2];
        if (attachClamp)
            TrackBounds(v.z, outNear, outFar);
    }
}

// gilde.exe 0x5c9054 — non-skinned env-map lighting walk.
//   For each lit vertex (+77 flag) reflect the position about the matrix-rotated
//   normal (read from the +72 source block's +12/+16/+20 normal) and write the env
//   UV to +32/+36. Mirrors the v6==0 branch verbatim via the per-vertex kernel.
void ComputeMeshVertexLightingNonSkinned(MorphMeshBlock& mesh, const float* m3x3) {
    for (i32 i = 0; i < mesh.vertexCount; ++i) {
        Vertex& v = mesh.vertices[i];
        u8 litFlag = mesh.litFlags ? mesh.litFlags[i] : 0;  // *(vertex+77)
        if (!litFlag) continue;
        float pos[3] = { v.x, v.y, v.z };
        const float* normal = mesh.sources[i].normal;  // source +12
        float uv[2];
        ComputeEnvMapReflectionUv(pos, normal, m3x3, uv);
        v.u = uv[0];
        v.v = uv[1];
    }
}

} // namespace guild::render
