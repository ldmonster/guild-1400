// Unit tests for the Character render2 cluster (src/sim/character_render2.cpp): the
// flag (Wimpel) attach/refresh/remove sub-system, the visibility-state apply path,
// scene-attach reconciliation, and the mesh/anim sub-mesh bridges. Cross-module
// renderer / scene-graph / object calls are captured through a recording
// CharRender2Hooks mock. Golden values for the tolerance gates and the texture-index
// bias were computed by hand / with python3 (see the implementer report).
#include "test.h"

#include "sim/character_render2.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::u8;
using guild::u16;

namespace {

// ---- recording mock for the render2 hooks ---------------------------------
struct Rec2 {
    // counters / last-args
    int   inflateCalls = 0;
    void* lastInflate = nullptr;
    std::vector<void*> inflated;

    int   setPosCalls = 0;
    void* lastPosObj = nullptr;
    float lastPos[3] = {0, 0, 0};

    int   setWorldCalls = 0;
    void* lastWorldObj = nullptr;
    float lastWorld[3] = {0, 0, 0};

    int   lightCalls = 0;
    std::vector<void*> lightTargets;

    int   pointCalls = 0;
    int   rotateCalls = 0;
    // rotateVectorByHierarchy result (drives the yaw measurement)
    float rotateResult[3] = {0, 0, 0};

    int   selectTexCalls = 0;
    void* lastTexNode = nullptr;
    int   lastTexIndex = -999;

    int   attachNodeCalls = 0;
    const char* lastAttachName = nullptr;
    void* attachReturn = nullptr;

    int   applyParentCalls = 0;
    int   loadAnimCalls = 0;
    const char* lastAnimName = nullptr;

    int   pruneCalls = 0;
    int   propagateCalls = 0;
    int   removeMeshCalls = 0;
    int   detachCalls = 0;
    std::vector<void*> detached;

    int   runMeshCalls = 0;
    int   lastRunLimit = 0;
    int   runMeshReturn = 0;

    // ResolveMeshSelf
    int   heightmapCalls = 0;
    void* heightmapReturn = nullptr;
    int   collisionCalls = 0;
    int   switchCalls = 0;
    int   initLogCalls = 0;
    int   displayLogCalls = 0;
    guild::u8 lastInitFlag = 0;

    // person table (cityId -> record)
    const PersonRecord2* (*personFn)(u16) = nullptr;

    // sub-mesh slot names for one mesh
    const char* slotNames[3] = {nullptr, nullptr, nullptr};

