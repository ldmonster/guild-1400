// tests/integration/agf_anim_itest.cpp — REAL animation decode + posed-mesh sampler
// (integration). Builds a small synthetic "BGF\0" .baf with a known per-frame morph
// trajectory, loads it through LoadAnimation, samples a full sweep across the clip,
// and asserts the posed vertices move along the expected ramp (monotone) and that
// the posed bbox tracks the motion. No assets.
#include "test.h"

#include "render/agf_anim.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

bool feq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

struct Builder {
    std::vector<u8> b;
    void byte(u8 v) { b.push_back(v); }
    void u32v(u32 v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
                       b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff); }
    void i32v(i32 v) { u32v((u32)v); }
    void f32v(float f) { u32 bits; std::memcpy(&bits, &f, 4); u32v(bits); }
    void magic() { b.push_back('B'); b.push_back('G'); b.push_back('F'); b.push_back(0); }
};

// A small "walking" synthetic clip: 5 frames, 4 morph points. Point 0 slides +X
// over the whole clip (a known straight-line trajectory the sampler must reproduce
// as it sweeps t); the other points stay put.
std::vector<u8> BuildWalk() {
    const int nframes = 5, nv = 4;
    Builder w;
    w.magic();
    w.byte(48); w.u32v(1);
    w.byte(1);  w.u32v(0xABCD0001u);
    w.byte(35); w.i32v(nframes);
    w.byte(51); w.byte(1);
    w.byte(41); w.i32v(0);
    w.byte(42); w.i32v(nframes - 1);
    for (int f = 0; f < nframes; ++f) {
        w.byte(24); w.i32v(f * 8);   // uniform timing
        w.byte(25); w.i32v(nv);
        w.byte(33);
        for (int i = 0; i < nv; ++i) {
            if (i == 0) { w.f32v((float)f * 4.0f); w.f32v(0.0f); w.f32v(0.0f); }  // slides +X by 4/frame
            else        { w.f32v((float)i);        w.f32v(2.0f); w.f32v((float)-i); }
        }
        w.byte(40);
    }
    w.byte(47);
    return w.b;
}

} // namespace

// =============================================================================
// Load a synthetic clip, sweep t across every frame, assert the moving point's X
// increases monotonically and the static points never move.
// =============================================================================
TEST(AgfAnimItest, SweepPosedMesh) {
    auto buf = BuildWalk();
    AnimClip clip;
    bool ok = LoadAnimation(buf.data(), buf.size(), "walk.baf", clip, /*loadFlag*/0);
    CHECK(ok);
    CHECK_EQ(clip.FrameCount(), 5);
    CHECK_EQ(clip.VertexCount(), 4);

    // Sweep t in 0.25 steps across the clip; point 0's X must be non-decreasing and
    // strictly grow over the full span; points 1..3 stay fixed.
    float prevX = -1e30f;
    float firstX = 0.0f, lastX = 0.0f;
    bool firstSet = false;
    int samples = 0;
    for (float t = 0.0f; t <= 4.0001f; t += 0.25f) {
        PosedMesh m = SamplePosedMesh(clip, t);
        CHECK(m.valid);
        if (!m.valid || m.points.size() < 12) continue;
        ++samples;
        float x0 = m.points[0];
        CHECK(x0 >= prevX - 1e-3f);  // non-decreasing
        prevX = x0;
        if (!firstSet) { firstX = x0; firstSet = true; }
        lastX = x0;
        // static point 1 = (1, 2, -1) at every t.
        CHECK(feq(m.points[3], 1.0f));
        CHECK(feq(m.points[4], 2.0f));
        CHECK(feq(m.points[5], -1.0f));
        // all finite.
        for (float c : m.points) CHECK(std::isfinite(c));
    }
    CHECK(samples >= 16);
    // The moving point spans 0 -> 16 across the clip.
    CHECK(feq(firstX, 0.0f));
    CHECK(feq(lastX, 16.0f, 0.05f));
    CHECK(lastX - firstX > 10.0f);
}

// =============================================================================
// Mid-frame sample lands exactly on the expected lerp, and the posed bbox of the
// moving axis grows with t (the mesh's X extent tracks the moving point).
// =============================================================================
TEST(AgfAnimItest, BboxTracksMotion) {
    auto buf = BuildWalk();
    AnimClip clip;
    CHECK(LoadAnimation(buf.data(), buf.size(), "walk2.baf", clip, 0));

    PosedMesh early = SamplePosedMesh(clip, 0.5f);
    PosedMesh late  = SamplePosedMesh(clip, 3.5f);
    CHECK(early.valid);
    CHECK(late.valid);

    // Point 0 at t=0.5 = lerp(0, 4, 0.5) = 2 ; at t=3.5 = lerp(12,16,0.5) = 14.
    if (early.points.size() >= 1) CHECK(feq(early.points[0], 2.0f));
    if (late.points.size()  >= 1) CHECK(feq(late.points[0], 14.0f));

    // Max X grows as the moving point advances.
    CHECK(late.bbMax[0] > early.bbMax[0] + 5.0f);
    // The static cluster keeps min/max ordered.
    for (int k = 0; k < 3; ++k) {
        CHECK(early.bbMin[k] <= early.bbMax[k] + 1e-3f);
        CHECK(late.bbMin[k]  <= late.bbMax[k]  + 1e-3f);
    }
}
