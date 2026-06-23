// Golden, headless tests for the per-frame pose DRIVER (gilde.exe 0x5cd1d8).
// We install a recording SkeletonPoseHooks and a small synthetic skeleton/morph
// and assert the DRIVER's observable control flow: the early-out, the frame-advance
// (loop / clamp / reverse), the per-bone leaf call order, the boundary-triggered
// attachment prune + vegetation relight, and the morph terminal/free path.
#include "render/skeleton_pose_driver.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::render;

namespace {

// A recorder that captures the ORDER + arguments of every hook the driver fires.
struct Recorder {
    std::vector<std::string> log;
    int boneMatrixCalls = 0;
    int vegCacheCalls = 0;
    i32 vegCacheArg = -999;
    int pruneCalls = 0;
    int setPosCalls = 0;
    int setWorldCalls = 0;
    int freeMorphCalls = 0;
    int textureAdvCalls = 0;
    std::vector<std::pair<int,int>> sampledBones; // (layer,track) order
    std::vector<i32> sampledFrames;
};

// Build a hooks struct wired to a recorder. Most leaves just log; the bone sampler
// returns a translation far from the settle tolerance so the settle break never
// fires unless a test wants it to.
SkeletonPoseHooks MakeHooks(Recorder& r) {
    SkeletonPoseHooks h;
    h.textureAdvanceAnimFrames = [&r](SkeletonPoseState&, u32){ r.textureAdvCalls++; r.log.push_back("tex"); };
    h.objectPropagateDirty     = [&r](SkeletonPoseState&, u32){ r.log.push_back("dirty"); };
    h.globalFrameCounter       = [](){ return 0xABCDu; };
    h.sampleBoneTranslation    = [&r](SkeletonPoseState&, int l, int t, i32 f, i32, float out[3]){
        r.sampledBones.push_back({l,t}); r.sampledFrames.push_back(f);
        out[0] = 100.0f; out[1] = 0.0f; out[2] = 100.0f;  // far from any tolPos
    };
    h.interpolateBoneFrame     = [&r](SkeletonPoseState&, int, int, i32, i32, int){ r.log.push_back("interp"); };
    h.computeBoneDelta         = [&r](SkeletonPoseState&, int, int, i32, i32, int){ r.log.push_back("delta"); };
    h.objectSetPosition        = [&r](SkeletonPoseState&, const float*){ r.setPosCalls++; r.log.push_back("setpos"); };
    h.objectSetWorldTranslation= [&r](SkeletonPoseState&, const float*){ r.setWorldCalls++; r.log.push_back("setworld"); };
    h.pruneExpiredAttachments  = [&r](SkeletonPoseState&, int){ r.pruneCalls++; r.log.push_back("prune"); };
    h.computeBoneMatrices      = [&r](SkeletonPoseState&){ r.boneMatrixCalls++; r.log.push_back("matrices"); };
    h.findHighestPriorityLayer = [](SkeletonPoseState&){ return 0; };
    h.buildVegetationCache     = [&r](SkeletonPoseState&, i32 a){ r.vegCacheCalls++; r.vegCacheArg = a; r.log.push_back("veg"); };
    h.freeMorphAnim            = [&r](SkeletonPoseState&){ r.freeMorphCalls++; r.log.push_back("free"); };
    return h;
}

// A small forward-looping skeletal layer: 4 frames, equal duration 10, one active
// delta-encoded track on layer 0 track 0.
i32 g_durations[4] = {10, 10, 10, 10};

SkeletonPoseState::Layer MakeLayer(u8 mode, u8 flags, i32 fromFrame, i32 phase) {
    SkeletonPoseState::Layer L;
    L.active = true;
    PoseTrack& t = L.tracks[0];
    t.animHeaderId = 1;            // active header present
    t.flags = flags;
    t.mode  = mode;
    t.fromFrame = fromFrame;
    t.toFrame   = fromFrame;
    t.phase     = phase;
    t.expiry    = 0xFFFFFFFFu;     // never arm boundary via expiry unless overridden
    PoseAnimHeader& hd = L.headers[0];
    hd.frameCount = 4;
    hd.startFrame = 0;
    hd.endFrame   = 3;
    hd.deltaEncoded = true;        // -> interpolateBoneFrame path
    hd.durations  = g_durations;
    return L;
}

} // namespace

