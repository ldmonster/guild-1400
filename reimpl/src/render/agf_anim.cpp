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
//   Object state mapped to parameters: obj+0x00 -> fromFrame, obj+0x04 -> toFrame,
//   obj+0x08 -> phase, obj+0x64 -> offset, obj+0x6D bit1 (0x2) -> reverse, bit2
//   (0x4) -> ease. Frames live at clip+0x15C (192-byte stride, duration at +4);
//   frameCount at clip+0x148. All frame/count comparisons are SIGNED (jg/jle/jl).
//
//   Forward branch (flags&2 == 0, disasm 0x5c9475..0x5c9505):
//     inv = (float)(1.0 / (double)frames[fromFrame].dur)      (fstp @0x5c9496)
//     v   = (float)((double)phase * inv)                      (fstp @0x5c949f)
//     ease (flags&4):
//       tiny clip  (fc<=2 && fromFrame+1 >= fc-1): v = 1-(cos(v*pi)+1)*0.5
//       trailing   (fc-1 <= toFrame):              v = 1-(cos((v+1)*pi/2)+1)
//       leading    (fromFrame <= 0):               v = 1-cos(v*pi/2)
//       otherwise: no shaping (branch @0x5c9528 -> 0x5c94d9)
//     wTo (*a2) = (float)((double)inv*offset + v)  (fstp @0x5c94e7)
//     wFrom (*a3) = (float)(1.0 - wTo)
//   Reverse branch (flags&2, disasm 0x5c93af..0x5c944e) uses frames[toFrame].dur,
//   tests tiny/leading on toFrame and trailing on fromFrame, and yields
//     wFrom (*a3) = (float)(v - (double)inv*offset), wTo (*a2) = 1 - wFrom.
//   There is NO [0,1] clamp and NO zero-duration guard in the original.
MorphWeights ComputeMorphWeights(const AnimClip& clip, int fromFrame, int toFrame,
                                 int phase, bool ease, float offset, bool reverse) {
    MorphWeights w;
    if (!clip.valid || clip.FrameCount() <= 0) return w;
    const int fc = clip.FrameCount();
    const auto& frames = clip.anim.frames;
    // Memory-safety bound only (the original indexes raw memory); no value clamps.
    const int durIdx = reverse ? toFrame : fromFrame;
    if (durIdx < 0 || durIdx >= (int)frames.size()) return w;

    // fild dur; fld1; fdivrp -> 80-bit reciprocal, fstp to float.
    float inv = (float)(1.0 / (double)frames[(size_t)durIdx].duration);
    // fild phase; fmul inv -> 80-bit product, fstp to float.
    float v = (float)((double)phase * inv);

    if (reverse) {
        if (ease) {
            if (fc <= 2 && toFrame <= 0)          // 0x5c93e5/0x5c93ee -> 0x5c944f
                v = (float)(1.0 - (std::cos((double)v * kPi) + 1.0) * kHalf);
            else if (toFrame <= 0)                // 0x5c93f4 -> 0x5c9467
                v = (float)(1.0 - std::cos((double)v * kHalfPi));
            else if (fc - 1 <= fromFrame)         // 0x5c9403 -> 0x5c9407
                v = (float)(1.0 - (std::cos(((double)v + 1.0) * kHalfPi) + 1.0));
        }
        // fmul offset; fsubr v -> v - inv*offset at 80-bit, fstp float (0x5c9430).
        float vFrom = (float)((double)v - (double)inv * offset);
        w.wFrom = vFrom;
        w.wTo   = (float)(1.0 - vFrom);
    } else {
        if (ease) {
            if (fc > 2 || fromFrame + 1 < fc - 1) {   // 0x5c94ae/0x5c94b7
                if (fc - 1 <= toFrame)                // 0x5c9510 -> 0x5c9514
                    v = (float)(1.0 - (std::cos(((double)v + 1.0) * kHalfPi) + 1.0));
                else if (fromFrame <= 0)              // 0x5c9528 -> 0x5c952d
                    v = (float)(1.0 - std::cos((double)v * kHalfPi));
                // else: no shaping
            } else {                                  // tiny clip -> 0x5c94bb
                v = (float)(1.0 - (std::cos((double)v * kPi) + 1.0) * kHalf);
            }
        }
        // fmul offset; fadd v -> inv*offset + v at 80-bit, fstp float (0x5c94e7).
        float vTo = (float)((double)inv * offset + v);
        w.wTo   = vTo;
        w.wFrom = (float)(1.0 - vTo);
    }
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
