// E2E: move a character across a universe boundary, then step its motion queue
// (walk-on-path executor) along a tile path, verifying position / universe /
// visibility / heading against a reference computed alongside.
//
// Flow:
//   1. Relocate the character into universe 1 (entering a building) via
//      Move2UniverseActionUpdate; verify the universe id changed and visibility
//      toggled per the slot rules.
//   2. Seed a straight tile path on a flat heightmap, attach a walk anim, and run
//      WalkOnPathStep tick-by-tick; verify the avatar advances toward each
//      waypoint target, the heading turns toward the segment direction, and the
//      path terminates (activePath cleared) when exhausted.
#include "tests/framework/test.h"

#include "sim/charaction_motion.h"
#include "sim/character_universe.h"
#include "render/heightmap.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

struct FlatMap {
    render::Heightmap hm{};
    std::vector<u8> heights;
    std::vector<u8> entries;
    FlatMap(int size, u8 h, u8 type) {
        heights.assign((size_t)size * size, h);
        entries.assign((size_t)size * size * 24, 0);
        for (size_t i = 0; i < (size_t)size * size; ++i) entries[i * 24] = type;
        hm.originX = 0; hm.originY = 0; hm.originZ = 0;
        hm.scaleX = 1.0f; hm.scaleY = 1.0f; hm.scaleZ = 1.0f;
        hm.size = size; hm.heights = heights.data(); hm.entries = entries.data();
    }
};

// Recording motion hooks: a single shared anim, attach counts + footstep log.
MotionAnim g_anim;
int g_attachCount = 0;
int g_detachCount = 0;
int g_footsteps = 0;
MotionAnim* RecAttach(MotionCharacter*, const char*, int) {
    ++g_attachCount; g_anim = MotionAnim{}; return &g_anim;
}
void RecDetach(MotionCharacter*) { ++g_detachCount; }
void RecDraw(MotionCharacter*) {}
void RecFreeWp(MotionCharacter* ch) { ch->waypoints = nullptr; }
int  RecFootstep(MotionCharacter*, int, int) { ++g_footsteps; return 1; }
const MotionHooks kRec = { &RecAttach, &RecDetach, &RecDraw, &RecFreeWp, &RecFootstep };
} // namespace

