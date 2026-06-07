// End-to-end flow for src/sim/object_lifecycle4.{h,cpp} (VIBE_Object_* batch 4):
// spawn a node -> link it into the scene list -> fan transparency over its
// sub-meshes -> unlink -> dispose (frees draw-data). Exercises the node-type
// classification, the intrusive-list pointer surgery, the draw-data sub-mesh
// free path and the hook routing across the family in one sequence.
#include "sim/object_lifecycle4.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {

struct E2EState {
    std::vector<std::vector<uint8_t>> heap;
    int freeCount = 0;
    int changeTransparency = 0;
    int walkCount = 0;
    int lightRemove = 0;

    void* alloc(int size) {
        heap.emplace_back(static_cast<size_t>(size > 0 ? size : 1), 0);
        return heap.back().data();
    }
};
E2EState g_s;

void* eAlloc(int size, const char*) { return g_s.alloc(size); }
void eFree(void*) { ++g_s.freeCount; }
// InitStruct preserves the lightInfo ptr (+488); the arena is already zeroed.
void eInit(ObjNode4*) {}
void eStr(char* dst, const char* src, int n) {
    std::memset(dst, 0, n);
    if (src) std::strncpy(dst, src, n - 1);
}
void eChange(ObjNode4*, SubMeshEntry*, int, int) { ++g_s.changeTransparency; }
int  eWalk(void* node, int mask, int arg) {
    // Model the subtree transparency walk as a single self-invocation so the
    // e2e exercises ChangeTransparencySubMeshes through the tree wrapper.
    ++g_s.walkCount;
    int alpha = arg;
    ObjectChangeTransparencySubMeshes(static_cast<ObjNode4*>(node), &alpha);
    (void)mask;
    return 1;
}
void eLightRemove(ObjNode4*) { ++g_s.lightRemove; }

ObjLife4Hooks hooks() {
    ObjLife4Hooks h{};
    h.allocDebug = eAlloc;
    h.freeDebug = eFree;
    h.initStruct = eInit;
    h.strNCopyPad = eStr;
    h.changeTransparency = eChange;
    h.walkAndInvoke = eWalk;
    h.lightRemoveCache = eLightRemove;
    return h;
}

}  // namespace

TEST(ObjLife4E2E, SpawnLinkTransparencyDispose) {
    g_s = E2EState{};
    ObjLife4ResetHooks();
    g_objCurrent4 = nullptr;
    g_sceneListHead = nullptr;
    g_sceneListSentinel = nullptr;
    g_activeUniverse = nullptr;
    g_lightInfoForce5 = false;
    ObjLife4SetHooks(hooks());

    // Sentinel + an existing head already in the scene list.
    ObjNode4 sentinel, existingHead;
    existingHead.nodeType = 2;
    g_sceneListSentinel = &sentinel;
    g_sceneListHead = &existingHead;
    int universeTag = 0;
    g_activeUniverse = &universeTag;

    // 1) Spawn a "room" node (kind>=5, first char 'r' -> nodeType 6).
    ObjNode4* room = ObjectSpawn(5, "room_kitchen");
    CHECK(room != nullptr);
    CHECK_EQ((int)room->nodeType, 6);
    CHECK_EQ((int)room->nodeTypeShadow, 6);
    CHECK(room->lightInfo != nullptr);
    CHECK(room->universe == &universeTag);
    CHECK(std::strcmp(room->name, "room_kitchen") == 0);

    // 2) Link into scene: head-insert before existing head.
    ObjectLinkIntoScene(room);
    CHECK(g_sceneListHead == room);
    CHECK(room->nextList == &existingHead);
    CHECK(room->prevList == &sentinel);
    CHECK(existingHead.prevList == room);
    CHECK((room->flags528 & 3) == 3);

    // 3) Attach a draw-data block with 4 sub-meshes and fan transparency over
    //    the subtree (the walk wrapper invokes the per-sub-mesh applicator).
    DrawData dd;
    SubMeshEntry subs[4];
    dd.subMeshCount = 4;
    dd.subMeshes = subs;
    room->drawData = &dd;
    char tr = ObjectApplyTransparencyTree(room, 64);
    CHECK_EQ((int)tr, 1);
    CHECK_EQ(g_s.walkCount, 1);
    CHECK_EQ(g_s.changeTransparency, 4);   // one per sub-mesh

    // 4) Unlink from the scene list — splice out, head reverts to existingHead
    //    via the sentinel/next links.
    ObjectUnlinkFromScene(room);
    CHECK((room->flags528 & 2) == 0);
    CHECK(sentinel.nextList == &existingHead);   // prev.next = next
    CHECK(existingHead.prevList == &sentinel);   // next.prev = prev

    // 5) Dispose: type 6 path triggers the light-cache removal, then frees the
    //    draw-data block. Use a heap-allocated DrawData so FreeDrawData can free
    //    it. (The stack `dd` above was only for the transparency walk.)
    room->drawData = nullptr;
    auto* heapDD = static_cast<DrawData*>(g_s.alloc(sizeof(DrawData)));
    new (heapDD) DrawData();
    heapDD->subMeshCount = 0;
    room->drawData = heapDD;
    int rc = ObjectDisposeResources(room);
    CHECK_EQ(rc, 1);
    CHECK_EQ(g_s.lightRemove, 1);          // type==6 -> LightRemoveCacheEntry
    CHECK(room->drawData == nullptr);
    CHECK(room->meshFrame == nullptr);
    CHECK(g_s.freeCount >= 1);             // draw-data block freed
}
