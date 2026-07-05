// tests/unit/agf_anim_test.cpp — REAL animation (animations.BIN) decode + posed-mesh
// sampler (unit). Hand-built minimal "BGF\0" .baf token buffers crafted to the
// recovered grammar; asserts parsed frame/vertex counts, then samples at t and
// asserts the morph-point interpolation (lerp between two frames). No assets.
#include "test.h"

#include "render/agf_anim.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Little-endian .baf builder mirroring the on-disk token encoding the loader reads
// (VIBE_ModelIo_LoadBinaryAnimation token IDs from anim_load.h).
struct Builder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void i32v(i32 v) { u32v((u32)v); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// Build a minimal pure-morph animation: `nframes` frames, each with `nv` points.
// Frame f's point i = base(i) + f*step(i), so a known linear ramp the sampler must
// reproduce by lerp. Token order per the loader:
//   leading 4 magic bytes (consumed) ; 48 version ; 1 magic dword ; 35 frameCount ;
//   51 hasVertex=1 ; 41 start ; 42 end ; then per frame: 24 idx, 25 vtxCount,
//   33 points... 40 done ; finally 47 end.
std::vector<u8> BuildMorph(int nframes, int nv, int frameStride) {
    Builder w;
    w.magic();                 // the 4 bytes ReadStream consumes before token 48
    w.byte(48); w.u32v(1);     // version marker + version dword
    w.byte(1);  w.u32v(0xABCD0001u);  // magic guard (<= max)
    w.byte(35); w.i32v(nframes);      // frame count
    w.byte(51); w.byte(1);            // hasVertex = 1
    w.byte(41); w.i32v(0);            // start frame
    w.byte(42); w.i32v(nframes - 1);  // end frame
    for (int f = 0; f < nframes; ++f) {
        w.byte(24); w.i32v(f * frameStride);  // frame time (drives segment duration)
        w.byte(25); w.i32v(nv);               // vertex count
        w.byte(33);                            // points
        for (int i = 0; i < nv; ++i) {
            // base = (i+1, 0, -i) ; step per frame = (10, i, 0)
            w.f32v((float)(i + 1) + (float)f * 10.0f);
            w.f32v((float)0       + (float)f * (float)i);
            w.f32v((float)(-i)    + (float)f * 0.0f);
        }
        w.byte(40);  // done
    }
    w.byte(47);      // end-of-chunk
    return w.b;
}

} // namespace

// =============================================================================
// Parse: counts recovered from a hand-built buffer.
// =============================================================================
TEST(AgfAnimUnit, ParseCounts) {
    auto buf = BuildMorph(/*nframes*/4, /*nv*/3, /*stride*/10);
    AnimClip clip;
    bool ok = LoadAnimation(buf.data(), buf.size(), "synth.baf", clip, /*loadFlag*/0);
    CHECK(ok);
    CHECK(clip.valid);
    CHECK_EQ(clip.FrameCount(), 4);
    CHECK_EQ(clip.VertexCount(), 3);
    CHECK_EQ(clip.StartFrame(), 0);
    CHECK_EQ(clip.EndFrame(), 3);
    // Each frame's point array is vertexCount*3 floats.
    const std::vector<float>& p0 = clip.FramePoints(0);
    CHECK_EQ((int)p0.size(), 9);
    // Frame 0 vertex 0 = base (1, 0, 0).
    if (p0.size() >= 3) {
        CHECK(feq(p0[0], 1.0f));
        CHECK(feq(p0[1], 0.0f));
        CHECK(feq(p0[2], 0.0f));
    }
    // Out-of-range frame request -> empty.
    CHECK_EQ((int)clip.FramePoints(99).size(), 0);
    CHECK_EQ((int)clip.FramePoints(-1).size(), 0);
}

// =============================================================================
// Bad / truncated buffers are rejected (matching the loader early-returns).
// =============================================================================
TEST(AgfAnimUnit, RejectBad) {
    AnimClip clip;
    CHECK(!LoadAnimation(nullptr, 0, "x", clip));
    u8 junk[8] = {0};
    CHECK(!LoadAnimation(junk, sizeof(junk), "x", clip));
    CHECK(!clip.valid);
}

// =============================================================================
// W11 hardening: malformed / truncated / oversized .baf inputs. Each must fail
// safe or clamp, with no over-read / no runaway allocation. ASAN+UBSAN-checked.
// =============================================================================

// ---- oversized frame count (declared huge, no body) -> reject, no OOM -------
TEST(AgfAnimUnit, OversizedFrameCount) {
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(35); w.i32v(1000000000);  // ~1e9 frames in a ~15-byte file
    // No frame bodies follow.
    AnimClip clip;
    // The frame-count bound rejects this before the 192*N allocation.
    CHECK(!LoadAnimation(w.b.data(), w.b.size(), "huge.baf", clip, 0));
    CHECK(!clip.valid);
}