// --- 1. Early-out: same time as last update returns true and fires NO hooks. ---
TEST(SkeletonPoseDriver, EarlyOutOnSameTime) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.lastUpdateTime = 1000;
    bool ok = UpdateSkeletonPose(st, h, 1000);
    CHECK(ok);
    CHECK_EQ((int)r.log.size(), 0);
    CHECK_EQ(r.boneMatrixCalls, 0);   // early-out is before the palette rebuild
}

// --- 2. A normal tick rebuilds the bone-matrix palette exactly once. ---
TEST(SkeletonPoseDriver, AlwaysRebuildsPaletteOnAdvance) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.lastUpdateTime = 0;
    bool ok = UpdateSkeletonPose(st, h, 50);
    CHECK(ok);
    CHECK_EQ(r.boneMatrixCalls, 1);
}

// --- 3. Reverse-leg step inside the clip walks the frame cursor toward `first`. ---
//   The engine services tracks via the +110&2 ("playing in reverse") leg; with the
//   mode reverse bit (+109&2) set the phase subtracts and the cursor steps back one
//   frame per consumed segment (the 0x5cd7a3 branch).
TEST(SkeletonPoseDriver, ReverseStepWalksFrame) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.hasDrawData = true;
    // flags 0x02 = serviced/reverse leg; mode 0x02 = reverse-direction phase carry.
    SkeletonPoseState::Layer L = MakeLayer(/*mode reverse*/0x02, /*flags*/0x02,
                                           /*fromFrame*/3, /*phase*/0);
    L.tracks[0].toFrame = 3;
    // Pre-fold a reverse phase increment of 5 ticks; mode&2 -> phase -= 5 -> -5 (<0).
    L.tracks[0].phaseFrac = 5.0f;
    st.layers = &L; st.layerCount = 1;

    bool ok = UpdateSkeletonPose(st, h, 100);
    CHECK(ok);
    // phase went negative and firstFrame(0) < toFrame(3): step the cursor back.
    CHECK(L.tracks[0].fromFrame < 3);
    // delta-encoded header -> the per-frame interpolate leaf ran.
    bool sawInterp = false; for (auto& s : r.log) if (s=="interp") sawInterp = true;
    CHECK(sawInterp);
    CHECK_EQ(r.boneMatrixCalls, 1);
}

// --- 4. A non-looping reverse clip HOLDS/CLAMPS at the `first` boundary. ---
//   With no loop bit (mode 0x02 only), when the reverse cursor reaches firstFrame the
//   hold branch (0x5cd809) re-arms at advLast and the repeat counter governs finish.
TEST(SkeletonPoseDriver, NonLoopingReverseClipHoldsAtStart) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.hasDrawData = true;
    SkeletonPoseState::Layer L = MakeLayer(/*mode reverse,no-loop*/0x02, /*flags*/0x02,
                                           /*fromFrame*/0, /*phase*/0);
    L.tracks[0].toFrame = 0;          // already at first -> hold branch
    L.tracks[0].phaseFrac = 25.0f;    // big reverse overshoot
    st.layers = &L; st.layerCount = 1;

    bool ok = UpdateSkeletonPose(st, h, 200);
    CHECK(ok);
    // The hold branch (0x5cd809) re-armed the cursor at the clip end (advLast = 3)
    // rather than getting stuck at the `first` frame, then the ping-pong continued.
    CHECK(L.tracks[0].fromFrame != 0);
    CHECK_EQ(r.boneMatrixCalls, 1);
}

// --- 5. Boundary + type-4 object triggers the attachment prune AND veg relight. ---
//   Drive the reverse one-shot to a finish (clamp bit), and arm the activity-expiry
//   so boundaryThisTick (v193) latches the vegetation relight.
TEST(SkeletonPoseDriver, BoundaryTriggersPruneAndVegCache) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.hasDrawData = true;
    st.hasSkin = true;
    st.objectType = 4;             // vegetation path
    st.drawFlags = 0;              // lighting enabled
    SkeletonPoseState::Layer L = MakeLayer(/*mode clamp*/0x10, /*flags serviced*/0x02,
                                           /*fromFrame*/2, /*phase*/0);
    L.tracks[0].toFrame = 2;
    L.tracks[0].phaseFrac = 30.0f;
    L.tracks[0].expiry = 0;        // elapsed > 0 -> arm boundaryThisTick (v193)
    st.layers = &L; st.layerCount = 1;

    bool ok = UpdateSkeletonPose(st, h, 300);
    CHECK(ok);
    CHECK(st.boundaryThisTick);
    // type-4 + boundary -> rebuild veg cache; texture-anim advance also runs.
    CHECK_EQ(r.vegCacheCalls, 1);
    CHECK_EQ(r.textureAdvCalls, 1);
}

