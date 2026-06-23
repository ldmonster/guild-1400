// ===========================================================================
// npcevent_reaper_full_test — golden tests for the FULL Reaper event functions
// (gilde.exe 0x4d8c34 / 0x4d8f74 / 0x4d92a4 / 0x4d9440). Drive each function over
// a fake He record with injected coupled-leaf hooks; assert the He fields written
// (spawned flag, position triples, last-tick) and the return codes. Headless.
// Unique suite prefix: ReaperFull.
// ===========================================================================
#include "tests/framework/test.h"

#include "sim/npcevent_reaper_full.h"
#include "sim/actionqueue.h"  // guild::sim::g_gameTick

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i32;
using guild::u32;
// f32 comes from guild::sim (recon4 header) via the using-directive above.

namespace {

// A backing buffer big enough for every reaper He offset (max +236 + 4).
struct FakeHe {
    unsigned char bytes[512];
    FakeHe() { std::memset(bytes, 0, sizeof(bytes)); }
    HeRecord* rec() { return reinterpret_cast<HeRecord*>(bytes); }
    i32& at_i32(int off) { return *reinterpret_cast<i32*>(bytes + off); }
    f32& at_f32(int off) { return *reinterpret_cast<f32*>(bytes + off); }
    u32& at_u32(int off) { return *reinterpret_cast<u32*>(bytes + off); }
};

// Recording mock for the coupled leaves.
struct Mock {
    // gate controls
    bool srcOk = true, tgtOk = true, nodeOk = true, attachOk = true;
    int moveBlocked = 0;
    // supplied points
    Vec3 srcPt{10, 20, 30};
    Vec3 tgtPt{100, 0, 200};         // y forced 0 for planar dist in move
    Vec3 nodePt{50, 0, 60};
    Vec3 attachEuler{1, 2, 3};
    Vec3 attachPos{7, 8, 9};
    Vec3 poseEuler{4, 5, 6};
    Vec3 posePos{11, 12, 13};
    f32  terrain = 0.0f;
    // capture
    int slotEnter = 0, slotLeave = 0;
    bool animLoaded = false;
    bool setPosCalled = false; Vec3 setPos{};
    bool soundCalled = false; Vec3 soundListener{}; Vec3 soundFacing{};
    i32 lastSrcId = -999, lastTgtId = -999;
};

Mock* g_m = nullptr;

ReaperFullHooks MakeHooks() {
    ReaperFullHooks h{};
    // dword_649D60: a non-zero saved slot so enter (target 0) and leave (target
    // == saved slot, here 7) are distinguishable.
    h.getActiveSceneSlot = []() { return 7; };
    h.switchActiveSlot = [](int t, int, int) {
        if (t == 0) g_m->slotEnter++; else g_m->slotLeave++;
    };
    h.resolveSourcePoint = [](HeRecord*, i32 id, Vec3* out) {
        g_m->lastSrcId = id; if (!g_m->srcOk) return false; *out = g_m->srcPt; return true;
    };
    h.resolveTargetPoint = [](HeRecord*, i32 id, Vec3* out) {
        g_m->lastTgtId = id; if (!g_m->tgtOk) return false; *out = g_m->tgtPt; return true;
    };
    h.resolveNodePoint = [](HeRecord*, Vec3* out) {
        if (!g_m->nodeOk) return false; *out = g_m->nodePt; return true;
    };
    h.terrainHeight = [](const Vec3&) { return g_m->terrain; };
    h.nodeMoveBlocked = [](HeRecord*) { return g_m->moveBlocked; };
    h.attachReaperNode = [](HeRecord*, const Vec3&, Vec3* e, Vec3* p) -> void* {
        if (!g_m->attachOk) return nullptr;
        *e = g_m->attachEuler; *p = g_m->attachPos;
        return reinterpret_cast<void*>(0xBEEF);
    };
    h.loadReaperAnim = [](void*) { g_m->animLoaded = true; };
    h.readNodePose = [](HeRecord*, Vec3* e, Vec3* p) { *e = g_m->poseEuler; *p = g_m->posePos; };
    h.setNodePosition = [](HeRecord*, const Vec3& p) { g_m->setPosCalled = true; g_m->setPos = p; };
    h.sound3dSetListener = [](HeRecord*, const Vec3& l, const Vec3& f) {
        g_m->soundCalled = true; g_m->soundListener = l; g_m->soundFacing = f;
    };
    return h;
}

constexpr int OFF_SRC = 176, OFF_TGT = 180, OFF_SPAWN = 196;
constexpr int OFF_CUR = 208, OFF_PREV = 224, OFF_TICK = 236;

} // namespace

