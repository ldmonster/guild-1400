#include "render/agf_anim.h"

#include <cmath>

namespace guild::render {

namespace {
// Recovered ease constants (VIBE_Anim_ComputeMorphWeights @0x5c9394):
//   flt_628CCC = 1.5707964  (pi/2)
//   flt_628CD0 = 3.1415927  (pi)
//   flt_628CD4 = 0.5
constexpr float kHalfPi = 1.5707963705062866f;
constexpr float kPi     = 3.1415927410125732f;
constexpr float kHalf   = 0.5f;

// Shared empty span for out-of-range frame requests.
const std::vector<float> kEmptyPoints;

inline int ClampIdx(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
} // namespace

const std::vector<float>& AnimClip::FramePoints(int f) const {
    if (f < 0 || f >= (int)anim.points.size()) return kEmptyPoints;
    return anim.points[(size_t)f];
}

// gilde.exe 0x5e450c — parse one inflated `.baf` member (delegates to the shared
// token parser so the grammar lives in exactly one place).
bool LoadAnimation(const u8* data, size_t size, const char* name, AnimClip& out,
                   u8 loadFlag) {
    out.valid = false;
    out.anim = Animation{};
    if (!LoadBinaryAnimation(data, size, name, loadFlag, out.anim)) return false;
    out.valid = out.anim.valid;
    return out.valid;
}

// gilde.exe 0x5c9394 — VIBE_Anim_ComputeMorphWeights.
//   The engine computes v = phase / segDuration (seg = frame[from].duration, read at
//   frame+4), optionally cosine-eases v at the clip extremes (flag bit 4), then
//   returns *a2 = 1 - v ("from" weight) and *a3 = v ("to" weight). Here `ease`
//   selects the inner-segment vs end-segment cosine shaping; the linear path (no
//   ease) is v = phase/seg straight.
MorphWeights ComputeMorphWeights(const AnimClip& clip, int fromFrame, int toFrame,
                                 int phase, bool ease) {
    MorphWeights w;
    if (!clip.valid || clip.FrameCount() <= 0) return w;
    int fc = clip.FrameCount();
    fromFrame = ClampIdx(fromFrame, 0, fc - 1);
    toFrame   = ClampIdx(toFrame, 0, fc - 1);

    // seg = the "from" frame's segment duration (engine: *(192*from + frames + 4)).
    int seg = clip.anim.frames.empty() ? 0 : clip.anim.frames[(size_t)fromFrame].duration;
    float v;
    if (seg <= 0) {
        v = 0.0f;
    } else {
        v = (float)phase / (float)seg;
    }

    if (ease) {
        // The engine cosine-eases the blend so it is C1 across segment joins:
        //   end-of-clip hold     -> (cos(v*pi)+1)*0.5
        //   leading half-segment -> cos(v*pi/2)
        //   trailing half-segment-> 1 - (cos((v+1)*pi/2)+1)
        // We use the symmetric mid-segment form (smoothstep-like) which matches the
        // common interior case ComputeMorphWeights takes for v in [0,1].
        v = (std::cos((v + 1.0f) * kPi) + 1.0f) * kHalf;
        (void)kHalfPi;
    }
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    w.wTo = v;
    w.wFrom = 1.0f - v;
    return w;
}

// gilde.exe 0x5c9394 + 0x5ca2fc — blend two morph frames into a posed mesh.
PosedMesh SamplePosedMeshSeg(const AnimClip& clip, int fromFrame, int toFrame, float wTo) {
    PosedMesh out;
    if (!clip.valid) return out;
    int nv = clip.VertexCount();
    if (nv <= 0) return out;

    const std::vector<float>& a = clip.FramePoints(fromFrame);
    const std::vector<float>& b = clip.FramePoints(toFrame);
    // Need at least the "from" frame populated; fall back to it when "to" is missing.
    const std::vector<float>& bb = (b.size() >= (size_t)nv * 3) ? b : a;
    if (a.size() < (size_t)nv * 3) return out;

    if (wTo < 0.0f) wTo = 0.0f;
    if (wTo > 1.0f) wTo = 1.0f;

    out.points.assign((size_t)nv * 3, 0.0f);
    float mn[3] = {1e30f, 1e30f, 1e30f};
    float mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < nv; ++i) {
        const float* pa = &a[(size_t)i * 3];
        const float* pb = &bb[(size_t)i * 3];
        float* po = &out.points[(size_t)i * 3];
        VectorLerp(pa, pb, wTo, po);
        for (int k = 0; k < 3; ++k) {
            if (po[k] < mn[k]) mn[k] = po[k];
            if (po[k] > mx[k]) mx[k] = po[k];
        }
    }
    for (int k = 0; k < 3; ++k) { out.bbMin[k] = mn[k]; out.bbMax[k] = mx[k]; }
    out.vertexCount = nv;
    out.valid = true;
    return out;
}

// gilde.exe 0x5c9394 + 0x5ca2fc — time-based posed-mesh sample.
PosedMesh SamplePosedMesh(const AnimClip& clip, float t, bool clamp) {
    PosedMesh out;
    if (!clip.valid || clip.FrameCount() <= 0) return out;
    int fc = clip.FrameCount();

    if (fc == 1) return SamplePosedMeshSeg(clip, 0, 0, 0.0f);

    if (clamp) {
        if (t < 0.0f) t = 0.0f;
        float last = (float)(fc - 1);
        if (t > last) t = last;
    } else {
        // Wrap modulo the loop range [start, end] (inclusive).
        int s = clip.StartFrame();
        int e = clip.EndFrame();
        if (e <= s) { s = 0; e = fc - 1; }
        float span = (float)(e - s) + 1.0f;  // wrap window length in frame units
        if (span <= 0.0f) span = (float)fc;
        float rel = std::fmod(t - (float)s, span);
        if (rel < 0.0f) rel += span;
        t = (float)s + rel;
        if (t > (float)(fc - 1)) t = (float)(fc - 1);
    }

    int from = (int)std::floor(t);
    if (from < 0) from = 0;
    if (from > fc - 1) from = fc - 1;
    int to = (from + 1 <= fc - 1) ? from + 1 : from;
    float frac = t - (float)from;  // [0,1)

    // Translate the fractional position into a phase against this segment's duration
    // so ComputeMorphWeights honours per-segment timing; for a unit-fraction the
    // resulting wTo == frac (seg cancels). Use the direct weight to avoid quantizing.
    out = SamplePosedMeshSeg(clip, from, to, frac);
    return out;
}

} // namespace guild::render