// --- 6. Forced tick (sign bit) suppresses the texture advance AND the veg relight. ---
TEST(SkeletonPoseDriver, ForcedTickSuppressesLightCache) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.hasDrawData = true; st.hasSkin = true; st.objectType = 4; st.drawFlags = 0;
    SkeletonPoseState::Layer L = MakeLayer(0x10, 0x02, 2, 0);
    L.tracks[0].phaseFrac = 30.0f; L.tracks[0].expiry = 0;
    st.layers = &L; st.layerCount = 1;

    // top bit set => forced.
    bool ok = UpdateSkeletonPose(st, h, 0x80000000u | 400u);
    CHECK(ok);
    CHECK(st.boundaryThisTick);     // boundary still detected
    CHECK_EQ(r.textureAdvCalls, 0); // forced => no texture advance
    CHECK_EQ(r.vegCacheCalls, 0);   // forced => no veg relight
    CHECK_EQ(r.boneMatrixCalls, 1); // palette still rebuilt
}

// --- 7. Multiple active tracks are sampled in (layer,track) order. ---
TEST(SkeletonPoseDriver, BoneIterationOrder) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.hasDrawData = true;
    SkeletonPoseState::Layer L;
    L.active = true;
    for (int k = 0; k < 3; ++k) {
        PoseTrack& t = L.tracks[k];
        t.animHeaderId = 1;
        t.flags = 0x02 | 0x04;     // active + settle-check (so sampleBoneTranslation runs)
        t.mode  = 0x01;
        t.fromFrame = 0; t.toFrame = 0; t.phase = 0; t.phaseFrac = 1.0f;
        t.expiry = 0xFFFFFFFFu;
        t.settleTol = 0.001f;      // tiny tol -> our 100.0 sample never settles
        PoseAnimHeader& hd = L.headers[k];
        hd.frameCount = 4; hd.startFrame = 0; hd.endFrame = 3;
        hd.deltaEncoded = true; hd.durations = g_durations;
    }
    st.layers = &L; st.layerCount = 1;

    bool ok = UpdateSkeletonPose(st, h, 10);
    CHECK(ok);
    // settle-check fires sampleBoneTranslation once per track, in order 0,1,2.
    CHECK_EQ((int)r.sampledBones.size(), 3);
    CHECK(r.sampledBones[0] == std::make_pair(0,0));
    CHECK(r.sampledBones[1] == std::make_pair(0,1));
    CHECK(r.sampledBones[2] == std::make_pair(0,2));
}

// --- 8. Morph terminal path pushes position + world translation and frees the block. ---
TEST(SkeletonPoseDriver, MorphTerminalFreesBlock) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.animBaseTime = 0;
    PoseMorphAnim m;
    m.flags = 0x02;        // active (no 0x08 latch -> takes the free path)
    m.mode  = 0x10;        // clamp one-shot
    m.frameCount = 3;
    m.fromFrame = 1;       // near the end
    m.toFrame   = 1;
    m.phase     = 0;
    m.phaseFrac = 5.0f;
    m.flags |= 0x10;       // hold/boundary bit so the forward terminal clamp engages
    m.expiry = 0;          // elapsed past expiry -> morphActiveOut
    st.morph = &m;

    bool ok = UpdateSkeletonPose(st, h, 60);
    CHECK(ok);
    CHECK(m.finished);
    CHECK_EQ(r.setPosCalls, 1);
    CHECK_EQ(r.setWorldCalls, 1);
    CHECK_EQ(r.freeMorphCalls, 1);   // no 0x08 latch -> the block is freed
}