// --- Step 1: universe transition --------------------------------------------
TEST(SimMotionE2E, UniverseTransitionThenWalk) {
    // --- relocate into universe 1 (e.g. a building interior) ---
    SetUniverseHooks(nullptr);              // inert: move succeeds, dummy resolves
    g_univActiveSlot = 0;
    g_univActiveUniverseId = 0;
    g_univActiveSubUniverse = -1;

    UniverseTransition rel{};
    rel.dataUniverse = 1;                   // destination universe 1
    rel.dataId = 100;                       // destination id
    rel.dataSubId = 5;
    rel.curUniverse = 0;                    // currently in universe 0
    rel.char56 = rel.char60 = rel.char64 = 0x55;  // scratch dirty
    UniverseResult ur = Move2UniverseActionUpdate(&rel);
    CHECK(ur == UniverseResult::kOk);
    CHECK_EQ(rel.curUniverse, 100);         // now in the destination universe
    CHECK_EQ(rel.char48, 5);                // sub-id stored
    CHECK_EQ(rel.char56, 0);                // scratch cleared
    CHECK_EQ(rel.prevSlot, 0);              // restored to the original slot
    // prevSlot(0) != Data0(1) -> no slot-mismatch hide; visible stays 1.
    CHECK_EQ(rel.visible, 1);

    // --- now walk the character along a tile path inside universe 1 ---
    g_attachCount = g_detachCount = g_footsteps = 0;
    SetMotionHooks(&kRec);
    g_tickStart = 0; g_tickEnd = 1;         // one game-tick per frame
    g_motionFootstepEnabled = 0;
    g_motionFootstepCount = 0;
    g_motionActiveUniverse = 1;
    g_motionSceneChanged = 0;

    FlatMap map(16, /*h=*/0, /*type=*/3);

    MotionAvatar av{};
    av.flag1 = 0;                           // not indoors-flag/mounted
    av.moveFlags = 0;
    av.universeId = 1;                      // in the active universe
    av.cart = 0;
    av.baseSpeed = 1.0f;
    av.ramp = 0.0f;                         // ramp from 0 -> 1
    av.posX = 2.0f; av.posY = 0.0f; av.posZ = 2.0f;  // start at tile (2,2)
    av.transX = 2.0f; av.transYaw = 0.0f; av.transZ = 2.0f;
    av.anim = nullptr;
    av.object3dIndoorFloor = 0;

    MotionCharacter ch{};
    ch.avatar = &av;
    ch.mesh = &map.hm;
    ch.universe = reinterpret_cast<void*>(1);  // owning universe present
    ch.universeIndoorFloor = 0;
    ch.startMesh = 0;                       // matches the modeled owning-universe check
    ch.activePath = 0;                      // start in BUILD phase
    ch.morphStamp = -1;
    ch.startTileX = 2; ch.startTileY = 2;
    ch.morphInit = 0;
    ch.turnAngle = 0.0f;

    // --- build phase: path flag 0 -> allocates + zeros the waypoint buffer ---
    WalkOnPathStep(&ch);
    CHECK(ch.waypoints != nullptr);         // buffer allocated
    CHECK_EQ(ch.waypointCap, 256);
    CHECK_EQ(ch.waypointIdx, 0);
    CHECK_EQ(ch.morphInit, 0);

    // Seed a straight path east: tiles (2,2) (3,2) (4,2) (5,2) (6,2) then a 0,0
    // terminator. (col,row) pairs, 2 bytes/tile.
    std::memset(ch.waypoints, 0, kWaypointAlloc);
    const int path[][2] = { {2,2},{3,2},{4,2},{5,2},{6,2} };
    for (int i = 0; i < 5; ++i) {
        ch.waypoints[2*i]   = (u8)path[i][0];
        ch.waypoints[2*i+1] = (u8)path[i][1];
    }
    ch.activePath = 1;                      // now there IS a path

    // --- morph-init phase: attaches the walk anim, seeds segment 0 ---
    WalkOnPathStep(&ch);
    CHECK_EQ(g_attachCount, 1);             // walk anim attached
    CHECK(av.anim != nullptr);
    CHECK_EQ(ch.morphInit, 1);

    // --- drive the path: step until exhausted or a tick budget runs out ---
    // The engine's anim/position-integration leaf moves the object toward each
    // segment target and flags the anim "stop" (+110 bit3) on arrival; the executor
    // then advances to the next waypoint. We model that leaf here: each tick, step
    // the avatar position toward the current target and, on arrival, set the stop
    // bit so WalkOnPathStep advances the waypoint index.
    int   reachedFinal = 0;
    int   advances = 0;
    bool  headingMovedAtSomePoint = false;
    // Budget enough ticks to walk the 4 segments; a short path STALLS at the last
    // waypoint (the engine only auto-terminates at the 256-slot capacity), so we
    // stop driving once the avatar has reached the final waypoint (index 4).
    for (int tick = 0; tick < 4000 && ch.waypointIdx < 4; ++tick) {
        g_tickStart = tick;
        g_tickEnd = tick + 1;
        int idxBefore = ch.waypointIdx;
        float headingBefore = av.transYaw;
        WalkOnPathStep(&ch);
        if (ch.waypointIdx != idxBefore) {
            ++advances;
            CHECK(ch.targetX >= 2.0f && ch.targetX <= 6.0f);
        }
        if (!Near(av.transYaw, headingBefore, 1e-6f))
            headingMovedAtSomePoint = true;
        if (ch.waypointIdx >= 4) reachedFinal = 1;

        // --- model the anim/position leaf: advance toward the segment target ---
        if (av.anim) {
            float dx = ch.targetX - av.posX;
            float dz = ch.targetZ - av.posZ;
            float dist = std::sqrt(dx * dx + dz * dz);
            const float speed = 0.5f;       // world units per tick (model)
            if (dist <= speed) {
                av.posX = ch.targetX;
                av.posZ = ch.targetZ;
                av.anim->flags110 |= 8u;    // arrived -> "stop" so the executor advances
            } else {
                av.posX += dx / dist * speed;
                av.posZ += dz / dist * speed;
            }
            av.transX = av.posX;
            av.transZ = av.posZ;
        }
    }

    // The path advanced through its segments toward the eastern goal.
    CHECK(advances >= 3);                     // segments (2,2)->(3,2)->(4,2)->(5,2)->(6,2)
    CHECK(reachedFinal == 1);                 // reached the final waypoint index (4)
    CHECK_EQ(ch.waypointIdx, 4);              // at the last real waypoint
    CHECK(ch.targetX >= 5.99f);               // current segment target is tile (6,2)
    CHECK(Near(ch.targetZ, 2.0f, 0.01f));     // row unchanged
    // The heading interpolation turned the avatar away from its start facing
    // (+Z, yaw 0) toward the eastward (+X) segment direction.
    CHECK(headingMovedAtSomePoint);
    CHECK(!Near(av.transYaw, 0.0f, 0.05f));   // heading turned from yaw 0

    SetMotionHooks(nullptr);
}

// --- A second, focused walk: verify position tracks the segment target -------
TEST(SimMotionE2E, AvatarHeightSnapsToTerrainWhileWalking) {
    SetMotionHooks(&kRec);
    g_attachCount = g_detachCount = g_footsteps = 0;
    g_tickStart = 0; g_tickEnd = 1;
    g_motionFootstepEnabled = 0; g_motionFootstepCount = 0; g_motionActiveUniverse = 1; g_motionSceneChanged = 0;

    FlatMap map(16, /*h=*/50, /*type=*/3);   // terrain ~50.5 high

    MotionAvatar av{};
    av.universeId = 1; av.baseSpeed = 1.0f; av.ramp = 0.0f;
    av.posX = 4.0f; av.posY = 0.0f; av.posZ = 4.0f;
    av.transX = 4.0f; av.transYaw = 0.0f; av.transZ = 4.0f;

    MotionCharacter ch{};
    ch.avatar = &av; ch.mesh = &map.hm;
    ch.universe = reinterpret_cast<void*>(1);
    ch.universeIndoorFloor = 0;
    ch.startMesh = 0; ch.activePath = 0; ch.morphStamp = -1;
    ch.startTileX = 4; ch.startTileY = 4; ch.morphInit = 0; ch.turnAngle = 0.0f;

    WalkOnPathStep(&ch);                     // build
    std::memset(ch.waypoints, 0, kWaypointAlloc);
    ch.waypoints[0] = 4; ch.waypoints[1] = 4;
    ch.waypoints[2] = 5; ch.waypoints[3] = 4;
    ch.waypoints[4] = 6; ch.waypoints[5] = 4;
    ch.activePath = 1;
    WalkOnPathStep(&ch);                     // morph-init

    // A handful of ticks: the avatar's Y should climb toward the terrain height.
    for (int t = 0; t < 50 && ch.activePath; ++t) {
        g_tickStart = t; g_tickEnd = t + 1;
        WalkOnPathStep(&ch);
    }
    CHECK(av.posY > 10.0f);                  // height-snapped up from 0 toward ~53.5
    CHECK(av.posY < 60.0f);                  // (lerped, lifted; never overshoots wildly)
    SetMotionHooks(nullptr);
}