    // scene walk: list of (handle, name) the hook will feed the callback
    std::vector<std::pair<void*, const char*>> nodes;
    int lastWalkLimit = 0;
};

Rec2* g_rec = nullptr;

void H_inflate(void* o) { g_rec->inflateCalls++; g_rec->lastInflate = o; g_rec->inflated.push_back(o); }
void H_setpos(void* o, const float p[3]) {
    g_rec->setPosCalls++; g_rec->lastPosObj = o;
    g_rec->lastPos[0] = p[0]; g_rec->lastPos[1] = p[1]; g_rec->lastPos[2] = p[2];
}
void H_setworld(void* o, const float p[3]) {
    g_rec->setWorldCalls++; g_rec->lastWorldObj = o;
    g_rec->lastWorld[0] = p[0]; g_rec->lastWorld[1] = p[1]; g_rec->lastWorld[2] = p[2];
}
void H_light(void* o) { g_rec->lightCalls++; g_rec->lightTargets.push_back(o); }
void H_point(const void*, const float in[3], float out[3]) {
    g_rec->pointCalls++;
    // Stand-in transform: world = in + {100, 0, 0} so tests can see it ran.
    out[0] = in[0] + 100.0f; out[1] = in[1]; out[2] = in[2];
}
void H_rotate(const void*, const float[3], float out[3]) {
    g_rec->rotateCalls++;
    out[0] = g_rec->rotateResult[0]; out[1] = g_rec->rotateResult[1]; out[2] = g_rec->rotateResult[2];
}
void H_visible(void*, int) {}
int  H_seltex(void* n, int idx) { g_rec->selectTexCalls++; g_rec->lastTexNode = n; g_rec->lastTexIndex = idx; return 0; }
void* H_attach(void*, const char* name) { g_rec->attachNodeCalls++; g_rec->lastAttachName = name; return g_rec->attachReturn; }
void H_applyparent(void*) { g_rec->applyParentCalls++; }
void H_loadanim(void*, const char* name, int) { g_rec->loadAnimCalls++; g_rec->lastAnimName = name; }
void H_walk(void* /*root*/, bool (*cb)(void*, const char*, void*), int limit, void* ctx) {
    g_rec->lastWalkLimit = limit;
    for (auto& n : g_rec->nodes) {
        if (!cb(n.first, n.second, ctx))
            break;
    }
}
void H_removemesh(void*) { g_rec->removeMeshCalls++; }
void H_detach(void* n) { g_rec->detachCalls++; g_rec->detached.push_back(n); }
void H_prune(void*) { g_rec->pruneCalls++; }
void H_propagate(void*, int) { g_rec->propagateCalls++; }
const PersonRecord2* H_person(u16 id) { return g_rec->personFn ? g_rec->personFn(id) : nullptr; }
int  H_runmesh(void*, int limit) { g_rec->runMeshCalls++; g_rec->lastRunLimit = limit; return g_rec->runMeshReturn; }
const char* H_slotname(void*, int slot) { return (slot >= 0 && slot < 3) ? g_rec->slotNames[slot] : nullptr; }
void* H_heightmap(void*) { g_rec->heightmapCalls++; return g_rec->heightmapReturn; }
void H_collision(void*) { g_rec->collisionCalls++; }
void H_switch(int) { g_rec->switchCalls++; }
void H_initlog(u8 f) { g_rec->initLogCalls++; g_rec->lastInitFlag = f; }
void H_displaylog(u8) { g_rec->displayLogCalls++; }

CharRender2Hooks MakeHooks() {
    CharRender2Hooks h{};
    h.inflateGeometry = H_inflate;
    h.heightmapCreate = H_heightmap;
    h.buildCollisionGrid = H_collision;
    h.switchUniverse = H_switch;
    h.initLogAndInflate = H_initlog;
    h.displayLogAndCleanup = H_displaylog;
    h.setObjectPosition = H_setpos;
    h.setWorldTranslation = H_setworld;
    h.buildLightCache = H_light;
    h.pointThroughBoneChain = H_point;
    h.rotateVectorByHierarchy = H_rotate;
    h.setVisible = H_visible;
    h.selectTextureSet = H_seltex;
    h.attachToUniverseNode = H_attach;
    h.applyParentTransform = H_applyparent;
    h.loadObjectAnimation = H_loadanim;
    h.walkScene = H_walk;
    h.removeMeshFromTree = H_removemesh;
    h.detachAndRelease = H_detach;
    h.pruneExpiredAttachments = H_prune;
    h.propagateDirty = H_propagate;
    h.lookupPerson = H_person;
    h.runMeshWalk = H_runmesh;
    h.submeshName = H_slotname;
    return h;
}

struct HookScope {
    CharRender2Hooks h;
    Rec2 rec;
    HookScope() { h = MakeHooks(); g_rec = &rec; SetCharRender2Hooks(&h); }
    ~HookScope() { SetCharRender2Hooks(nullptr); g_rec = nullptr; }
};

// A fixed flag-kind person record (type 6, texture index 80 -> 80-62 = 18).
PersonRecord2 g_flagRec{ /*typeByte=*/6, /*texIndex=*/80 };
const PersonRecord2* FlagPerson(u16) { return &g_flagRec; }
PersonRecord2 g_nonFlagRec{ /*typeByte=*/2, /*texIndex=*/80 };
const PersonRecord2* NonFlagPerson(u16) { return &g_nonFlagRec; }

} // namespace

