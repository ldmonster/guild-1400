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
//   DERIVED HELPER (not the 1:1 driver). The byte-for-byte reconstruction of the full
//   UpdateSkeletonPose @0x5cd1d8 inner while(1) state machine lives in
//   render/skeleton_pose_driver.cpp; that is the function in the live call tree. This
//   routine is a self-contained, narrowed model of the non-reverse forward branch of
//   that loop, kept for unit-level testing of the phase-consumption arithmetic.
//
//   Branch fidelity to the original forward leg (0x5cd1d8): while phase >= dur(curFrame)
//   step forward; at the [first,last] boundary the clamp leg sets phase = dur-1 and
//   marks boundary (original `*(v5+8) = v41 - 1`), the loop leg sets the reverse bit and
//   phase = dur-1-b (b = bit1 of the unmodelled +110 byte; 0 here), the hold leg does
//   phase -= dur and wraps to first. The +110 active-bit, the ComputeBoneDelta /
//   SampleBoneTranslation tolerance breaks, and the repeatCount(+108) decrement are part
//   of the full driver, not this narrowed helper. AdvanceFrameIndex (0x5ccf18) recomputes
//   toFrame each step exactly as the engine does.
i32 AdvanceTrackPhase(TrackState& st, const i32* durations, i32 firstFrame,
                      i32 lastFrame, i32 frameCount) {
    int steps = 0;
    st.boundary = false;

    // A track with no frames has nothing to advance — the engine only reaches this
    // path for an active header (frameCount > 0). Guard the degenerate 0-frame /
    // null-table case so the unbounded durations[fromFrame] read below can't run off
    // the buffer (faithful fail-safe; valid input is unaffected). [W11-ANIM]
    if (frameCount <= 0 || !durations)
        return 0;

    // Bound the loop defensively (the engine relies on monotonic phase consumption;
    // a degenerate zero-duration table could spin, so cap at frameCount*2 + 1).
    int guard = frameCount * 2 + 2;

    while (guard-- > 0) {
        // fromFrame is driven by AdvanceFrameIndex (always within [0,frameCount)),
        // but clamp the read index defensively to the table's bounds so malformed
        // boundary state cannot index past it. Matches skeleton_pose_driver's dur().
        i32 fi = (st.fromFrame >= 0 && st.fromFrame < frameCount) ? st.fromFrame : 0;
        i32 dur = durations[fi];
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
                // Original (0x5cd1d8 forward branch): `*(v5+8) = v41 - 1`, i.e. the
                // phase is set to dur-1 unconditionally (no dur>0 guard). Match it.
                st.boundary = true;
                st.phase = dur - 1;
                break;
            } else if ((st.mode & 1) != 0) {
                // Loop: flip into reverse (ping-pong) — set bit1, reflect the phase.
                // Original: `*(v5+8) -= ((mode110<<6)>>7) + *(v5+8) - dur + 1`, which
                // for b = bit1(*(v5+110)) collapses to `phase = dur - 1 - b`. The +110
                // active byte is not modelled here (b == 0 on a forward entry), so this
                // is phase = dur - 1 (a fixed value, NOT phase -= (dur-1)).
                st.mode |= 2;
                st.phase = dur - 1;
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
