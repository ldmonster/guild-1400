// Golden tests for the Reaper plague-NPC movement / pose / sound math.
// gilde.exe 0x4d8c34 / 0x4d8f74 / 0x4d92a4 / 0x4d9440 / 0x5cb148.
#include "test.h"
#include "sim/npcevent_recon4_reaper_move.h"

#include <cmath>

using namespace guild;
using namespace guild::sim;

namespace {
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
int g_speed = 1;
int SpeedHook() { return g_speed; }
}

TEST(NpcEventRecon4, VectorNormalizeUnit) {
    Vec3 v{0.0f, 3.0f, 4.0f};
    VIBE_Math_VectorNormalize(v);
    CHECK(near(v.x, 0.0f));
    CHECK(near(v.y, 0.6f));
    CHECK(near(v.z, 0.8f));
}

TEST(NpcEventRecon4, VectorNormalizeZero) {
    Vec3 v{0.0f, 0.0f, 0.0f};
    VIBE_Math_VectorNormalize(v);
    CHECK(near(v.x, 0.0f));
    CHECK(near(v.y, 0.0f));
    CHECK(near(v.z, 0.0f));
}

TEST(NpcEventRecon4, SqrDistOpOrder) {
    Vec3 t{40.0f, 60.0f, 90.0f}, s{10.0f, 20.0f, 30.0f};
    // 30^2 + 40^2 + 60^2 = 900 + 1600 + 3600 = 6100
    CHECK(near(VIBE_Reaper_SqrDist(t, s), 6100.0f));
}

TEST(NpcEventRecon4, ApproachDeltaAndSqr) {
    Vec3 src{10.0f, 20.0f, 30.0f}, dst{40.0f, 60.0f, 90.0f};
    ApproachResult r = VIBE_NpcEvent_ReaperApproachTarget_Math(src, dst);
    CHECK(near(r.delta.x, 30.0f));
    CHECK(near(r.delta.y, 40.0f));
    CHECK(near(r.delta.z, 60.0f));
    CHECK(near(r.sqrLen, 6100.0f));
    // spawn position is the (already-biased) source point.
    CHECK(near(r.spawnPos.x, 10.0f));
    CHECK(near(r.spawnPos.y, 20.0f));
    CHECK(near(r.spawnPos.z, 30.0f));
}

TEST(NpcEventRecon4, MoveArrivedWithinThreshold) {
    // planar dist sqrt(30^2 + 20^2) = sqrt(1300) ~= 36.06 < 45 -> arrived.
    Vec3 target{100.0f, 0.0f, 100.0f}, reaper{70.0f, 5.0f, 80.0f};
    MoveResult r = VIBE_NpcEvent_ReaperMoveTowardTarget_Math(
        target, reaper, /*targetHeight*/ 0.0f, /*gameTick*/ 0, /*lastTick*/ 0,
        /*stepHeight*/ 0.0f);
    CHECK(near(r.planarDist, 36.0555115f));
    CHECK_EQ(r.code, 2);
    CHECK(r.arrived);
    // position is left unchanged when arrived.
    CHECK(near(r.newPos.x, 70.0f));
    CHECK(near(r.newPos.y, 5.0f));
    CHECK(near(r.newPos.z, 80.0f));
}

TEST(NpcEventRecon4, MoveTowardStepWithHopArc) {
    ReaperMoveHooks h; h.getGameSpeed = &SpeedHook; g_speed = 2;
    SetReaperMoveHooks(&h);

    Vec3 target{500.0f, 0.0f, 500.0f}, reaper{0.0f, 10.0f, 0.0f};
    MoveResult r = VIBE_NpcEvent_ReaperMoveTowardTarget_Math(
        target, reaper, /*targetHeight*/ 100.0f, /*gameTick*/ 200,
        /*lastTick*/ 100, /*stepHeight*/ 0.0f);

    CHECK_EQ(r.code, 1);              // still moving (707 > 45)
    CHECK(!r.arrived);
    CHECK(near(r.planarDist, 707.10681f, 1e-2f));
    // stepScale = (2*0.25 + 0.5) * 100 = 100
    CHECK(near(r.stepScale, 100.0f));
    // stepHeight+80 = 80 > pre-hop y (33.3756) -> hop arc engaged.
    CHECK(near(r.newPos.x, 687.5166f, 0.2f));
    CHECK(near(r.newPos.y, 131.17557f, 0.05f));
    CHECK(near(r.newPos.z, 687.5166f, 0.2f));

    SetReaperMoveHooks(nullptr);
}

TEST(NpcEventRecon4, MoveTowardNoHopWhenAboveTerrain) {
    ReaperMoveHooks h; h.getGameSpeed = &SpeedHook; g_speed = 1;
    SetReaperMoveHooks(&h);

    // Reaper already high; stepHeight low so no hop. target far.
    Vec3 target{1000.0f, 0.0f, 0.0f}, reaper{0.0f, 500.0f, 0.0f};
    MoveResult r = VIBE_NpcEvent_ReaperMoveTowardTarget_Math(
        target, reaper, /*targetHeight*/ 0.0f, /*gameTick*/ 110,
        /*lastTick*/ 100, /*stepHeight*/ -1000.0f);

    CHECK_EQ(r.code, 1);
    // stepScale = (1*0.25+0.5)*10 = 7.5 ; moves only along +x.
    CHECK(near(r.stepScale, 7.5f));
    CHECK(r.newPos.x > 0.0f);
    // No hop: stepHeight+flt_61EFE4(80) = -920 < newPos.y. But dy is NOT zero —
    // the y-delta is (targetHeight + 80) - reaper.y = (0+80) - 500 = -420 (flt_61EFE4
    // == 80.0, verified via get_bytes @0x61EFE4). So the normalized step does move y:
    //   mag = sqrt(1000^2 + 420^2) = 1084.61975 ; ny = -420/mag = -0.3872451
    //   newPos.y = 500 + 7.5*ny = 500 - 2.90434 = 497.0957.
    CHECK(near(r.newPos.y, 497.0957f, 0.02f));
    CHECK(near(r.newPos.z, 0.0f));

    SetReaperMoveHooks(nullptr);
}

TEST(NpcEventRecon4, CacheTargetPoseDelta) {
    Vec3 target{5.0f, 9.0f, 13.0f}, reaper{1.0f, 1.0f, 1.0f};
    CachePoseResult r = VIBE_NpcEvent_ReaperCacheTargetPose_Math(target, reaper);
    CHECK(near(r.delta.x, 4.0f));
    CHECK(near(r.delta.y, 8.0f));
    CHECK(near(r.delta.z, 12.0f));
    // 16 + 64 + 144 = 224
    CHECK(near(r.sqrLen, 224.0f));
}

TEST(NpcEventRecon4, SoundPosDelta) {
    Vec3 target{2.0f, 6.0f, 9.0f}, reaper{0.0f, 0.0f, 0.0f};
    SoundPosResult r = VIBE_NpcEvent_ReaperUpdateSoundPos_Math(target, reaper);
    CHECK(near(r.delta.x, 2.0f));
    CHECK(near(r.delta.y, 6.0f));
    CHECK(near(r.delta.z, 9.0f));
    // 4 + 36 + 81 = 121
    CHECK(near(r.sqrLen, 121.0f));
}