// ===========================================================================
// TouchMeshFrames — inflate only the present, not-yet-inflated handles.
// ===========================================================================
TEST(CharRender2_TouchMeshFrames, OnlyInflatesUninflatedPresent) {
    HookScope s;
    int body = 1, head = 2, attach = 3, extra = 4;
    MeshHandles m{};
    m.body = &body;   m.bodyInflated = false;             // present, needs inflate
    m.head = &head;   m.headInflated = true;  m.hasHead = true;   // present, skip
    m.attach = &attach; m.attachInflated = false; m.hasAttachPtr = true; m.hasAttach = true; // inflate
    m.extra = &extra; m.extraInflated = false; m.hasExtra = false;  // ptr absent -> skip
    TouchMeshFrames(m);
    CHECK_EQ(s.rec.inflateCalls, 2);
    CHECK_EQ(s.rec.inflated[0], (void*)&body);
    CHECK_EQ(s.rec.inflated[1], (void*)&attach);
}

TEST(CharRender2_TouchMeshFrames, AttachPtrPresentButInnerNull) {
    HookScope s;
    int body = 1;
    MeshHandles m{};
    m.body = &body; m.bodyInflated = true;        // present + inflated -> skip
    m.hasAttachPtr = true; m.hasAttach = false;   // *a1[73] == 0 -> skip
    TouchMeshFrames(m);
    CHECK_EQ(s.rec.inflateCalls, 0);
}

// ===========================================================================
// ApplyVisibilityState — position always, light cache only when refreshLight,
// transport light cache only when both refreshLight and hasTransport.
// ===========================================================================
TEST(CharRender2_ApplyVisibilityState, NoRefreshNoTransport) {
    HookScope s;
    int mesh = 1;
    VisibilityCtx v{}; v.bodyMesh = &mesh; v.hasTransport = false; v.hasLowPoly = false;
    float pos[3] = {1, 2, 3};
    ApplyVisibilityState(v, pos, /*refreshLight=*/false);
    CHECK_EQ(s.rec.setPosCalls, 1);
    CHECK_EQ(s.rec.lastPosObj, (void*)&mesh);
    CHECK(s.rec.lastPos[0] == 1.0f && s.rec.lastPos[1] == 2.0f && s.rec.lastPos[2] == 3.0f);
    CHECK_EQ(s.rec.lightCalls, 0);
}

TEST(CharRender2_ApplyVisibilityState, RefreshWithTransport) {
    HookScope s;
    int mesh = 1, tmesh = 2;
    VisibilityCtx v{}; v.bodyMesh = &mesh; v.hasTransport = true; v.transportMesh = &tmesh;
    float pos[3] = {0, 0, 0};
    ApplyVisibilityState(v, pos, /*refreshLight=*/true);
    CHECK_EQ(s.rec.setPosCalls, 1);
    CHECK_EQ(s.rec.lightCalls, 2);                 // body + transport
    CHECK_EQ(s.rec.lightTargets[0], (void*)&mesh);
    CHECK_EQ(s.rec.lightTargets[1], (void*)&tmesh);
}

// ===========================================================================
// ShowWithScale — null mesh is a no-op; present mesh runs the bone-chain place,
// the hierarchy rotation, the world translation (yaw only), and the vis apply.
// ===========================================================================
TEST(CharRender2_ShowWithScale, NullMeshNoOp) {
    HookScope s;
    ShowScaleCtx c{}; c.mesh = nullptr;
    float place[3] = {0, 0, 0};
    CHECK_EQ(ShowWithScale(c, place, place), false);
    CHECK_EQ(s.rec.pointCalls, 0);
    CHECK_EQ(s.rec.setWorldCalls, 0);
}