// --- 9. Morph latch (0x08) keeps the block (no free) and arms reset-pending. ---
TEST(SkeletonPoseDriver, MorphLatchKeepsBlock) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    PoseMorphAnim m;
    m.flags = 0x02 | 0x10;
    m.mode  = 0x10 | 0x08;   // clamp + latch
    m.frameCount = 3; m.fromFrame = 1; m.toFrame = 1; m.phase = 0; m.phaseFrac = 5.0f;
    m.expiry = 0xFFFFFFFFu;
    st.morph = &m;

    bool ok = UpdateSkeletonPose(st, h, 70);
    CHECK(ok);
    CHECK(m.finished);
    CHECK_EQ(r.freeMorphCalls, 0);          // latched -> NOT freed
    CHECK_EQ((int)(m.flags & 0x02), 0);     // active cleared
    CHECK_EQ((int)(m.mode & 0x20), 0x20);   // reset-pending armed
}

// --- 11. Morph reverse clamp finishes only when toFrame<=0 (0x5ce93b key = +8). ---
//   With the reverse bit set, phase<0 and toFrame already at 0, the clamp(0x10) bit
//   drives the terminal at 0x5ce982 (fromFrame=0, phase=0, finished). This locks the
//   `cmp [esi+8],0 / jle` gate (toFrame, NOT phase).
TEST(SkeletonPoseDriver, MorphReverseClampToFrameGate) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    PoseMorphAnim m;
    m.flags = 0x02;            // active, no latch
    m.mode  = 0x02 | 0x10;     // reverse + clamp
    m.frameCount = 3;
    m.fromFrame = 0;
    m.toFrame   = 0;           // toFrame<=0 -> terminal block
    m.phase     = 0;
    m.phaseFrac = 5.0f;        // reverse: phase -= 5 -> -5 (<0)
    m.expiry = 0xFFFFFFFFu;
    st.morph = &m;

    bool ok = UpdateSkeletonPose(st, h, 80);
    CHECK(ok);
    CHECK(m.finished);
    CHECK_EQ(m.fromFrame, 0);
    CHECK_EQ(m.phase, 0);
    CHECK_EQ(r.freeMorphCalls, 1);  // no latch -> freed
}

// --- 12. Veg-cache: FindHighestPriorityLayer runs only when a layer was recorded
//   (v180 != 0). Disasm 0x5ce419 is `jz` past the call (skip-when-zero). When no
//   layer recorded but morph arms the relight, BuildVegetationCache is still called
//   with the unmodified (0) layer and the search is skipped.
TEST(SkeletonPoseDriver, VegCacheGateSkipsSearchWhenZero) {
    int searchCalls = 0;
    Recorder r; auto h = MakeHooks(r);
    h.findHighestPriorityLayer = [&searchCalls](SkeletonPoseState&){ searchCalls++; return 2; };
    SkeletonPoseState st;
    st.hasDrawData = true; st.hasSkin = true; st.objectType = 4; st.drawFlags = 0;
    st.animBaseTime = 0;
    // Morph arms the relight (morphActiveOut) but records NO skeletal layer -> v180==0.
    PoseMorphAnim m;
    m.flags = 0x02; m.mode = 0x10; m.frameCount = 3; m.fromFrame = 1; m.toFrame = 1;
    m.phase = 0; m.phaseFrac = 5.0f; m.flags |= 0x10; m.expiry = 0;  // -> morphActiveOut
    st.morph = &m;

    bool ok = UpdateSkeletonPose(st, h, 90);
    CHECK(ok);
    CHECK(st.morphActiveOut);
    CHECK_EQ(searchCalls, 0);          // v180==0 -> search skipped (the `jz` path)
    CHECK_EQ(r.vegCacheCalls, 1);      // BuildVegetationCache still runs (with 0)
    CHECK_EQ(r.vegCacheArg, 0);
}

// --- 10. No drawData + no morph: a bare tick still rebuilds the palette and returns. ---
TEST(SkeletonPoseDriver, BareTickRebuildsPaletteAndReturns) {
    Recorder r; auto h = MakeHooks(r);
    SkeletonPoseState st;
    st.lastUpdateTime = 0;
    st.hasDrawData = false;
    bool ok = UpdateSkeletonPose(st, h, 5);
    CHECK(ok);
    CHECK_EQ(r.boneMatrixCalls, 1);
    CHECK_EQ(r.vegCacheCalls, 0);   // no boundary / not type-4
}
