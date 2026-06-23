#include "test.h"
#include "render/animation_playback.h"

#include <cstring>
#include <cstdint>

using namespace guild;
using namespace guild::render;

// Test-observable globals from the unowned-stubs placeholder.
namespace guild::render {
extern int   g_animObjSetPositionCalls;
extern int   g_animObjSetWorldCalls;
extern int   g_animObjDirtyCalls;
extern float g_animLastPosition[3];
extern float g_animLastWorld[3];
}
namespace guild::sim { extern u32 g_gameTick; }

namespace {

// A 4-keyframe AnimFrame array (192-byte stride) + a typed AnimHeaderView + bone.
struct Fixture {
    AnimFrame frames[4] = {};
    AnimHeaderView hdr;
    float bone[160] = {};

    Fixture() {
        int durs[4] = {10, 20, 30, 40};
        float tx[4] = {0, 1, 3, 6}, ty[4] = {0, 2, 2, 5}, tz[4] = {0, 0.5f, 1.5f, 1.5f};
        float rx[4] = {0, 10, 20, 30}, ry[4] = {0, 1, 2, 3}, rz[4] = {0, -1, -2, -3};
        for (int i = 0; i < 4; ++i) {
            frames[i].duration = durs[i];
            frames[i].tx = tx[i]; frames[i].ty = ty[i]; frames[i].tz = tz[i];
            // +44/+48/+52 rotation/aux triple == AnimFrame.rx/ry/rz.
            frames[i].rx = rx[i]; frames[i].ry = ry[i]; frames[i].rz = rz[i];
        }
        hdr.frames = frames;
        hdr.frameCount = 4;
        hdr.deltaMode = 1;
        hdr.firstFrame = 0;
        hdr.lastFrame = 3;
        hdr.advanceCount = 4;

        bone[19] = 100; bone[20] = 200; bone[21] = 300;
        bone[30] = 1;   bone[31] = 2;   bone[32] = 3;
        bone[99] = 2; bone[103] = 0; bone[107] = 0;
        bone[100] = 0; bone[104] = 2; bone[108] = 0;
        bone[101] = 0; bone[105] = 0; bone[109] = 2;
    }
};

bool near(float a, float b) { return (a - b) * (a - b) < 1e-6f; }

AnimTrack MakeTrack(const Fixture& f) {
    AnimTrack t;
    t.hdr = &f.hdr;
    return t;
}

} // namespace

TEST(AnimPlayback, SampleDeltaAccumulate) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    t.segFrom = 0;   // v51
    t.segPhase = 5;  // v49
    float out[3];
    const float* r = SampleBoneTranslation(f.bone, t, /*phaseEnd*/7, /*toFrame*/2, out);
    CHECK(r == f.bone);
    CHECK(near(out[0], 107.4000015f));
    CHECK(near(out[1], 205.3999939f));
    CHECK(near(out[2], 305.5f));
}

TEST(AnimPlayback, SampleStaticPose) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    t.segFrom = 2;   // segFrom == toFrame
    t.segPhase = 7;  // segPhase == phaseEnd
    float out[3];
    SampleBoneTranslation(f.bone, t, /*phaseEnd*/7, /*toFrame*/2, out);
    CHECK(near(out[0], 101.0f));
    CHECK(near(out[1], 202.0f));
    CHECK(near(out[2], 303.0f));
}

TEST(AnimPlayback, SampleLerpNonDelta) {
    Fixture f;
    f.hdr.deltaMode = 0;   // delta mode OFF -> two-keyframe lerp path
    AnimTrack t = MakeTrack(f);
    t.fromFrame = 1;
    t.toFrame = 3;
    t.phaseAccum = 15;
    float out[3];
    SampleBoneTranslation(f.bone, t, 0, 0, out);
    CHECK(near(out[0], 110.5f));
    CHECK(near(out[1], 210.5f));
    CHECK(near(out[2], 305.5f));
}

TEST(AnimPlayback, ComputeBoneDeltaPositionAndWorld) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    // obj 3x3 cols at bytes +396.. (identity*2), base +76/+80/+84, world +132/+136/+140
    float obj[160] = {};
    auto setb = [&](int byteOff, float v) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(obj) + byteOff, &v, sizeof(float));
    };
    setb(396, 2); setb(412, 0); setb(428, 0);
    setb(400, 0); setb(416, 2); setb(432, 0);
    setb(404, 0); setb(420, 0); setb(436, 2);
    setb(76, 10); setb(80, 20); setb(84, 30);
    setb(132, 5); setb(136, 6); setb(140, 7);

    g_animObjSetPositionCalls = 0;
    g_animObjSetWorldCalls = 0;
    ComputeBoneDelta(obj, t, /*toFrame*/2, /*fromFrame*/0, /*mode*/0x3);
    CHECK_EQ(g_animObjSetPositionCalls, 1);
    CHECK_EQ(g_animObjSetWorldCalls, 1);
    CHECK(near(g_animLastPosition[0], 16.0f));
    CHECK(near(g_animLastPosition[1], 24.0f));
    CHECK(near(g_animLastPosition[2], 33.0f));
    CHECK(near(g_animLastWorld[0], 25.0f));
    CHECK(near(g_animLastWorld[1], 8.0f));
    CHECK(near(g_animLastWorld[2], 5.0f));
}