TEST(CharRender2_ShowWithScale, RunsFullPathWorldPosFromBoneChain) {
    HookScope s;
    int mesh = 1, body = 2;
    ShowScaleCtx c{};
    c.mesh = &mesh; c.bodyMesh = &body;
    c.vis.bodyMesh = &body; c.vis.hasTransport = false; c.vis.hasLowPoly = false;
    // The binary rotates the FIXED +Z axis flt_5CA2B0 == {0,0,1} and measures the yaw
    // against it. Make the mock return {0,0,1} so the rotated vector is coincident with
    // the reference => VectorAngleBetween == 0. placeRot is ignored by the binary.
    float place[3] = {10, 20, 30};
    float placeRot[3] = {1, 0, 0};   // unused by ShowWithScale (binary uses flt_5CA2B0)
    s.rec.rotateResult[0] = 0; s.rec.rotateResult[1] = 0; s.rec.rotateResult[2] = 1;
    CHECK_EQ(ShowWithScale(c, place, placeRot), true);
    CHECK_EQ(s.rec.pointCalls, 1);
    CHECK_EQ(s.rec.rotateCalls, 1);
    // world translation == {0, yaw, 0}; yaw == 0 for coincident axes.
    CHECK_EQ(s.rec.setWorldCalls, 1);
    CHECK(s.rec.lastWorld[0] == 0.0f && s.rec.lastWorld[2] == 0.0f);
    CHECK(s.rec.lastWorld[1] == 0.0f);
    // ApplyVisibilityState ran at the bone-chain world pos (place + {100,0,0}).
    CHECK_EQ(s.rec.setPosCalls, 1);
    CHECK(s.rec.lastPos[0] == 110.0f && s.rec.lastPos[1] == 20.0f && s.rec.lastPos[2] == 30.0f);
    CHECK_EQ(s.rec.lightCalls, 1);   // refreshLight == true
}

// ===========================================================================
// ResolveMeshSelf — cache hit short-circuits; cold path creates+caches the mesh, and
// the universe switch + collision-grid gates fire only on their conditions.
// ===========================================================================
TEST(CharRender2_ResolveMeshSelf, CacheHitReturnsImmediately) {
    HookScope s;
    int cached = 1;
    ResolveMeshCtx c{};
    c.cachedMesh = &cached;
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.mesh, (void*)&cached);
    CHECK_EQ(s.rec.heightmapCalls, 0);     // no work on a hit
    CHECK_EQ(s.rec.switchCalls, 0);
}

TEST(CharRender2_ResolveMeshSelf, ColdPathNoSwitchOrdinaryActor) {
    HookScope s;
    int newMesh = 7, arg = 2;
    s.rec.heightmapReturn = &newMesh;
    ResolveMeshCtx c{};
    c.cachedMesh = nullptr;
    c.isWildActor = false;       // ordinary actor: uses actorLoadFlag, no collision grid
    c.actorLoadFlag = 0;         // 0 -> no universe switch
    c.actorReady981 = 0;
    c.heightmapArg = &arg;
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.mesh, (void*)&newMesh);
    CHECK_EQ(s.rec.heightmapCalls, 1);
    CHECK_EQ(r.switchedUniverse, false);
    CHECK_EQ(s.rec.switchCalls, 0);
    CHECK_EQ(r.builtCollisionGrid, false);  // not the wild actor
}

TEST(CharRender2_ResolveMeshSelf, SwitchesUniverseWhenFlagSetAndNotReady) {
    HookScope s;
    int newMesh = 7;
    s.rec.heightmapReturn = &newMesh;
    ResolveMeshCtx c{};
    c.cachedMesh = nullptr;
    c.isWildActor = false;
    c.actorLoadFlag = 0x03;      // non-zero -> switch; saved = 0x03 & 0xFD == 0x01
    c.actorReady981 = 0;         // not ready -> switch taken
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.switchedUniverse, true);
    CHECK_EQ(s.rec.switchCalls, 2);          // enter + restore
    CHECK_EQ(s.rec.initLogCalls, 1);
    CHECK_EQ(s.rec.displayLogCalls, 1);
    CHECK_EQ((int)s.rec.lastInitFlag, 0x01); // 0x03 & 0xFD
}

TEST(CharRender2_ResolveMeshSelf, ReadyByte981SuppressesSwitch) {
    HookScope s;
    int newMesh = 7;
    s.rec.heightmapReturn = &newMesh;
    ResolveMeshCtx c{};
    c.cachedMesh = nullptr;
    c.actorLoadFlag = 0x03;
    c.actorReady981 = 1;         // ready -> no switch despite the flag
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.switchedUniverse, false);
    CHECK_EQ(s.rec.switchCalls, 0);
}

TEST(CharRender2_ResolveMeshSelf, WildActorBuildsCollisionGrid) {
    HookScope s;
    int newMesh = 7;
    s.rec.heightmapReturn = &newMesh;
    ResolveMeshCtx c{};
    c.cachedMesh = nullptr;
    c.isWildActor = true;        // wild actor: uses wildLoadFlag, builds collision grid
    c.isEmptyActor = false;
    c.wildLoadFlag = 0;          // 0 -> no switch
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.builtCollisionGrid, true);
    CHECK_EQ(s.rec.collisionCalls, 1);
}

