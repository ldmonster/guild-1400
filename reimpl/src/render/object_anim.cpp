// guild::render — object keyframe animation (Catmull-Rom tangents + Hermite). See header.
#include "render/object_anim.h"

namespace guild::render {

// gilde.exe 0x5ccf70 — VIBE_Anim_ComputeFrameTangents.
void ComputeFrameTangents(ObjAnimFrame& prev, ObjAnimFrame& mid, const ObjAnimFrame& next) {
    const float invPrev = 1.0f / prev.dur;     // 1.0 / *(int*)result
    const float invMid  = 1.0f / mid.dur;      // 1.0 / v18 (a2.dur)
    for (int c = 0; c < 3; ++c) {
        // pos: in-slope (prev->mid)/prev.dur, out-slope (mid->next)/mid.dur.
        const float inSlope  = (mid.pos[c] - prev.pos[c]) * invPrev;
        const float outSlope = (next.pos[c] - mid.pos[c]) * invMid;
        const float tan = (inSlope + outSlope) * kCatmullRomScale;
        mid.posOut[c]  = tan * mid.dur;        // a2[+32..] = tangent * mid.dur  (M0 of [mid,next])
        prev.posIn[c]  = tan * prev.dur;       // result[+48..] = tangent * prev.dur (M1 of [prev,mid])
    }
    for (int c = 0; c < 3; ++c) {
        const float inSlope  = (mid.rot[c] - prev.rot[c]) * invPrev;
        const float outSlope = (next.rot[c] - mid.rot[c]) * invMid;
        const float tan = (inSlope + outSlope) * kCatmullRomScale;
        mid.rotOut[c]  = tan * mid.dur;        // a2[+64..]
        prev.rotIn[c]  = tan * prev.dur;       // result[+76..]
    }
}

// gilde.exe 0x5cec40 — VIBE_Anim_BuildFrameTangents (non-looping path).
void BuildFrameTangents(std::vector<ObjAnimFrame>& f) {
    const int n = (int)f.size();
    for (int i = 0; i < n; ++i) {              // start clean (boundary frames stay 0)
        for (int c = 0; c < 3; ++c) {
            f[i].posOut[c] = f[i].posIn[c] = 0.0f;
            f[i].rotOut[c] = f[i].rotIn[c] = 0.0f;
        }
    }
    // Interior frames get the Catmull-Rom tangent (fills frame i's outTan + the
    // previous frame's inTan); frame 0's outTan and frame n-1's inTan stay zero,
    // so the spline eases out of the first waypoint and into the last.
    for (int i = 1; i < n - 1; ++i)
        ComputeFrameTangents(f[i - 1], f[i], f[i + 1]);
}

float ObjectAnimDuration(const std::vector<ObjAnimFrame>& f) {
    float t = 0.0f;
    for (std::size_t i = 0; i + 1 < f.size(); ++i) t += f[i].dur;
    return t;
}

void SampleObjectAnim(const std::vector<ObjAnimFrame>& f, float t,
                      float outPos[3], float outRot[3]) {
    if (f.empty()) { for (int c = 0; c < 3; ++c) outPos[c] = outRot[c] = 0.0f; return; }
    if (f.size() == 1) {
        for (int c = 0; c < 3; ++c) { outPos[c] = f[0].pos[c]; outRot[c] = f[0].rot[c]; }
        return;
    }
    const float total = ObjectAnimDuration(f);
    if (t <= 0.0f) t = 0.0f;
    if (t >= total) t = total;

    // Locate the segment [i, i+1] and the local parameter u in [0,1].
    int i = 0;
    float acc = 0.0f;
    for (; i + 1 < (int)f.size(); ++i) {
        if (t <= acc + f[i].dur || i + 2 == (int)f.size()) break;
        acc += f[i].dur;
    }
    const float seg = f[i].dur > 1e-9f ? f[i].dur : 1.0f;
    float u = (t - acc) / seg;
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;

    // Cubic Hermite basis.
    const float u2 = u * u, u3 = u2 * u;
    const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
    const float h10 = u3 - 2.0f * u2 + u;
    const float h01 = -2.0f * u3 + 3.0f * u2;
    const float h11 = u3 - u2;

    const ObjAnimFrame& A = f[i];
    const ObjAnimFrame& B = f[i + 1];
    for (int c = 0; c < 3; ++c) {
        // segment [i,i+1]: P0=A.pos, M0=A.posOut, P1=B.pos, M1=A.posIn.
        outPos[c] = h00 * A.pos[c] + h10 * A.posOut[c] + h01 * B.pos[c] + h11 * A.posIn[c];
        outRot[c] = h00 * A.rot[c] + h10 * A.rotOut[c] + h01 * B.rot[c] + h11 * A.rotIn[c];
    }
}

} // namespace guild::render
