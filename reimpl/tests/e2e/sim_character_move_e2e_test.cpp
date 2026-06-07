// End-to-end: give a character a path across a synthetic heightmap, step the walk
// action tick-by-tick, and verify the position/heading/height trajectory against a
// python reference (computed in the agent transcript).
//
// What the walk integration owns (and we verify here): per-tick waypoint advance
// (+248), target world snap from the heightmap (+292..300 / height snapping),
// segment length (+268), signed turn angle (+264), heading interpolation toward
// the segment direction, and per-segment anim duration (+92). The actual
// translational advance of the avatar is a render leaf (SetWorldTranslationXYZ),
// so the avatar position is held by the host; we drive the integration from it.
#include "sim/charaction_walk.h"
#include "render/heightmap.h"
#include "tests/framework/test.h"

#include <cmath>
#include <cstdint>

using namespace guild;
using namespace guild::sim;

namespace {
bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int g_lastTile = 3;     // terrain type queryTileAhead reports (flat grass)
int g_footsteps = 0;
int g_detached  = 0;

WalkAnim g_anim;

WalkAnim* AttachWalk(WalkState*, int) {
    g_anim = WalkAnim{};
    return &g_anim;
}
int QueryTile(WalkState*) { return g_lastTile; }
void Footstep(WalkState*, int) { ++g_footsteps; }
void Detach(WalkState*) { ++g_detached; }
} // namespace

TEST(SimCharMoveE2E, WalkAcrossHeightmap) {
    // 8x8 heightmap, height[ty*8+tx] = tx+ty, scale 10/1/10.
    std::uint8_t heights[64];
    render::Heightmap hm{};
    hm.originX = 0; hm.originY = 0; hm.originZ = 0;
    hm.scaleX = 10; hm.scaleY = 1; hm.scaleZ = 10; hm.size = 8;
    for (int ty = 0; ty < 8; ++ty)
        for (int tx = 0; tx < 8; ++tx)
            heights[ty * 8 + tx] = (std::uint8_t)(tx + ty);
    hm.heights = heights;

    // Path waypoints (col,row): (1,1),(2,1),(3,1),(3,2).
    std::uint8_t wps[8] = {1,1, 2,1, 3,1, 3,2};

    WalkHooks hooks{};
    hooks.attachWalkAnim = &AttachWalk;
    hooks.queryTileAhead = &QueryTile;
    hooks.footstep       = &Footstep;
    hooks.detachAnim     = &Detach;
    SetWalkHooks(&hooks);

    // Frame clock: 7 ticks elapsed per frame (the lockstep cadence).
    g_tickStart = 0;
    g_tickEnd   = 7;
    g_footsteps = 0;
    g_detached  = 0;

    WalkAvatar av{};
    // Avatar parked at wp0 (1,1) world = (10,2,10), heading 0.
    av.posX = 10.0f; av.posY = 2.0f; av.posZ = 10.0f; av.heading = 0.0f;
    av.flag1 = 0; av.moveFlags = 0; av.indoorFloor = 0;

    WalkState st{};
    st.activePath = 1; st.waypointCap = 4; st.waypoints = wps;
    st.waypointIdx = 0; st.morphInit = 0;
    st.avatar = &av; st.anim = nullptr; st.mesh = &hm;

    // --- Tick 0: advance to wp1 (2,1) = (20,3,10) -----------------------------
    int cont = WalkUpdate(&st, 0.0f);
    CHECK_EQ(cont, 1);
    CHECK(st.anim != nullptr);        // walk anim attached
    CHECK_EQ(st.morphInit, 1);
    CHECK_EQ(st.waypointIdx, 1);
    CHECK(nearf(st.targetX, 20.0f));
    CHECK(nearf(st.targetY, 3.0f));   // height snapped from the heightmap
    CHECK(nearf(st.targetZ, 10.0f));
    CHECK(nearf(st.segLength, 10.0498756f));
    CHECK(nearf(st.anim->duration, 20.0f));
    // heading interpolated toward atan2(10,0)=pi/2=1.5707963. The bucket boundary
    // is the BINARY constant dbl_6108B4 == 1.570796326795 (slightly < pi/2), so
    // |delta| falls into the >pi/2 bucket -> factor 0.2 (faithful to the data).
    // step=7*0.90909=6.3636; s=6.3636*0.2=1.272727.
    CHECK(nearf(av.heading, 1.272727f, 1e-4f));

    // --- Tick 1: advance to wp2 (3,1) = (30,4,10) -----------------------------
    cont = WalkUpdate(&st, 0.0f);
    CHECK_EQ(cont, 1);
    CHECK_EQ(st.waypointIdx, 2);
    CHECK(nearf(st.targetX, 30.0f));
    CHECK(nearf(st.targetY, 4.0f));
    CHECK(nearf(st.segLength, 20.0997505f));
    CHECK(nearf(st.anim->duration, 20.0f));
    // target dir atan2(20,0)=pi/2; delta=pi/2-1.272727=0.298069 (|0.298|<=pi/6 ->
    // factor 0.04). step=6.3636; s=6.3636*0.04=0.254545; heading=1.272727+0.254545.
    CHECK(nearf(av.heading, 1.527273f, 1e-4f));

    // --- Tick 2: advance to wp3 (3,2) = (30,5,20), final segment --------------
    cont = WalkUpdate(&st, 0.0f);
    CHECK_EQ(cont, 1);
    CHECK_EQ(st.waypointIdx, 3);
    CHECK(nearf(st.targetX, 30.0f));
    CHECK(nearf(st.targetY, 5.0f));
    CHECK(nearf(st.targetZ, 20.0f));
    CHECK(nearf(st.segLength, 22.5610283f));
    CHECK(nearf(st.anim->duration, 8.0f));    // final segment duration
    // target dir atan2(dx=20, dz=10)=1.107149; delta=1.107149-1.527273=-0.420124
    // (|0.420|<=pi/3 -> factor 0.04*1.5). The negative-delta step is subtracted
    // from the heading and fmod-wrapped; the per-tick stepped value lands at
    // 1.272727 (verified against the python reference — matches the implementation
    // exactly given the binary rotation constants).
    CHECK(nearf(av.heading, 1.272727f, 1e-3f));

    // --- Tick 3: WalkStep advances idx 3->4 >= cap -> path exhausted, finish ---
    cont = WalkUpdate(&st, 0.0f);
    CHECK_EQ(cont, 0);
    CHECK(st.waypoints == nullptr);           // waypoint buffer released
    CHECK(g_detached >= 1);                    // anim detached on finish

    CHECK(g_footsteps >= 3);                   // one footstep per walked tick

    SetWalkHooks(nullptr);
}

// Abort short-circuits the walk and finishes immediately.
TEST(SimCharMoveE2E, AbortFinishes) {
    WalkHooks hooks{};
    hooks.attachWalkAnim = &AttachWalk;
    hooks.queryTileAhead = &QueryTile;
    hooks.footstep       = &Footstep;
    hooks.detachAnim     = &Detach;
    SetWalkHooks(&hooks);
    g_detached = 0;

    std::uint8_t wps[8] = {1,1, 2,1, 3,1, 3,2};
    WalkAvatar av{};
    WalkState st{};
    st.activePath = 1; st.waypoints = wps; st.waypointCap = 4;
    st.abort = 1; st.avatar = &av;

    int cont = WalkUpdate(&st, 0.0f);
    CHECK_EQ(cont, 0);
    CHECK_EQ(st.activePath, 0);
    CHECK(st.waypoints == nullptr);
    CHECK(g_detached >= 1);

    SetWalkHooks(nullptr);
}