TEST(CharRender2_ResolveMeshSelf, NullMeshSkipsCollisionGrid) {
    HookScope s;
    s.rec.heightmapReturn = nullptr;   // HeightmapCreate failed
    ResolveMeshCtx c{};
    c.cachedMesh = nullptr;
    c.isWildActor = true;
    ResolveMeshResult r = ResolveMeshSelf(c);
    CHECK_EQ(r.mesh, (void*)nullptr);
    CHECK_EQ(r.builtCollisionGrid, false);
    CHECK_EQ(s.rec.collisionCalls, 0);
}

// ===========================================================================
// Sub-mesh bridges.
// ===========================================================================
TEST(CharRender2_UpdateSubMeshes, MatchingSlotPrunesAndDirties) {
    HookScope s;
    int mesh = 1;
    s.rec.slotNames[0] = "left";
    s.rec.slotNames[1] = "RightHand";   // matches "righthand" case-insensitively
    s.rec.slotNames[2] = nullptr;
    CHECK_EQ(UpdateSubMeshes(&mesh, "righthand"), 1);
    CHECK_EQ(s.rec.pruneCalls, 1);
    CHECK_EQ(s.rec.propagateCalls, 1);
}

TEST(CharRender2_UpdateSubMeshes, NullMeshNoOp) {
    HookScope s;
    CHECK_EQ(UpdateSubMeshes(nullptr, "x"), 1);
    CHECK_EQ(s.rec.pruneCalls, 0);
}

TEST(CharRender2_DrawSubMeshes, PrunesEachPresentSlot) {
    HookScope s;
    int mesh = 1;
    s.rec.slotNames[0] = "a";
    s.rec.slotNames[1] = nullptr;
    s.rec.slotNames[2] = "c";
    CHECK_EQ(DrawSubMeshes(&mesh), 1);
    CHECK_EQ(s.rec.pruneCalls, 2);
}

// ===========================================================================
// RunMeshCallback — limit 480 when mode bit 1 set, else 96; returns accumulator.
// ===========================================================================
TEST(CharRender2_RunMeshCallback, LimitDependsOnMode) {
    HookScope s; int actor = 1;
    s.rec.runMeshReturn = 0xABCD;
    CHECK_EQ(RunMeshCallback(&actor, 0), 0xABCD);
    CHECK_EQ(s.rec.lastRunLimit, 96);
    RunMeshCallback(&actor, 2);
    CHECK_EQ(s.rec.lastRunLimit, 480);
    RunMeshCallback(&actor, 0xFF);   // bit1 set
    CHECK_EQ(s.rec.lastRunLimit, 480);
}

// ===========================================================================
// AttachFlag — non-flag node is a no-op; the flag node builds the rotation, attaches
// the wimpel, selects the biased texture set, and loads the anim.
// ===========================================================================
TEST(CharRender2_AttachFlag, NonFlagNodeNoOp) {
    HookScope s;
    CHECK_EQ(AttachFlag("something_else", 5), 1);
    CHECK_EQ(s.rec.attachNodeCalls, 0);
    CHECK_EQ(s.rec.selectTexCalls, 0);
    CHECK_EQ(s.rec.loadAnimCalls, 0);
}

TEST(CharRender2_AttachFlag, FlagNodeAttachesAndTextures) {
    HookScope s;
    int node = 7;
    s.rec.attachReturn = &node;
    s.rec.personFn = FlagPerson;     // type 6, texIndex 80
    CHECK_EQ(AttachFlag("dummy_FAHNE", 42), 1);
    CHECK_EQ(s.rec.attachNodeCalls, 1);
    CHECK(std::strcmp(s.rec.lastAttachName, "sp_WIMPEL") == 0);
    CHECK_EQ(s.rec.applyParentCalls, 1);
    CHECK_EQ(s.rec.selectTexCalls, 1);
    CHECK_EQ(s.rec.lastTexNode, (void*)&node);
    CHECK_EQ(s.rec.lastTexIndex, 80 - 62);   // texIndex - kFlagTexBiasA == 18
    CHECK_EQ(s.rec.loadAnimCalls, 1);
    CHECK(std::strcmp(s.rec.lastAnimName, "sonstiges\\sp_WIMPEL.baf") == 0);
}