TEST(AnimPlayback, ComputeBoneDeltaModeZeroNoPush) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    float obj[160] = {};
    g_animObjSetPositionCalls = 0;
    g_animObjSetWorldCalls = 0;
    u8 r = ComputeBoneDelta(obj, t, 2, 0, /*mode*/0);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(g_animObjSetPositionCalls, 0);
    CHECK_EQ(g_animObjSetWorldCalls, 0);
}

TEST(AnimPlayback, UpdateTrackBlendWeight) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    float wFrom = 0.25f, wTo = 0.75f;
    int wFromBits, wToBits;
    std::memcpy(&wFromBits, &wFrom, 4);
    std::memcpy(&wToBits, &wTo, 4);

    auto weightAt = [&](u32 tick) {
        sim::g_gameTick = tick;
        UpdateTrackBlendWeight(t, wFromBits, wToBits, 100, 200);
        return t.blendCur;
    };
    // before window: v6=0 -> blendCur = wFrom (NOT 0); the original clamps the
    // PHASE to 0, leaving the start weight, not the weight to 0.
    CHECK(near(weightAt(50), 0.25f));
    CHECK(near(weightAt(150), 0.5f));   // mid window
    CHECK(near(weightAt(250), 0.75f));  // after window -> wTo
}

TEST(AnimPlayback, FindHighestPriorityLayer) {
    AnimLayer layers[3] = {};
    layers[0].active = 1; layers[0].weight = 0.2f;
    layers[1].active = 0; layers[1].weight = 0.9f;  // inactive -> ignored
    layers[2].active = 1; layers[2].weight = 0.5f;  // highest active weight
    CHECK_EQ(FindHighestPriorityLayer(true, layers, 3), 2);
    // gate clear -> -1 (the original's 0 / "none")
    CHECK_EQ(FindHighestPriorityLayer(false, layers, 3), -1);
    // no active layer -> -1
    AnimLayer none[3] = {};
    CHECK_EQ(FindHighestPriorityLayer(true, none, 3), -1);
}

TEST(AnimPlayback, SetAndClearLoopFlags) {
    const char* n0 = "walk";
    const char* n1 = "run";
    const char* n2 = "walk";
    AnimTrack tracks[3] = {};
    tracks[0].boneName = n0; tracks[1].boneName = n1; tracks[2].boneName = n2;

    int stamp = 0;
    sim::g_gameTick = 4242;
    AnimSetLoopFlags(tracks, 3, true, "walk", &stamp);
    CHECK_EQ((int)(tracks[0].loopFlags & 2), 2);   // walk -> set
    CHECK_EQ((int)(tracks[1].loopFlags & 2), 0);   // run  -> untouched
    CHECK_EQ((int)(tracks[2].loopFlags & 2), 2);   // walk -> set
    CHECK_EQ(stamp, 4242);

    AnimClearLoopFlags(tracks, 3, "walk");
    CHECK_EQ((int)(tracks[0].loopFlags & 2), 0);
    CHECK_EQ((int)(tracks[2].loopFlags & 2), 0);

    // SetLoopFlags(false) clears the bit (and re-stamps).
    AnimSetLoopFlags(tracks, 3, true, "walk", &stamp);
    AnimSetLoopFlags(tracks, 3, false, "walk", &stamp);
    CHECK_EQ((int)(tracks[0].loopFlags & 2), 0);
}

TEST(AnimPlayback, FindFirstActiveBone) {
    // header: idx87 frames ptr (nonzero), idx81 bone count, names at +64 stride 64.
    std::uint8_t hdr[1024] = {};
    void* dummy = hdr; std::memcpy(hdr + 87 * 4, &dummy, sizeof(void*));
    int count = 3; std::memcpy(hdr + 81 * 4, &count, 4);
    std::strcpy(reinterpret_cast<char*>(hdr + 64 + 0 * 64), "pelvis");
    std::strcpy(reinterpret_cast<char*>(hdr + 64 + 1 * 64), "spine");
    std::strcpy(reinterpret_cast<char*>(hdr + 64 + 2 * 64), "head");
    CHECK_EQ(FindFirstActiveBone(hdr, "pelvis"), 0);
    CHECK_EQ(FindFirstActiveBone(hdr, "spine"), 1);
    CHECK_EQ(FindFirstActiveBone(hdr, "head"), 2);
    CHECK_EQ(FindFirstActiveBone(hdr, "missing"), -1);
    CHECK_EQ(FindFirstActiveBone(nullptr, "x"), -1);
}

