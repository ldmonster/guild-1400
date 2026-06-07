// End-to-end flow for the Character render2 cluster: a full flag (Wimpel) lifecycle
// on a city character — refresh (attach), then a global update pass that removes the
// old flag and re-attaches it — followed by a show-with-scale placement that drives
// the visibility-state apply. Everything is observed through one recording
// CharRender2Hooks mock so the cross-function wiring is verified, not just the leaves.
#include "test.h"

#include "sim/character_render2.h"

#include <cstring>
#include <utility>
#include <vector>

using namespace guild::sim;
using guild::u16;
using guild::u8;

namespace {

struct E2ERec {
    int attachCalls = 0;
    int detachCalls = 0;
    int selectTexCalls = 0;
    int loadAnimCalls = 0;
    int setPosCalls = 0;
    int setWorldCalls = 0;
    int lightCalls = 0;
    int removeMeshCalls = 0;
    int pruneCalls = 0;
    std::vector<int> texIndices;
    std::vector<void*> detached;
    void* attachReturn = nullptr;
    // walk feed (the scene tree contents the walk hook replays to the callback)
    std::vector<std::pair<void*, const char*>> nodes;
    int lastWalkLimit = 0;
};

E2ERec* g_e = nullptr;
PersonRecord2 g_cityRec{ /*typeByte=*/6, /*texIndex=*/100 };  // flag kind, tex 100

void E_inflate(void*) {}
void* E_heightmap(void*) { return nullptr; }
void E_collision(void*) {}
void E_switch(int) {}
void E_initlog(u8) {}
void E_displaylog(u8) {}
void E_setpos(void*, const float[3]) { g_e->setPosCalls++; }
void E_setworld(void*, const float[3]) { g_e->setWorldCalls++; }
void E_light(void*) { g_e->lightCalls++; }
void E_point(const void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void E_rotate(const void*, const float in[3], float out[3]) { out[0]=in[0]; out[1]=in[1]; out[2]=in[2]; }
void E_visible(void*, int) {}
int  E_seltex(void*, int idx) { g_e->selectTexCalls++; g_e->texIndices.push_back(idx); return 0; }
void* E_attach(void*, const char*) { g_e->attachCalls++; return g_e->attachReturn; }
void E_applyparent(void*) {}
void E_loadanim(void*, const char*, int) { g_e->loadAnimCalls++; }
void E_walk(void*, bool (*cb)(void*, const char*, void*), int limit, void* ctx) {
    g_e->lastWalkLimit = limit;
    for (auto& n : g_e->nodes)
        if (!cb(n.first, n.second, ctx)) break;
}
void E_removemesh(void*) { g_e->removeMeshCalls++; }
void E_detach(void* n) { g_e->detachCalls++; g_e->detached.push_back(n); }
void E_prune(void*) { g_e->pruneCalls++; }
void E_propagate(void*, int) {}
const PersonRecord2* E_person(u16) { return &g_cityRec; }
int  E_runmesh(void*, int) { return 0; }
const char* E_slotname(void*, int) { return nullptr; }

CharRender2Hooks MakeE2EHooks() {
    CharRender2Hooks h{};
    h.inflateGeometry = E_inflate;
    h.heightmapCreate = E_heightmap;
    h.buildCollisionGrid = E_collision;
    h.switchUniverse = E_switch;
    h.initLogAndInflate = E_initlog;
    h.displayLogAndCleanup = E_displaylog;
    h.setObjectPosition = E_setpos;
    h.setWorldTranslation = E_setworld;
    h.buildLightCache = E_light;
    h.pointThroughBoneChain = E_point;
    h.rotateVectorByHierarchy = E_rotate;
    h.setVisible = E_visible;
    h.selectTextureSet = E_seltex;
    h.attachToUniverseNode = E_attach;
    h.applyParentTransform = E_applyparent;
    h.loadObjectAnimation = E_loadanim;
    h.walkScene = E_walk;
    h.removeMeshFromTree = E_removemesh;
    h.detachAndRelease = E_detach;
    h.pruneExpiredAttachments = E_prune;
    h.propagateDirty = E_propagate;
    h.lookupPerson = E_person;
    h.runMeshWalk = E_runmesh;
    h.submeshName = E_slotname;
    return h;
}

} // namespace

// ===========================================================================
// Flag lifecycle: initial refresh attaches the wimpel; a global update pass removes
// the existing wimpel node and re-attaches a fresh one with the city's texture set.
// ===========================================================================
TEST(CharRender2_E2E, FlagLifecycleRefreshThenUpdate) {
    E2ERec rec;
    g_e = &rec;
    CharRender2Hooks h = MakeE2EHooks();
    SetCharRender2Hooks(&h);

    int root = 1, mount = 10, wimpelNode = 99;
    rec.attachReturn = &wimpelNode;

    CharActor2 actor{};
    actor.sceneRoot97 = &root;
    actor.cityId39 = 4;          // valid city id
    actor.flagByte90 = 0;        // not yet handled

    // --- 1. initial RefreshFlagAnimation: the tree has the dummy_FAHNE mount ------
    rec.nodes = { {&mount, "dummy_FAHNE"} };
    RefreshFlagAnimation(&actor);
    CHECK_EQ(rec.lastWalkLimit, 256);
    CHECK_EQ(rec.attachCalls, 1);                 // wimpel attached
    CHECK_EQ(rec.selectTexCalls, 1);
    CHECK_EQ(rec.texIndices[0], 100 - 62);        // tex 100 biased by -62 == 38
    CHECK_EQ(rec.loadAnimCalls, 1);

    // --- 2. global update pass over the city's persons ---------------------------
    // The tree now also contains the attached sp_WIMPEL node (it would be detached on
    // a re-layout). UpdateAllFlags should RemoveFlagNodes (detach the wimpel) then
    // RefreshFlagAnimation (re-attach off the dummy_FAHNE mount).
    rec.nodes = { {&wimpelNode, "sp_WIMPEL"}, {&mount, "dummy_FAHNE"} };
    int attachBefore = rec.attachCalls;
    int detachBefore = rec.detachCalls;

    CharActor2* persons[1] = {&actor};
    UpdateAllFlags(persons, 1);

    // RemoveFlagNodes detached the old wimpel...
    CHECK(rec.detachCalls > detachBefore);
    CHECK_EQ(rec.detached.back(), (void*)&wimpelNode);
    CHECK_EQ(rec.removeMeshCalls, 1);
    // ...and RefreshFlagAnimation re-attached a fresh one off the mount.
    CHECK(rec.attachCalls > attachBefore);
    CHECK_EQ(rec.lastWalkLimit, 256);             // last walk is the refresh pass

    SetCharRender2Hooks(nullptr);
    g_e = nullptr;
}

// ===========================================================================
// Placement: ShowWithScale drives the bone-chain world place + the full visibility
// apply (position + light cache). A prior ResolveMeshSelf lazily builds the terrain
// mesh that the placement then operates over.
// ===========================================================================
TEST(CharRender2_E2E, ResolveThenPlace) {
    E2ERec rec;
    g_e = &rec;
    CharRender2Hooks h = MakeE2EHooks();
    SetCharRender2Hooks(&h);

    int mesh = 1, body = 2;

    // --- ResolveMeshSelf: cold load (no cache, no flag) builds the terrain mesh ----
    static int s_terrain = 9;
    h.heightmapCreate = [](void*) -> void* { return &s_terrain; };
    SetCharRender2Hooks(&h);
    ResolveMeshCtx rc{};
    rc.cachedMesh = nullptr;
    rc.isWildActor = false;
    rc.actorLoadFlag = 0;          // no universe switch
    ResolveMeshResult rr = ResolveMeshSelf(rc);
    CHECK_EQ(rr.mesh, (void*)&s_terrain);
    CHECK_EQ(rr.switchedUniverse, false);

    // A second resolve with the now-cached mesh short-circuits.
    rc.cachedMesh = rr.mesh;
    ResolveMeshResult rr2 = ResolveMeshSelf(rc);
    CHECK_EQ(rr2.mesh, rr.mesh);

    // --- ShowWithScale: place the character ---------------------------------------
    ShowScaleCtx sc{};
    sc.mesh = &mesh; sc.bodyMesh = &body;
    sc.vis.bodyMesh = &body; sc.vis.hasTransport = false;
    float place[3]    = {0, 0, 0};
    float placeRot[3] = {1, 0, 0};
    CHECK_EQ(ShowWithScale(sc, place, placeRot), true);
    CHECK_EQ(rec.setWorldCalls, 1);   // world rotation written
    CHECK_EQ(rec.setPosCalls, 1);     // visibility apply position
    CHECK_EQ(rec.lightCalls, 1);      // refreshLight == true

    SetCharRender2Hooks(nullptr);
    g_e = nullptr;
}