TEST(CharRender2_AttachFlag, FlagNodeNoCitySkipsTexture) {
    HookScope s;
    int node = 7;
    s.rec.attachReturn = &node;
    s.rec.personFn = FlagPerson;
    CHECK_EQ(AttachFlag("dummy_FAHNE", kNoCity), 1);
    CHECK_EQ(s.rec.attachNodeCalls, 1);
    CHECK_EQ(s.rec.selectTexCalls, 0);   // no city id -> no texture set
    CHECK_EQ(s.rec.loadAnimCalls, 1);    // anim still loads
}

// ===========================================================================
// ShowFlag — only the sp_WIMPEL node with a valid city id applies a texture set.
// ===========================================================================
TEST(CharRender2_ShowFlag, WrongNameNoOp) {
    HookScope s; int node = 1; s.rec.personFn = FlagPerson;
    CHECK_EQ(ShowFlag(&node, "not_wimpel", 5), 1);
    CHECK_EQ(s.rec.selectTexCalls, 0);
}

TEST(CharRender2_ShowFlag, NoCityNoOp) {
    HookScope s; int node = 1; s.rec.personFn = FlagPerson;
    CHECK_EQ(ShowFlag(&node, "sp_WIMPEL", kNoCity), 1);
    CHECK_EQ(s.rec.selectTexCalls, 0);
}

TEST(CharRender2_ShowFlag, ValidAppliesBiasedTexture) {
    HookScope s; int node = 1; s.rec.personFn = FlagPerson;   // texIndex 80
    CHECK_EQ(ShowFlag(&node, "sp_WIMPEL", 3), 1);
    CHECK_EQ(s.rec.selectTexCalls, 1);
    CHECK_EQ(s.rec.lastTexIndex, 80 - 62);
}

// ===========================================================================
// CollectFlagNodes — appends sp_WIMPEL handles; capacity guard at 32.
// ===========================================================================
TEST(CharRender2_CollectFlagNodes, AppendsOnlyWimpel) {
    FlagNodeList list{}; list.count = 0;
    int a = 1, b = 2, c = 3;
    CHECK_EQ(CollectFlagNodes(&a, "sp_WIMPEL", &list), true);
    CHECK_EQ(CollectFlagNodes(&b, "other", &list), true);   // ignored
    CHECK_EQ(CollectFlagNodes(&c, "SP_WIMPEL", &list), true); // case-insensitive
    CHECK_EQ(list.count, 2);
    CHECK_EQ(list.nodes[0], (void*)&a);
    CHECK_EQ(list.nodes[1], (void*)&c);
}

TEST(CharRender2_CollectFlagNodes, CapacityGuard) {
    FlagNodeList list{}; list.count = 30;
    int x = 1, y = 2, z = 3;
    // 30 -> 31: still < 32 -> continue (true).
    CHECK_EQ(CollectFlagNodes(&x, "sp_WIMPEL", &list), true);
    CHECK_EQ(list.count, 31);
    // 31 -> 32: count < 32 is now false -> stop (false). The 32nd handle (index 31)
    // is the last that fits in nodes[32].
    CHECK_EQ(CollectFlagNodes(&y, "sp_WIMPEL", &list), false);
    CHECK_EQ(list.count, 32);
    CHECK_EQ(list.nodes[31], (void*)&y);
    // 32 -> 33: over capacity, the handle is NOT stored (guarded), count keeps rising.
    CHECK_EQ(CollectFlagNodes(&z, "sp_WIMPEL", &list), false);
    CHECK_EQ(list.count, 33);
}

// ===========================================================================
// RefreshFlagAnimation — gated on root + valid flag-kind city; drives AttachFlag.
// ===========================================================================
TEST(CharRender2_RefreshFlagAnimation, NoRootNoOp) {
    HookScope s;
    CharActor2 a{}; a.sceneRoot97 = nullptr; a.cityId39 = 5;
    s.rec.personFn = FlagPerson;
    RefreshFlagAnimation(&a);
    CHECK_EQ(s.rec.lastWalkLimit, 0);   // walk never invoked
}