TEST(AnimPlayback, SeekToFrameClampAndPhase) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    t.modeFlags = 0;   // hold mode, forward
    int dummyObj = 0;
    g_animObjDirtyCalls = 0;

    // seek to a mid frame: phase reset to 0, fromFrame stored, toFrame advanced.
    u8 r = SeekToFrame(&dummyObj, t, 1);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(t.fromFrame, 1);
    CHECK_EQ(t.phaseAccum, 0);     // mid-clip -> phase 0
    CHECK_EQ(t.toFrame, 2);        // AdvanceFrameIndex forward (cur<last)
    CHECK_EQ(g_animObjDirtyCalls, 1);

    // seek past the end -> clamp to last frame (3); phase = dur(3)-1 = 39.
    SeekToFrame(&dummyObj, t, 99);
    CHECK_EQ(t.fromFrame, 3);
    CHECK_EQ(t.phaseAccum, 39);

    // negative -> wraps to last frame (3) too.
    SeekToFrame(&dummyObj, t, -1);
    CHECK_EQ(t.fromFrame, 3);
}

// ===========================================================================
// W11-ANIM hardening — degenerate playback inputs (ASAN/UBSAN).
// ===========================================================================

// FindFirstActiveBone reads a1[87] (a 32-bit pointer slot at byte 348). The UBSAN
// fix reads it as a 4-byte word; store ONLY 4 bytes there to prove there is no
// 8-byte misaligned over-read (the previous code did `*(const void**)` here).
TEST(AnimPlaybackEdge, FindFirstActiveBone_FourByteFramesSlot) {
    std::uint8_t hdr[1024] = {};
    std::uint32_t framesPtr = 0xABCD1234u;            // nonzero 32-bit slot only
    std::memcpy(hdr + 87 * 4, &framesPtr, 4);
    int count = 2; std::memcpy(hdr + 81 * 4, &count, 4);
    std::strcpy(reinterpret_cast<char*>(hdr + 64 + 0 * 64), "root");
    std::strcpy(reinterpret_cast<char*>(hdr + 64 + 1 * 64), "tip");
    CHECK_EQ(FindFirstActiveBone(hdr, "root"), 0);
    CHECK_EQ(FindFirstActiveBone(hdr, "tip"), 1);
    CHECK_EQ(FindFirstActiveBone(hdr, "none"), -1);
}

// A zero frames-pointer slot (a1[87] == 0) -> -1 with no name scan (no OOB on the
// name buffer even if the count says otherwise).
TEST(AnimPlaybackEdge, FindFirstActiveBone_NullFramesSlot) {
    std::uint8_t hdr[1024] = {};
    std::uint32_t framesPtr = 0; std::memcpy(hdr + 87 * 4, &framesPtr, 4);
    int count = 9999; std::memcpy(hdr + 81 * 4, &count, 4);   // bogus large count
    CHECK_EQ(FindFirstActiveBone(hdr, "x"), -1);              // bailed before scan
}

// Zero bone count (a1[81] <= 0) -> -1 immediately; the name scan never runs.
TEST(AnimPlaybackEdge, FindFirstActiveBone_ZeroCount) {
    std::uint8_t hdr[1024] = {};
    std::uint32_t framesPtr = 1; std::memcpy(hdr + 87 * 4, &framesPtr, 4);
    int count = 0; std::memcpy(hdr + 81 * 4, &count, 4);
    CHECK_EQ(FindFirstActiveBone(hdr, "x"), -1);
}

// SeekToFrame on a track with no header (hdr == nullptr) returns 0 without touching
// any frame array.
TEST(AnimPlaybackEdge, SeekToFrame_NoHeader) {
    AnimTrack t;            // t.hdr defaults to nullptr
    int obj = 0;
    CHECK_EQ((int)SeekToFrame(&obj, t, 5), 0);
}

// SeekToFrame frame == frameCount (one past the last valid index) clamps to the
// last frame; ASAN proves SegDur(frames, last) reads a valid keyframe.
TEST(AnimPlaybackEdge, SeekToFrame_FramePastTrackClamps) {
    Fixture f;
    AnimTrack t = MakeTrack(f);
    t.modeFlags = 0;
    int obj = 0;
    SeekToFrame(&obj, t, f.hdr.frameCount);     // == 4, one past last index 3
    CHECK_EQ(t.fromFrame, 3);                   // clamped to last frame
    CHECK_EQ(t.phaseAccum, 39);                 // dur(3)-1
}