// ---- negative frame count -> reject ----------------------------------------
TEST(AgfAnimUnit, NegativeFrameCount) {
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(35); w.i32v(-5);
    AnimClip clip;
    CHECK(!LoadAnimation(w.b.data(), w.b.size(), "neg.baf", clip, 0));
}

// ---- oversized per-frame vertex count -> no runaway alloc / no OOB ----------
TEST(AgfAnimUnit, OversizedVertexCount) {
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(35); w.i32v(1);            // 1 frame
    w.byte(51); w.byte(1);            // hasVertex
    w.byte(41); w.i32v(0);
    w.byte(42); w.i32v(0);
    w.byte(24); w.i32v(0);            // frame time
    w.byte(25); w.i32v(1000000000);  // claim 1e9 verts
    w.byte(33);                       // points: only a couple of floats follow
    w.f32v(1.0f); w.f32v(2.0f);
    AnimClip clip;
    // Must not crash / OOM: the point alloc is bounded by the buffer size, and the
    // write loop is guarded. Result may be valid (clamped) or rejected — either is
    // memory-safe; we only require no ASAN report and a defined return.
    bool ok = LoadAnimation(w.b.data(), w.b.size(), "verts.baf", clip, 0);
    CHECK(ok == clip.valid);   // self-consistent, no UB
}

// ---- truncated mid-points (frameCount says 2, second frame's data missing) --
TEST(AgfAnimUnit, TruncatedMidFrame) {
    auto good = BuildMorph(/*nframes*/2, /*nv*/3, /*stride*/10);
    // Lop off the final ~third so the second frame's point stream is incomplete.
    std::vector<u8> bad(good.begin(), good.begin() + good.size() * 2 / 3);
    AnimClip clip;
    // The streaming reader stops at EOF; a partial frame must not over-read.
    bool ok = LoadAnimation(bad.data(), bad.size(), "trunc.baf", clip, 0);
    CHECK(ok == clip.valid);   // no UB regardless of success
}

// ---- bad magic guard (token 1 value > kMagicMax) -> reject -----------------
TEST(AgfAnimUnit, BadMagicGuard) {
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(1);  w.u32v(0xFFFFFFFFu);  // > 0xABCD0001 -> rejected
    w.byte(35); w.i32v(1);
    AnimClip clip;
    CHECK(!LoadAnimation(w.b.data(), w.b.size(), "magic.baf", clip, 0));
}

// =============================================================================
// ComputeMorphWeights: linear weight pair w/1-w = phase/seg, clamped.
// =============================================================================
TEST(AgfAnimUnit, MorphWeights) {
    auto buf = BuildMorph(/*nframes*/3, /*nv*/2, /*stride*/10);  // seg duration 10
    AnimClip clip;
    CHECK(LoadAnimation(buf.data(), buf.size(), "w.baf", clip, 0));
    // frame[0].duration is the post-pass *3 of (time[1]-time[0]) = 10 -> 30.
    int seg = clip.anim.frames.empty() ? 0 : clip.anim.frames[0].duration;
    CHECK(seg > 0);
    // phase = half a segment -> wTo ~ 0.5.
    MorphWeights mw = ComputeMorphWeights(clip, 0, 1, seg / 2, /*ease*/false);
    CHECK(feq(mw.wTo, 0.5f, 0.02f));
    CHECK(feq(mw.wFrom, 1.0f - mw.wTo));
    // phase 0 -> all "from".
    MorphWeights z = ComputeMorphWeights(clip, 0, 1, 0, false);
    CHECK(feq(z.wTo, 0.0f));
    CHECK(feq(z.wFrom, 1.0f));
    // Phase beyond seg does NOT clamp: gilde.exe 0x5c9394 has no [0,1] clamp
    // (wTo = phase/dur straight; old pin of 1.0 was an invented clamp).
    MorphWeights f = ComputeMorphWeights(clip, 0, 1, seg * 4, false);
    CHECK(feq(f.wTo, 4.0f));
    CHECK(feq(f.wFrom, 1.0f - 4.0f));
    // Ease shaping (flags bit 0x4): interior segments are NOT shaped (branch
    // 0x5c9528 -> 0x5c94d9 goes straight to the weight store). With 3 frames,
    // from=1, to=1 (fc-1 > toFrame fails? fc-1=2 > 1 -> then fromFrame>0 -> no
    // shaping): wTo stays phase/dur.
    MorphWeights mid = ComputeMorphWeights(clip, 1, 1, seg / 2, /*ease*/true);
    int seg1 = clip.anim.frames[1].duration;
    CHECK(feq(mid.wTo, (float)((double)(seg / 2) * (float)(1.0 / (double)seg1))));
    // Leading segment (fromFrame <= 0, fc-1 > toFrame): wTo = 1 - cos(v*pi/2).
    MorphWeights lead = ComputeMorphWeights(clip, 0, 1, seg / 2, /*ease*/true);
    {
        float inv = (float)(1.0 / (double)seg);
        float v = (float)((double)(seg / 2) * inv);
        CHECK(feq(lead.wTo, (float)(1.0 - std::cos((double)v * 1.5707963705062866))));
    }
}