TEST(CharRender2_RefreshFlagAnimation, NonFlagKindNoOp) {
    HookScope s;
    int root = 1;
    CharActor2 a{}; a.sceneRoot97 = &root; a.cityId39 = 5;
    s.rec.personFn = NonFlagPerson;   // type 2
    RefreshFlagAnimation(&a);
    CHECK_EQ(s.rec.lastWalkLimit, 0);
}

TEST(CharRender2_RefreshFlagAnimation, FlagKindWalksAndAttaches) {
    HookScope s;
    int root = 1, n1 = 10;
    CharActor2 a{}; a.sceneRoot97 = &root; a.cityId39 = 7;
    s.rec.personFn = FlagPerson;
    int node7 = 7; s.rec.attachReturn = &node7;
    // the tree has one "dummy_FAHNE" mount -> AttachFlag fires
    s.rec.nodes = { {&n1, "dummy_FAHNE"} };
    RefreshFlagAnimation(&a);
    CHECK_EQ(s.rec.lastWalkLimit, 256);
    CHECK_EQ(s.rec.attachNodeCalls, 1);    // AttachFlag ran for the mount
    CHECK_EQ(s.rec.selectTexCalls, 1);
}

// ===========================================================================
// RemoveFlagNodes — collects then detaches each sp_WIMPEL node.
// ===========================================================================
TEST(CharRender2_RemoveFlagNodes, NoRootNoOp) {
    HookScope s;
    CharActor2 a{}; a.sceneRoot97 = nullptr;
    RemoveFlagNodes(&a);
    CHECK_EQ(s.rec.detachCalls, 0);
}

TEST(CharRender2_RemoveFlagNodes, DetachesCollected) {
    HookScope s;
    int root = 1, w1 = 11, w2 = 12, other = 13;
    CharActor2 a{}; a.sceneRoot97 = &root;
    s.rec.nodes = { {&w1, "sp_WIMPEL"}, {&other, "tree"}, {&w2, "sp_WIMPEL"} };
    RemoveFlagNodes(&a);
    CHECK_EQ(s.rec.lastWalkLimit, 64);
    CHECK_EQ(s.rec.detachCalls, 2);
    CHECK_EQ(s.rec.detached[0], (void*)&w1);
    CHECK_EQ(s.rec.detached[1], (void*)&w2);
    CHECK_EQ(s.rec.removeMeshCalls, 2);
}

// ===========================================================================
// UpdateAllFlags — per-person gating (valid flag-kind city, +90 bit0 clear).
// ===========================================================================
TEST(CharRender2_UpdateAllFlags, GatesAndDispatches) {
    HookScope s;
    int rootA = 1, rootB = 2, rootC = 3;
    CharActor2 a{}; a.sceneRoot97 = &rootA; a.cityId39 = 7; a.flagByte90 = 0;     // eligible
    CharActor2 b{}; b.sceneRoot97 = &rootB; b.cityId39 = 7; b.flagByte90 = 1;     // bit0 set -> skip
    CharActor2 c{}; c.sceneRoot97 = &rootC; c.cityId39 = kNoCity;                  // no city -> skip
    s.rec.personFn = FlagPerson;
    CharActor2* persons[3] = {&a, &b, &c};
    // only `a` should walk: RemoveFlagNodes(64) then RefreshFlagAnimation(256). The
    // last walk limit observed is 256 (from RefreshFlagAnimation, after RemoveFlagNodes).
    UpdateAllFlags(persons, 3);
    CHECK_EQ(s.rec.lastWalkLimit, 256);
    // a: RemoveFlagNodes walked (limit 64) + RefreshFlagAnimation walked (limit 256).
    // No nodes in the tree, so no detach / attach side effects.
    CHECK_EQ(s.rec.detachCalls, 0);
}

TEST(CharRender2_UpdateAllFlags, NullPersonSkipped) {
    HookScope s;
    CharActor2* persons[1] = {nullptr};
    UpdateAllFlags(persons, 1);
    CHECK_EQ(s.rec.lastWalkLimit, 0);
}
