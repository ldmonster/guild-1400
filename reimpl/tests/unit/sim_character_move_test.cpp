// Unit tests for the deferred Character WALK MOVEMENT integration.
//   src/sim/charaction_walk.{h,cpp}  — per-tick movement: rotation interp,
//                                       waypoint advance, height snap, speed.
//   src/sim/character_move.{h,cpp}   — universe transition, turn-anim select.
// Golden values are python-computed (see the agent transcript).
#include "sim/charaction_walk.h"
#include "sim/character_move.h"
#include "render/heightmap.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstdint>

using namespace guild;
using namespace guild::sim;

namespace {
bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build an 8x8 synthetic heightmap with height[ty*n+tx] = tx+ty, scale 10/1/10.
render::Heightmap MakeHeightmap(std::uint8_t* heights /*64*/) {
    render::Heightmap hm{};
    hm.originX = 0; hm.originY = 0; hm.originZ = 0;
    hm.scaleX = 10; hm.scaleY = 1; hm.scaleZ = 10;
    hm.size = 8;
    for (int ty = 0; ty < 8; ++ty)
        for (int tx = 0; tx < 8; ++tx)
            heights[ty * 8 + tx] = (std::uint8_t)(tx + ty);
    hm.heights = heights;
    hm.entries = nullptr;
    return hm;
}
} // namespace

// --- Rotation interpolation golden vectors (WalkRotateTowardHeading) --------
TEST(SimCharMove, RotateBucketPi3) {
    // cur=0, delta=1.0 (|delta|<=pi/2 && <=pi/3? 1.0<=1.047 yes; <=pi/6? no ->
    // factor 0.04*1.5 ... wait 1.0>pi/6(0.5236) and <=pi/3(1.047) so bucket is
    // 0.04*1.5). elapsed=7: step=7*0.90909=6.3636; s=6.3636*0.04*1.5=0.38182.
    int done = 0;
    float nv = WalkRotateTowardHeading(0.0f, 1.0f, 7, false, false, &done);
    CHECK(nearf(nv, 0.38181818f));
    CHECK_EQ(done, 0);
}

TEST(SimCharMove, RotateAlignedSnaps) {
    // |delta| <= 0.04 -> snap to target = cur+delta, done.
    int done = 0;
    float nv = WalkRotateTowardHeading(0.5f, 0.02f, 7, false, false, &done);
    CHECK(nearf(nv, 0.52f));
    CHECK_EQ(done, 1);
}

TEST(SimCharMove, RotateLargeAngleUses020) {
    // delta=2.5 > 2*pi/3(2.094) so |delta|>pi/2 -> factor 0.2.
    // step=7*0.90909=6.3636; s=6.3636*0.2=1.27273; nv=0+1.27273.
    int done = 0;
    float nv = WalkRotateTowardHeading(0.0f, 2.5f, 7, false, false, &done);
    CHECK(nearf(nv, 1.27272727f));
    CHECK_EQ(done, 0);
}

TEST(SimCharMove, RotateNegativeDelta) {
    // cur=1.0, delta=-0.8 (|0.8|<=pi/2, <=pi/3? 0.8<=1.047 yes, <=pi/6? no ->
    // 0.04*1.5). step=6.3636; s=6.3636*0.06=0.381818; nv=1.0-0.381818=0.618182.
    int done = 0;
    float nv = WalkRotateTowardHeading(1.0f, -0.8f, 7, false, false, &done);
    CHECK(nearf(nv, 0.61818182f));
    CHECK_EQ(done, 0);
}

TEST(SimCharMove, RotateOvershootSnaps) {
    // Big step past a tiny target -> snap & done. delta=0.05, elapsed=100.
    int done = 0;
    float nv = WalkRotateTowardHeading(0.0f, 0.05f, 100, false, false, &done);
    CHECK(nearf(nv, 0.05f));
    CHECK_EQ(done, 1);
}

TEST(SimCharMove, RotateCartScalesStep) {
    // fast/cart move scales step by 0.4. delta=1.0, elapsed=7:
    // step=7*0.90909*0.4=2.54545; s=2.54545*0.06=0.152727.
    int done = 0;
    float nv = WalkRotateTowardHeading(0.0f, 1.0f, 7, /*fast*/true, false, &done);
    CHECK(nearf(nv, 0.15272727f));
}

// --- Segment duration (WalkSegmentDuration) ---------------------------------
TEST(SimCharMove, SegmentDuration) {
    CHECK(nearf(WalkSegmentDuration(1, 4, false, false), 20.0f));  // mid segment
    CHECK(nearf(WalkSegmentDuration(3, 4, false, false), 8.0f));   // final segment
    CHECK(nearf(WalkSegmentDuration(1, 4, true,  false), 40.0f));  // mounted mid
    CHECK(nearf(WalkSegmentDuration(3, 4, true,  false), 24.0f));  // mounted final
    CHECK(nearf(WalkSegmentDuration(1, 4, true,  true), 120.0f));  // mounted+indoor (*3)
}

// --- Anim playback speed (WalkAnimSpeed) ------------------------------------
TEST(SimCharMove, AnimSpeed) {
    CHECK(nearf(WalkAnimSpeed(1.0f, 1.0f, 0.7f, 3,  false), 1.5400000f));  // flat 2.2
    CHECK(nearf(WalkAnimSpeed(1.0f, 1.0f, 0.7f, 6,  false), 1.75f));       // stairs 2.5
    CHECK(nearf(WalkAnimSpeed(1.0f, 1.0f, 0.7f, 3,  true),  1.19f, 1e-3f));// cart flat 1.7
    CHECK(nearf(WalkAnimSpeed(1.0f, 1.0f, 0.7f, 11, true),  1.33f, 1e-3f));// cart stairs 1.9
}

