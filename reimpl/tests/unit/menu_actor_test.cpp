// Golden unit tests for VIBE_Character_CreateMenuDummyActor @0x52af64 (sim/menu_actor).
// No assets. A recording mock MenuActorHooks captures every leaf call; we assert the
// three branches (dummy-not-found, char-create-fails, success) and the success-path
// field configuration + the facing-yaw math reproduced from the reconstructed
// guild::util primitives.
#include "tests/framework/test.h"
#include "sim/menu_actor.h"

#include <cmath>
#include <cstring>
#include <string>

using namespace guild::sim;

namespace {

// A recording mock. `dummy`/`chr`/`node` are the opaque handles handed back; the dummy
// handle doubles as the float* frame buffer fed to RotateVectorByHierarchy.
struct Mock {
    // What the lookup / create hooks return:
    void* dummyToReturn = nullptr;
    void* charToReturn  = nullptr;
    void* nodeToReturn  = nullptr;
    float dummyWorld[3] = {0, 0, 0};

    // Recorded calls:
    int  findCalls = 0, getPosCalls = 0, createCalls = 0, objNodeCalls = 0;
    int  setRotCalls = 0, queryCalls = 0, backrefCalls = 0, dirtyCalls = 0;
    int  preloadCalls = 0, statusCalls = 0;
    float lastRot[3] = {0, 0, 0};
    void* backrefNode = nullptr;
    void* backrefChr  = nullptr;
    std::string lastModel;
};

Mock* g_mock = nullptr;

void* HFind(int /*name*/, const char* model) {
    g_mock->findCalls++; g_mock->lastModel = model ? model : "";
    return g_mock->dummyToReturn;
}
void HGetPos(void* /*d*/, float out3[3]) {
    g_mock->getPosCalls++;
    out3[0] = g_mock->dummyWorld[0]; out3[1] = g_mock->dummyWorld[1];
    out3[2] = g_mock->dummyWorld[2];
}
void* HCreate(const char* /*model*/) { g_mock->createCalls++; return g_mock->charToReturn; }
void* HObjNode(void* /*chr*/)        { g_mock->objNodeCalls++; return g_mock->nodeToReturn; }
void  HSetRot(void* /*n*/, const float v[3]) {
    g_mock->setRotCalls++;
    g_mock->lastRot[0] = v[0]; g_mock->lastRot[1] = v[1]; g_mock->lastRot[2] = v[2];
}
void  HQuery(void* /*chr*/)          { g_mock->queryCalls++; }
void  HBackref(void* n, void* c)     { g_mock->backrefCalls++; g_mock->backrefNode = n; g_mock->backrefChr = c; }
void  HDirty(void* /*n*/)            { g_mock->dirtyCalls++; }
void  HPreload(void* /*chr*/)        { g_mock->preloadCalls++; }
void  HStatus(void* /*n*/)           { g_mock->statusCalls++; }

MenuActorHooks MakeHooks() {
    MenuActorHooks h;
    h.findDummyObject          = HFind;
    h.getDummyWorldPos         = HGetPos;
    h.createCharacterFromModel = HCreate;
    h.charObjectNode           = HObjNode;
    h.setWorldRotation         = HSetRot;
    h.queryTerrainType         = HQuery;
    h.linkNodeBackref          = HBackref;
    h.propagateDirtyFlag       = HDirty;
    h.preloadGaitAnim          = HPreload;
    h.registerStatusText       = HStatus;
    return h;
}

// Build a frame buffer whose 3x3 hierarchy rotation maps the +Z forward {0,0,1} to a
// chosen world direction. RotateVectorByHierarchy reads the 3x3 at float indices
// 99..109 (cols 99/100/101, 103/104/105, 107/108/109) and the parent link at byte 504
// (float index 126). For input {0,0,1} only the third column (indices 107/108/109)
// matters: out = {f[107], f[108], f[109]}. We size the buffer past index 126 and zero
// it so the parent link is null (single-level rotation).
struct Frame {
    float buf[160];
    Frame() { std::memset(buf, 0, sizeof(buf)); }
    void SetForward(float x, float y, float z) { buf[107] = x; buf[108] = y; buf[109] = z; }
    float* ptr() { return buf; }
};

} // namespace

// (a) Dummy not found -> returns null, nothing else runs.
TEST(MenuActor, DummyNotFoundReturnsNull) {
    Mock m; g_mock = &m;
    m.dummyToReturn = nullptr;
    MenuActorHooks h = MakeHooks(); SetMenuActorHooks(&h);

    MenuActorRecord rec;
    void* r = CreateMenuDummyActor(/*name*/ 100, "dummy/model", &rec);

    CHECK(r == nullptr);
    CHECK(!rec.found);
    CHECK_EQ(m.findCalls, 1);
    CHECK_EQ(m.getPosCalls, 0);
    CHECK_EQ(m.createCalls, 0);
    CHECK_EQ(m.statusCalls, 0);
    SetMenuActorHooks(nullptr);
}

