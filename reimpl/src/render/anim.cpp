#include "render/anim.h"

namespace guild::render {

// gilde.exe 0x5ca2fc — VIBE_Math_VectorLerp
//   out[0] = (b[0]-a[0])*t + a[0];  out[1] = (b[1]-a[1])*t + a[1];
//   out[2] = t*(b[2]-a[2]) + a[2];  return a;
// x87: each component is one fld/fsub/fmul/fadd chain at 80-bit precision with a
// single fstp to float (disasm 0x5ca301..0x5ca32a) — model with double.
const float* VectorLerp(const float* a, const float* b, float t, float* out) {
    out[0] = (float)(((double)b[0] - a[0]) * t + a[0]);
    out[1] = (float)(((double)b[1] - a[1]) * t + a[1]);
    out[2] = (float)((double)t * ((double)b[2] - a[2]) + a[2]);
    return a;
}

// gilde.exe 0x5cbc10 — VIBE_Anim_InterpolateBoneFrame (translation accumulation)
//
// Reconstruction of the per-segment delta accumulation. Mirrors the original's
// three phases (disasm 0x5cbc93..0x5cbeb3):
//   (1) leading partial segment from `fromFrame`, prorated by
//       v30 = (float)(1.0 - (double)curPhase / (double)keys[fromFrame].dur)
//       (fild/fild/fdivp + fld1/fsubrp at 80-bit, one fstp to float @0x5cbd0a),
//   (2) the full inter-frame deltas for whole segments in (fromFrame, toFrame),
//   (3) the trailing partial segment at `toFrame`, prorated by
//       v32 = (float)((double)num / (double)keys[toFrame].dur)  (fstp @0x5cbe2e)
//       where num = (fromFrame == toFrame) ? targetPhase - curPhase : targetPhase
//       (branch @0x5cbe08).
// The x/y deltas are stored to float before the multiply (fstp @0x5cbcec /
// 0x5cbd14 / 0x5cbe65 / 0x5cbe89); the z delta stays on the FPU stack at 80-bit
// through its multiply (fst @0x5cbd32 keeps it; fmulp @0x5cbea5) — modeled with
// double. There is no zero guard on the durations in the original.
void AccumulateBoneTranslation(const BoneKeyframe* keys, int fromFrame, int toFrame,
                               int curPhase, int targetPhase, float* out) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;

    // Phase 1: leading partial segment (v30 = 1 - curPhase/keys[from].dur).
    if (fromFrame < toFrame) {
        float lead = (float)(1.0 - (double)curPhase / (double)keys[fromFrame].dur);
        float dx = keys[fromFrame + 1].tx - keys[fromFrame].tx;
        float dy = keys[fromFrame + 1].ty - keys[fromFrame].ty;
        ax = dx * lead;
        ay = dy * lead;
        az = (float)(((double)keys[fromFrame + 1].tz - keys[fromFrame].tz) * lead);

        // Phase 2: whole segments (fromFrame+1 .. toFrame-1) added in full
        // (all three deltas stored to float before the add: fstp @0x5cbd8d/
        // 0x5cbdb2/0x5cbddb).
        for (int k = fromFrame + 1; k < toFrame; ++k) {
            ax += keys[k + 1].tx - keys[k].tx;
            ay += keys[k + 1].ty - keys[k].ty;
            az += keys[k + 1].tz - keys[k].tz;
        }
    }

    // Phase 3: trailing partial segment at toFrame.
    {
        int num = (fromFrame == toFrame) ? (targetPhase - curPhase) : targetPhase;
        float trail = (float)((double)num / (double)keys[toFrame].dur);
        float dx = keys[toFrame + 1].tx - keys[toFrame].tx;
        float dy = keys[toFrame + 1].ty - keys[toFrame].ty;
        ax += dx * trail;
        ay += dy * trail;
        az += (float)(((double)keys[toFrame + 1].tz - keys[toFrame].tz) * trail);
    }

    out[0] = ax;
    out[1] = ay;
    out[2] = az;
}

} // namespace guild::render
