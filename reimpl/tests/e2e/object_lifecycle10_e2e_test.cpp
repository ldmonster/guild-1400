#include "test.h"

#include "sim/object_lifecycle10.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// E2E: a full spawn -> init -> alloc-draw -> attach -> find -> teardown flow across
// object_lifecycle10's functions, wired so the spawned node really flows through
// InitStruct, AllocDrawData, AttachToUniverseNode, FindByName, and finally
// DestroySpawnedEntities. All cross-module leaves are captor hooks.
// ===========================================================================
namespace {

std::vector<unsigned char>* g_drawArena = nullptr;
void* DrawArenaAlloc(unsigned size, const char*) {
    if (!g_drawArena) return nullptr;
    g_drawArena->assign(size, 0);
    return g_drawArena->data();
}

SceneNode10* g_node = nullptr;
void* SpawnReturnsNode(int, const char*) { return g_node; }

void SetPosition(SceneNode10* n, const float* p) {
    if (n && p) std::memcpy(n->raw + 76, p, 12);
}
void SetWorldXlate(SceneNode10* n, const float* p) {
    if (n && p) std::memcpy(n->raw + 132, p, 12);
}
int g_linkCalls = 0;
void LinkScene(SceneNode10*) { ++g_linkCalls; }

int g_switchCalls = 0, g_resetCalls = 0;
void SwitchSlot(int, int, int, void*) { ++g_switchCalls; }
int ResetSlot(void*) { return ++g_resetCalls; }
void FreeNode(void*) {}

// Walk hook that invokes the REAL Match predicate on our single spawned node
// (modeled as a SceneNode3 — the reused find type).
SceneNode3* g_walkNode = nullptr;
void WalkOnce(void*, void*, void* cb, u16, void* resultSlot) {
    auto fn = reinterpret_cast<bool (*)(SceneNode3*, FindCtx*)>(cb);
    fn(g_walkNode, static_cast<FindCtx*>(resultSlot));
}

}  // namespace

TEST(ObjLifecycle10E2E, SpawnInitAttachFindTeardownFlow) {
    SceneNode10 node;
    g_node = &node;

    std::vector<unsigned char> light(512, 0);
    std::vector<unsigned char> drawArena;
    g_drawArena = &drawArena;

    ObjLife10Hooks h{};
    h.memAllocDebug = &DrawArenaAlloc;
    h.objSpawn = &SpawnReturnsNode;
    h.objSetPosition = &SetPosition;
    h.objSetWorldTranslation = &SetWorldXlate;
    h.objLinkIntoScene = &LinkScene;
    h.sceneWalkAndInvoke = &WalkOnce;
    h.universeSwitchActiveSlot = &SwitchSlot;
    h.universeResetCurrentSlot = &ResetSlot;
    h.sceneGraphFreeNodeRecursive = &FreeNode;
    ObjLife10SetHooks(h);
    g_linkCalls = 0; g_switchCalls = 0; g_resetCalls = 0;

    // 1) Init the node.
    void* irc = ObjectInitStruct(&node, light.data());
    CHECK(irc == &node);
    CHECK_EQ((int)node.b(533), 1);

    // 2) Allocate the draw block; mark the first submesh resident.
    void* draw = ObjectAllocDrawData(&node);
    CHECK(draw != nullptr);
    if (draw) {
        SetBlockPtr(draw, 260, reinterpret_cast<void*>(static_cast<std::intptr_t>(1)));
        static_cast<unsigned char*>(draw)[2316] = 0;   // 0 submeshes => no tex walk
    }

    // 3) Attach the spawned node (named "well") into the universe.
    std::strcpy(reinterpret_cast<char*>(node.raw), "well");
    float pos[3] = {500.0f, 0.0f, 0.0f};
    float xlate[3] = {0.0f, 0.0f, 0.0f};
    void* out = ObjectAttachToUniverseNode(nullptr, pos, "well", xlate, nullptr);
    CHECK(out == &node);
    CHECK_EQ(g_linkCalls, 1);
    float seated[3];
    std::memcpy(seated, node.raw + 76, 12);
    CHECK_EQ(seated[0], 500.0f);

    // 4) Find a node back by name (the walk runs the REAL Match predicate on it).
    //    Find* uses the reused SceneNode3 find type; name the SceneNode3 "well".
    SceneNode3 searchRoot, findTarget;
    std::strcpy(reinterpret_cast<char*>(findTarget.raw), "well");
    g_walkNode = &findTarget;
    void* found = ObjectFindByName(&searchRoot, 0, "well", nullptr, nullptr);
    CHECK(found == &findTarget);

    // 5) Teardown across the slot tables (one active A slot).
    std::vector<unsigned char> slotsA(4 + 731 + 4, 0);
    slotsA[4 + 0] = 1;        // slot 0 active
    void* root = reinterpret_cast<void*>(static_cast<std::intptr_t>(0xABC));
    void* rootSlot = root;
    DestroyTables10 t;
    t.slotsA = slotsA.data();
    t.slotsB = nullptr; t.slotsBCount = 0;
    t.heightmaps = nullptr;
    int rc = ObjectDestroySpawnedEntities(0, root, &rootSlot, t, nullptr);
    CHECK(rootSlot == nullptr);
    CHECK_EQ(g_resetCalls, 2);     // initial + slot 0
    CHECK_EQ(rc, 2);
    CHECK_EQ((int)slotsA[4 + 0], 0xFF);

    ObjLife10ResetHooks();
    g_drawArena = nullptr;
}
