#include "render/anim.h"

namespace guild::render {

// gilde.exe 0x5ca2fc — VIBE_Math_VectorLerp
//   out[0] = (b[0]-a[0])*t + a[0];  out[1] = (b[1]-a[1])*t + a[1];
//   out[2] = t*(b[2]-a[2]) + a[2];  return a;
const float* VectorLerp(const float* a, const float* b, float t, float* out) {
    out[0] = (b[0] - a[0]) * t + a[0];
    out[1] = (b[1] - a[1]) * t + a[1];
    out[2] = t * (b[2] - a[2]) + a[2];
    return a;
}

// gilde.exe 0x5cbc10 — VIBE_Anim_InterpolateBoneFrame (translation accumulation)
//
// Reconstruction of the per-segment delta accumulation. Mirrors the original's
// three phases:
//   (1) leading partial segment from `fromFrame`, prorated by (1 - phaseNum/dur),
//   (2) the full inter-frame deltas for whole segments in (fromFrame, toFrame),
//   (3) the trailing partial segment at `toFrame`, prorated by the end phase.
// Per the original, when fromFrame >= toFrame the leading term is zero; when
// fromFrame == toFrame only the trailing prorated segment contributes, which with
// phaseNum == segLen yields the full keys[to+1]-keys[to] delta.
void AccumulateBoneTranslation(const BoneKeyframe* keys, int fromFrame, int toFrame,
                               int phaseNum, int segLen, float* out) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;

    // Phase 1: leading partial segment (original v30 = 1 - phaseNum/dur).
    if (fromFrame < toFrame) {
        int dur = segLen > 0 ? segLen : 1;
        float lead = 1.0f - (float)phaseNum / (float)dur;
        ax = (keys[fromFrame + 1].tx - keys[fromFrame].tx) * lead;
        ay = (keys[fromFrame + 1].ty - keys[fromFrame].ty) * lead;
        az = (keys[fromFrame + 1].tz - keys[fromFrame].tz) * lead;

        // Phase 2: whole segments (fromFrame+1 .. toFrame-1) added in full.
        for (int k = fromFrame + 1; k < toFrame; ++k) {
            ax += keys[k + 1].tx - keys[k].tx;
            ay += keys[k + 1].ty - keys[k].ty;
            az += keys[k + 1].tz - keys[k].tz;
        }
    }

    // Phase 3: trailing partial segment at toFrame (original v32 = phase/segLen).
    {
        int dur = segLen > 0 ? segLen : 1;
        float trail = (float)phaseNum / (float)dur;
        ax += (keys[toFrame + 1].tx - keys[toFrame].tx) * trail;
        ay += (keys[toFrame + 1].ty - keys[toFrame].ty) * trail;
        az += (keys[toFrame + 1].tz - keys[toFrame].tz) * trail;
    }

    out[0] = ax;
    out[1] = ay;
    out[2] = az;
}

} // namespace guild::render