// (b) Char create fails -> returns null after the dummy lookup + world-pos sample.
TEST(MenuActor, CharCreateFailsReturnsNull) {
    Mock m; g_mock = &m;
    Frame frame; frame.SetForward(1, 0, 0);
    m.dummyToReturn = frame.ptr();
    m.charToReturn  = nullptr;     // create fails
    m.dummyWorld[0] = 7.0f; m.dummyWorld[1] = 8.0f; m.dummyWorld[2] = 9.0f;
    MenuActorHooks h = MakeHooks(); SetMenuActorHooks(&h);

    MenuActorRecord rec;
    void* r = CreateMenuDummyActor(/*name*/ 101, "dummy/model", &rec);

    CHECK(r == nullptr);
    CHECK(rec.found);
    CHECK(!rec.created);
    CHECK_EQ(m.findCalls, 1);
    CHECK_EQ(m.getPosCalls, 1);
    CHECK_EQ(m.createCalls, 1);
    // World position is sampled before the create check.
    CHECK(rec.placedPos[0] == 7.0f && rec.placedPos[1] == 8.0f && rec.placedPos[2] == 9.0f);
    // No actor configuration ran:
    CHECK_EQ(m.setRotCalls, 0);
    CHECK_EQ(m.queryCalls, 0);
    CHECK_EQ(m.backrefCalls, 0);
    CHECK_EQ(m.dirtyCalls, 0);
    CHECK_EQ(m.preloadCalls, 0);
    CHECK_EQ(m.statusCalls, 0);
    SetMenuActorHooks(nullptr);
}

// (c) Success path: a dummy rotated so forward -> {1,0,0} (a +90 deg yaw of {0,0,1})
//     yields facing yaw = -pi/2; all fields configured; gait preloaded; backref linked.
TEST(MenuActor, SuccessConfiguresActor) {
    Mock m; g_mock = &m;
    Frame frame; frame.SetForward(1, 0, 0);   // RotateVectorByHierarchy({0,0,1}) -> {1,0,0}
    int charObj = 0, nodeObj = 0;
    m.dummyToReturn = frame.ptr();
    m.charToReturn  = &charObj;
    m.nodeToReturn  = &nodeObj;
    MenuActorHooks h = MakeHooks(); SetMenuActorHooks(&h);

    MenuActorRecord rec;
    void* r = CreateMenuDummyActor(/*name*/ 102, " ancestor/model", &rec);

    // Returned the created character actor.
    CHECK(r == &charObj);
    CHECK(rec.found);
    CHECK(rec.created);

    // Facing yaw: VectorAngleBetween({0,0,1},{1,0,0}) == -pi/2.
    const float kPiHalf = 1.5707964f;
    CHECK(std::fabs(rec.facingYaw - (-kPiHalf)) < 1e-4f);
    // The rotation vector pushed to the node is {0, yaw, 0}.
    CHECK(m.lastRot[0] == 0.0f);
    CHECK(std::fabs(m.lastRot[1] - (-kPiHalf)) < 1e-4f);
    CHECK(m.lastRot[2] == 0.0f);

    // Field configuration.
    CHECK(std::fabs(rec.scale - 0.6666667f) < 1e-6f);
    CHECK_EQ(rec.slotMarker, -1);
    CHECK_EQ(rec.typeByte, 4);
    CHECK_EQ(rec.active, 1);

    // The genuine leaf sequence ran exactly once each.
    CHECK_EQ(m.setRotCalls, 1);
    CHECK_EQ(m.queryCalls, 1);
    CHECK_EQ(m.dirtyCalls, 1);
    CHECK_EQ(m.statusCalls, 1);

    // Gait "bewegung/gehen" preloaded.
    CHECK(rec.gaitPreloaded);
    CHECK_EQ(m.preloadCalls, 1);
    CHECK(std::strcmp(kBewegungGehen, "bewegung/gehen") == 0);

    // Node backref linked to the char record.
    CHECK(rec.backrefLinked);
    CHECK_EQ(m.backrefCalls, 1);
    CHECK(m.backrefNode == &nodeObj);
    CHECK(m.backrefChr  == &charObj);
    SetMenuActorHooks(nullptr);
}

// Forward exactly equal to the reference {0,0,1} -> coincident -> yaw 0.
TEST(MenuActor, ForwardAlignedYieldsZeroYaw) {
    Mock m; g_mock = &m;
    Frame frame; frame.SetForward(0, 0, 1);   // already +Z
    int charObj = 0, nodeObj = 0;
    m.dummyToReturn = frame.ptr();
    m.charToReturn  = &charObj;
    m.nodeToReturn  = &nodeObj;
    MenuActorHooks h = MakeHooks(); SetMenuActorHooks(&h);

    MenuActorRecord rec;
    CreateMenuDummyActor(103, "model", &rec);
    CHECK(rec.facingYaw == 0.0f);
    SetMenuActorHooks(nullptr);
}