// =============================================================================
// SamplePosedMesh: interpolation between frames. With base(i)+f*step(i), sampling
// at integer t reproduces the frame exactly; at t=mid the point is the lerp.
// =============================================================================
TEST(AgfAnimUnit, SampleInterpolation) {
    const int nv = 3;
    auto buf = BuildMorph(/*nframes*/3, nv, /*stride*/10);
    AnimClip clip;
    CHECK(LoadAnimation(buf.data(), buf.size(), "s.baf", clip, 0));

    // At t=0 the posed mesh == frame 0 exactly.
    PosedMesh m0 = SamplePosedMesh(clip, 0.0f);
    CHECK(m0.valid);
    CHECK_EQ(m0.vertexCount, nv);
    const std::vector<float>& p0 = clip.FramePoints(0);
    bool eq0 = (m0.points.size() == p0.size());
    if (eq0) for (size_t k = 0; k < p0.size(); ++k) eq0 = eq0 && feq(m0.points[k], p0[k]);
    CHECK(eq0);

    // At t=1 the posed mesh == frame 1 exactly.
    PosedMesh m1 = SamplePosedMesh(clip, 1.0f);
    const std::vector<float>& p1 = clip.FramePoints(1);
    bool eq1 = (m1.points.size() == p1.size());
    if (eq1) for (size_t k = 0; k < p1.size(); ++k) eq1 = eq1 && feq(m1.points[k], p1[k]);
    CHECK(eq1);

    // At t=0.5 vertex 0 = lerp(frame0.v0, frame1.v0, 0.5).
    PosedMesh mh = SamplePosedMesh(clip, 0.5f);
    CHECK(mh.valid);
    if (mh.points.size() >= 3 && p0.size() >= 3 && p1.size() >= 3) {
        CHECK(feq(mh.points[0], 0.5f * (p0[0] + p1[0])));
        CHECK(feq(mh.points[1], 0.5f * (p0[1] + p1[1])));
        CHECK(feq(mh.points[2], 0.5f * (p0[2] + p1[2])));
    }

    // Explicit segment+weight form agrees with the time form.
    PosedMesh seg = SamplePosedMeshSeg(clip, 0, 1, 0.5f);
    CHECK(seg.valid);
    bool agree = (seg.points.size() == mh.points.size());
    if (agree) for (size_t k = 0; k < seg.points.size(); ++k) agree = agree && feq(seg.points[k], mh.points[k]);
    CHECK(agree);

    // Posed bbox is finite and ordered (min <= max).
    for (int k = 0; k < 3; ++k) CHECK(mh.bbMin[k] <= mh.bbMax[k] + 1e-3f);
}

// =============================================================================
// Clamp vs wrap at the clip ends.
// =============================================================================
TEST(AgfAnimUnit, ClampAndWrap) {
    auto buf = BuildMorph(/*nframes*/4, /*nv*/2, /*stride*/10);
    AnimClip clip;
    CHECK(LoadAnimation(buf.data(), buf.size(), "c.baf", clip, 0));

    // Clamp past the end == last frame.
    PosedMesh past = SamplePosedMesh(clip, 99.0f, /*clamp*/true);
    PosedMesh last = SamplePosedMesh(clip, 3.0f, true);
    bool same = (past.points.size() == last.points.size());
    if (same) for (size_t k = 0; k < last.points.size(); ++k) same = same && feq(past.points[k], last.points[k]);
    CHECK(same);

    // Negative time clamps to frame 0.
    PosedMesh neg = SamplePosedMesh(clip, -5.0f, true);
    PosedMesh zero = SamplePosedMesh(clip, 0.0f, true);
    bool same0 = (neg.points.size() == zero.points.size());
    if (same0) for (size_t k = 0; k < zero.points.size(); ++k) same0 = same0 && feq(neg.points[k], zero.points[k]);
    CHECK(same0);

    // Wrap mode produces a valid mesh past the end (loops back into range).
    PosedMesh wrapped = SamplePosedMesh(clip, 5.0f, /*clamp*/false);
    CHECK(wrapped.valid);
    CHECK_EQ(wrapped.vertexCount, 2);
}