// --- WalkStep waypoint advance + height snapping ----------------------------
TEST(SimCharMove, WalkStepAdvancesAndSnaps) {
    std::uint8_t heights[64];
    render::Heightmap hm = MakeHeightmap(heights);

    // Waypoints (col,row): (1,1),(2,1),(3,1),(3,2).
    std::uint8_t wps[8] = {1,1, 2,1, 3,1, 3,2};

    WalkAvatar av{};
    // Avatar starts at the world position of wp0 (1,1) = (10,2,10), heading 0.
    av.posX = 10.0f; av.posY = 2.0f; av.posZ = 10.0f; av.heading = 0.0f;

    WalkAnim anim{};
    WalkState st{};
    st.activePath = 1; st.waypointCap = 4; st.waypoints = wps;
    st.waypointIdx = 0;  // next WalkStep advances to idx 1
    st.avatar = &av; st.anim = &anim; st.mesh = &hm;

    int cont = WalkStep(&st, 0.0f);
    CHECK_EQ(cont, 1);
    CHECK_EQ(st.waypointIdx, 1);
    // Target snapped to wp(2,1) = (20,3,10).
    CHECK(nearf(st.targetX, 20.0f));
    CHECK(nearf(st.targetY, 3.0f));
    CHECK(nearf(st.targetZ, 10.0f));
    // Segment length |(10,2,10)-(20,3,10)| = sqrt(100+1) = 10.04988.
    CHECK(nearf(st.segLength, 10.0498756f));
    // Turn angle to (2,1): atan2(dx=10, dz=0) - heading 0 = pi/2.
    CHECK(nearf(st.turnAngle, 1.5707963f, 1e-4f));
    // Anim duration written = 20 (mid segment), active flags set.
    CHECK(nearf(anim.duration, 20.0f));
    CHECK(nearf(anim.targetX, 20.0f));
    CHECK((anim.flags110 & 2) != 0);
    CHECK((anim.flags110 & 8) == 0);
}

TEST(SimCharMove, WalkStepFinishesAtEnd) {
    std::uint8_t heights[64];
    render::Heightmap hm = MakeHeightmap(heights);
    std::uint8_t wps[8] = {1,1, 2,1, 3,1, 3,2};

    WalkAvatar av{};
    WalkAnim anim{};
    WalkState st{};
    st.activePath = 1; st.waypointCap = 4; st.waypoints = wps;
    st.waypointIdx = 3;  // already at last index; next advance -> idx 4 >= cap
    st.avatar = &av; st.anim = &anim; st.mesh = &hm;

    int cont = WalkStep(&st, 0.0f);
    CHECK_EQ(cont, 0);          // path exhausted -> finish
    CHECK(st.waypoints == nullptr);
}

// --- Universe transition validation ladder ----------------------------------
TEST(SimCharMove, MoveUniverseInvalidNegative) {
    MoveUniverseState st{};
    st.dataUniverse = -1;
    CHECK(Move2UniverseActionUpdate(&st) == MoveUniverseResult::kInvalidUniverse);
}

TEST(SimCharMove, MoveUniverseInvalidCombo) {
    MoveUniverseState st{};
    // Data0==0 but Data1!=-1 -> invalid combination.
    st.dataUniverse = 0; st.dataId = 5;
    CHECK(Move2UniverseActionUpdate(&st) == MoveUniverseResult::kInvalidCombo);
    // Data0!=0 but Data1==-1 -> invalid combination.
    st.dataUniverse = 3; st.dataId = -1;
    CHECK(Move2UniverseActionUpdate(&st) == MoveUniverseResult::kInvalidCombo);
}

TEST(SimCharMove, MoveUniverseOkClearsScratch) {
    MoveUniverseState st{};
    st.dataUniverse = 2; st.dataId = 7; st.dataSubId = 9;
    st.curUniverse = 2;            // active slot matches dest
    st.scratch48 = 111; st.scratch52 = 222; st.scratch56 = 333;
    CHECK(Move2UniverseActionUpdate(&st) == MoveUniverseResult::kOk);
    CHECK_EQ(st.curUniverse, 7);   // +44 = Data[1]
    CHECK_EQ(st.scratch48, 0);
    CHECK_EQ(st.scratch52, 0);
    CHECK_EQ(st.scratch56, 0);
}

TEST(SimCharMove, MoveUniverseMoveFailed) {
    static auto failMove = [](MoveUniverseState*, int) -> int { return 0; };
    MoveUniverseHooks h{};
    h.moveToUniverse = +failMove;
    h.switchActiveSlot = [](int) {};
    h.setVisible = [](MoveUniverseState*, int) {};
    SetMoveUniverseHooks(&h);

    MoveUniverseState st{};
    st.dataUniverse = 1; st.dataId = 4;
    CHECK(Move2UniverseActionUpdate(&st) == MoveUniverseResult::kMoveFailed);

    SetMoveUniverseHooks(nullptr);  // restore inert
}

// --- Turn-anim selection (TurnByAngleAction) --------------------------------
TEST(SimCharMove, TurnPickAnimation) {
    CHECK_EQ(TurnPickAnimation(0.5), 1);    // >= 0.2 -> left
    CHECK_EQ(TurnPickAnimation(0.2), 1);    // boundary inclusive
    CHECK_EQ(TurnPickAnimation(0.0), 0);    // deadzone
    CHECK_EQ(TurnPickAnimation(-0.2), 0);   // >= -0.2 still deadzone
    CHECK_EQ(TurnPickAnimation(-0.5), -1);  // < -0.2 -> right
}