// ---------------------------------------------------------------------------
TEST(ReaperFull, ApproachFirstSpawn) {
    Mock m; g_m = &m;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);
    g_gameTick = 4242;

    FakeHe fh;
    fh.at_i32(OFF_SRC) = 11;
    fh.at_i32(OFF_TGT) = 22;
    fh.at_i32(OFF_SPAWN) = 0;          // first spawn
    ReaperFull_SetNode(fh.rec(), nullptr);

    int rc = ReaperApproachTarget(fh.rec());

    CHECK_EQ(rc, 1);
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 1);           // +196 set
    CHECK_EQ(m.lastSrcId, 11);
    CHECK_EQ(m.lastTgtId, 22);
    CHECK(m.animLoaded);
    CHECK_EQ((int)fh.at_u32(OFF_TICK), 4242);    // +236 latched
    CHECK(ReaperFull_GetNode(fh.rec()) != nullptr);
    // prev pose (+224..) = attach euler ; cur pose (+208..) = attach pos
    CHECK_EQ(fh.at_f32(OFF_PREV + 0), 1.0f);
    CHECK_EQ(fh.at_f32(OFF_PREV + 8), 3.0f);
    CHECK_EQ(fh.at_f32(OFF_CUR + 0), 7.0f);
    CHECK_EQ(fh.at_f32(OFF_CUR + 8), 9.0f);
    // slot bracket: entered once (target 0), left once (target == saved slot 7)
    CHECK_EQ(m.slotEnter, 1);
    CHECK_EQ(m.slotLeave, 1);
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, ApproachGateFail) {
    Mock m; g_m = &m; m.srcOk = false;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh; fh.at_i32(OFF_SPAWN) = 0;
    int rc = ReaperApproachTarget(fh.rec());
    CHECK_EQ(rc, 0);
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 0);   // unchanged
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, ApproachAttachFail) {
    Mock m; g_m = &m; m.attachOk = false;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh; fh.at_i32(OFF_SPAWN) = 0;
    int rc = ReaperApproachTarget(fh.rec());
    CHECK_EQ(rc, 0);
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 0);
    CHECK(ReaperFull_GetNode(fh.rec()) == nullptr);
    SetReaperFullHooks(nullptr);
}

// ---------------------------------------------------------------------------
TEST(ReaperFull, MoveArrived) {
    Mock m; g_m = &m;
    // target very close to node so planar distance < 45 -> arrived (code 2).
    m.tgtPt = Vec3{50, 0, 60};   // same as nodePt (50,0,60) -> dist 0
    m.nodePt = Vec3{50, 0, 60};
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    fh.at_i32(OFF_TGT) = 77;
    fh.at_i32(OFF_SPAWN) = 1;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));

    int rc = ReaperMoveTowardTarget(fh.rec());
    CHECK_EQ(rc, 2);                       // arrived
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 0);     // +196 cleared on arrival
    CHECK(!m.setPosCalled);                // no movement write
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, MoveStepping) {
    Mock m; g_m = &m;
    m.tgtPt = Vec3{1000, 0, 1000};  // far -> planar dist >> 45 -> moving (code 1)
    m.nodePt = Vec3{0, 0, 0};
    m.terrain = -100000.0f;          // keep terrain below so hop clamp never fires
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);
    g_gameTick = 1000;

    FakeHe fh;
    fh.at_i32(OFF_TGT) = 88;
    fh.at_i32(OFF_SPAWN) = 1;
    fh.at_u32(OFF_TICK) = 990;       // dt = 10 ticks
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));

    int rc = ReaperMoveTowardTarget(fh.rec());
    CHECK_EQ(rc, 1);                       // moving
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 1);     // still spawned
    CHECK(m.setPosCalled);                 // SetPosition invoked
    CHECK_EQ((int)fh.at_u32(OFF_TICK), 1000);  // tick latched
    // prev pose refreshed from node euler (4,5,6)
    CHECK_EQ(fh.at_f32(OFF_PREV + 0), 4.0f);
    CHECK_EQ(fh.at_f32(OFF_PREV + 8), 6.0f);
    // cur pos (+208..) == the new stepped position == setPos
    CHECK_EQ(fh.at_f32(OFF_CUR + 0), m.setPos.x);
    CHECK_EQ(fh.at_f32(OFF_CUR + 4), m.setPos.y);
    CHECK_EQ(fh.at_f32(OFF_CUR + 8), m.setPos.z);
    // moved toward +x/+z from origin
    CHECK(m.setPos.x > 0.0f);
    CHECK(m.setPos.z > 0.0f);
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, MoveBlockedLatchesTick) {
    Mock m; g_m = &m; m.moveBlocked = 1;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);
    g_gameTick = 555;

    FakeHe fh;
    fh.at_i32(OFF_TGT) = 9;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));

    int rc = ReaperMoveTowardTarget(fh.rec());
    CHECK_EQ(rc, 1);                        // blocked -> 1
    CHECK_EQ((int)fh.at_u32(OFF_TICK), 555);// tick latched
    CHECK(!m.setPosCalled);                 // no movement
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, MoveGateFailNoNode) {
    Mock m; g_m = &m;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    ReaperFull_SetNode(fh.rec(), nullptr);   // no node -> gate fail
    int rc = ReaperMoveTowardTarget(fh.rec());
    CHECK_EQ(rc, 0);
    SetReaperFullHooks(nullptr);
}

