#include "test.h"
#include "render/animation_playback.h"
#include "render/skeleton.h"   // real AdvanceFrameIndex / InterpolateBoneFrame

#include <cstring>
#include <cstdint>
#include <cmath>

using namespace guild;
using namespace guild::render;

namespace guild::render {
extern int g_animObjDirtyCalls;
}
namespace guild::sim { extern u32 g_gameTick; }

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A frame array + header + track wired the way the engine does, so SeekToFrame's
// AdvanceFrameIndex call and SampleBoneTranslation's accumulation operate on the
// SAME data — verifying the playback cluster composes with the real frame-advance
// state primitive (skeleton.cpp) rather than a private copy.
struct Rig {
    AnimFrame frames[5] = {};
    AnimHeaderView hdr;
    AnimTrack track;
    float bone[160] = {};

    Rig() {
        for (int i = 0; i < 5; ++i) {
            frames[i].duration = 10;
            frames[i].tx = (float)i;        // 0,1,2,3,4
            frames[i].ty = (float)(2 * i);  // 0,2,4,6,8
            frames[i].tz = 0.0f;
        }
        hdr.frames = frames;
        hdr.frameCount = 5;
        hdr.deltaMode = 1;
        hdr.firstFrame = 0; hdr.lastFrame = 4; hdr.advanceCount = 5;
        track.hdr = &hdr;
        // identity bone, zero base/local translation -> sampler returns the raw
        // accumulated keyframe delta.
        bone[99] = 1; bone[104] = 1; bone[109] = 1;
    }
};
} // namespace

// SeekToFrame must use the REAL AdvanceFrameIndex (skeleton.cpp) to compute toFrame.
TEST(AnimPlaybackI, SeekUsesRealAdvanceFrameIndex) {
    Rig r;
    r.track.modeFlags = 0;   // hold mode, forward
    int obj = 0;
    g_animObjDirtyCalls = 0;

    SeekToFrame(&obj, r.track, 2);
    CHECK_EQ(r.track.fromFrame, 2);
    i32 expectTo = AdvanceFrameIndex(0, 2, /*last*/4, /*first*/0, /*count*/5);
    CHECK_EQ(r.track.toFrame, expectTo);   // == 3 (cur<last steps forward)
    CHECK_EQ(g_animObjDirtyCalls, 1);
}

// SampleBoneTranslation's accumulated delta over whole segments must agree with
// the per-segment delta the sibling InterpolateBoneFrame produces for the same
// range (both reconstruct the same original accumulation; this pins them together).
TEST(AnimPlaybackI, SamplerMatchesInterpolateBoneFrame) {
    Rig r;
    r.track.segFrom = 0;
    r.track.segPhase = 0;   // leading term full, trailing zero at phaseEnd 0
    float sampled[3];
    SampleBoneTranslation(r.bone, r.track, /*phaseEnd*/0, /*toFrame*/3, sampled);

    float ref[3];
    InterpolateBoneFrame(r.frames, r.bone, /*fromFrame*/0, /*toFrame*/3,
                         /*phaseNum*/0, /*phaseEnd*/0, ref);
    CHECK(feq(sampled[0], ref[0]));
    CHECK(feq(sampled[1], ref[1]));
    CHECK(feq(sampled[2], ref[2]));
    // raw value: tx delta 0->3 == 3, ty == 6.
    CHECK(feq(sampled[0], 3.0f));
    CHECK(feq(sampled[1], 6.0f));
}

// A multi-step playback loop: seek to frame 0, then repeatedly advance the track
// frame via the real AdvanceFrameIndex and sample, walking the whole clip.
TEST(AnimPlaybackI, MultiFramePlaybackWalk) {
    Rig r;
    r.track.modeFlags = 0;   // forward, hold at end
    int obj = 0;
    SeekToFrame(&obj, r.track, 0);

    int cur = 0;
    float prevTx = -1.0f;
    for (int step = 0; step < 4; ++step) {
        r.track.segFrom = cur;
        r.track.segPhase = 0;
        float out[3];
        SampleBoneTranslation(r.bone, r.track, /*phaseEnd*/0, /*toFrame*/cur, out);
        float tx = r.frames[cur].tx;
        CHECK(tx > prevTx);
        prevTx = tx;
        cur = AdvanceFrameIndex(0, cur, 4, 0, 5);
    }
    CHECK_EQ(cur, 4);   // hold mode keeps the last frame.
}