// ---------------------------------------------------------------------------
TEST(ReaperFull, CachePose) {
    Mock m; g_m = &m;
    m.poseEuler = Vec3{21, 22, 23};
    m.nodePt = Vec3{31, 32, 33};
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    fh.at_i32(OFF_TGT) = 5;
    fh.at_i32(OFF_SPAWN) = 0;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));

    int rc = ReaperCacheTargetPose(fh.rec());
    CHECK_EQ(rc, 1);
    CHECK_EQ(fh.at_i32(OFF_SPAWN), 1);      // +196 set
    CHECK_EQ(m.lastTgtId, 5);
    // prev pose = node euler ; cur pos = the reaper's own bone point (nodePt)
    CHECK_EQ(fh.at_f32(OFF_PREV + 0), 21.0f);
    CHECK_EQ(fh.at_f32(OFF_PREV + 8), 23.0f);
    CHECK_EQ(fh.at_f32(OFF_CUR + 0), 31.0f);
    CHECK_EQ(fh.at_f32(OFF_CUR + 8), 33.0f);
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, CachePoseGateFail) {
    Mock m; g_m = &m; m.tgtOk = false;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));
    int rc = ReaperCacheTargetPose(fh.rec());
    CHECK_EQ(rc, 0);
    SetReaperFullHooks(nullptr);
}

// ---------------------------------------------------------------------------
TEST(ReaperFull, UpdateSound) {
    Mock m; g_m = &m;
    m.nodePt = Vec3{41, 42, 43};
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    fh.at_i32(OFF_TGT) = 6;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));

    int rc = ReaperUpdateSoundPos(fh.rec());
    CHECK_EQ(rc, 1);
    CHECK(m.soundCalled);
    CHECK_EQ(m.soundListener.x, 41.0f);     // listener = reaper bone point
    CHECK_EQ(m.soundListener.z, 43.0f);
    SetReaperFullHooks(nullptr);
}

TEST(ReaperFull, UpdateSoundGateFail) {
    Mock m; g_m = &m; m.nodeOk = false;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);

    FakeHe fh;
    ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1234));
    int rc = ReaperUpdateSoundPos(fh.rec());
    CHECK_EQ(rc, 0);
    CHECK(!m.soundCalled);
    SetReaperFullHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Math-kernel cross-check: the planar arrival threshold is exactly 45.0.
TEST(ReaperFull, ArriveThresholdBoundary) {
    // dist 44 -> arrived ; dist 46 -> moving.
    Mock m; g_m = &m;
    ReaperFullHooks h = MakeHooks();
    SetReaperFullHooks(&h);
    g_gameTick = 10;

    {
        m.nodePt = Vec3{0, 0, 0};
        m.tgtPt = Vec3{44, 0, 0};
        FakeHe fh; fh.at_i32(OFF_SPAWN) = 1;
        ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1));
        CHECK_EQ(ReaperMoveTowardTarget(fh.rec()), 2);  // within 45 -> arrived
    }
    {
        m.nodePt = Vec3{0, 0, 0};
        m.tgtPt = Vec3{46, 0, 0};
        m.terrain = -1e9f;
        FakeHe fh; fh.at_i32(OFF_SPAWN) = 1; fh.at_u32(OFF_TICK) = 0;
        ReaperFull_SetNode(fh.rec(), reinterpret_cast<void*>(0x1));
        CHECK_EQ(ReaperMoveTowardTarget(fh.rec()), 1);  // beyond 45 -> moving
    }
    SetReaperFullHooks(nullptr);
}
