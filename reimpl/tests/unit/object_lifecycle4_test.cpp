// Unit tests for src/sim/object_lifecycle4.{h,cpp} (VIBE_Object_* batch 4).
// Golden vectors:
//   * Spawn node-type classification table (kind<5 -> kind; kind>=5 first-char
//     'p'->7 'r'->6 's'->8 else 5; force5 collapses 6->5) — from the 0x5b054c
//     switch ladder.
//   * AllocPolysAndPoints sizes: polys = 40*pointCount, points = 80*(polyCount+8).
//   * SetLowNibbleFlag: result byte = (hi & 0xF0) | (new & 0x0F).
//   * Linked-list surgery (Link/Unlink) verified against the prev/next/sentinel
//     pointer writes.
#include "sim/object_lifecycle4.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {

// --- Hook captor / fake heap -------------------------------------------------
struct Captor {
    std::vector<std::pair<int, const char*>> allocs;
    std::vector<void*> frees;
    int   initStructCalls = 0;
    int   strncopyCalls = 0;
    char  copiedName[64] = {0};
    int   copiedN = 0;
    int   changeTransparencyCalls = 0;
    int   lastAlpha = -999;
    int   walkCalls = 0;
    int   lastWalkMask = -1;
    int   lastWalkArg = -1;
    int   walkRet = 1;
    int   lightRemoveCalls = 0;
    int   shadowClearCalls = 0;
    int   invalidateCalls = 0;
    int   setWorldCalls = 0;
    int   floorFreeCalls = 0;
    int   floorAllocCalls = 0;
    int   floorBuildCalls = 0;
    int   requestBuildOp = -1;
    int   lightGrayA = -1, lightGrayB = -1;
    int   textureReleaseCalls = 0;
    int   animReleaseCalls = 0;

    // bump arena so AllocDebug returns distinct, freeable, real pointers.
    std::vector<std::vector<uint8_t>> blocks;
    void* arenaAlloc(int size) {
        blocks.emplace_back(static_cast<size_t>(size > 0 ? size : 1), 0);
        return blocks.back().data();
    }
};
Captor g_cap;

void* hAlloc(int size, const char* tag) {
    g_cap.allocs.emplace_back(size, tag);
    return g_cap.arenaAlloc(size);
}
void hFree(void* p) { g_cap.frees.push_back(p); }
// InitStruct (0x5b0e88) initialises the node IN PLACE but preserves the
// lightInfo pointer at +488 (it dereferences it); our arena already zeroes the
// block, so the captor only counts the call.
void hInit(ObjNode4*) { ++g_cap.initStructCalls; }
void hStr(char* dst, const char* src, int n) {
    ++g_cap.strncopyCalls;
    g_cap.copiedN = n;
    std::memset(dst, 0, n);
    if (src) std::strncpy(dst, src, n - 1);
    std::memset(g_cap.copiedName, 0, sizeof(g_cap.copiedName));
    if (src) std::strncpy(g_cap.copiedName, src, 63);
}
void hChange(ObjNode4*, SubMeshEntry*, int alpha, int) {
    ++g_cap.changeTransparencyCalls;
    g_cap.lastAlpha = alpha;
}
int hWalk(void*, int mask, int arg) {
    ++g_cap.walkCalls;
    g_cap.lastWalkMask = mask;
    g_cap.lastWalkArg = arg;
    return g_cap.walkRet;
}
void hLightRemove(ObjNode4*) { ++g_cap.lightRemoveCalls; }
void hShadowClear(ObjNode4*) { ++g_cap.shadowClearCalls; }
void hInvalidate(u8) { ++g_cap.invalidateCalls; }
void hSetWorld(ObjNode4*) { ++g_cap.setWorldCalls; }
void hFloorFree(ObjNode4*) { ++g_cap.floorFreeCalls; }
void hFloorAlloc(ObjNode4*) { ++g_cap.floorAllocCalls; }
void hFloorBuild(ObjNode4*) { ++g_cap.floorBuildCalls; }
void hReqBuild(ObjNode4*, int op) { g_cap.requestBuildOp = op; }
void hLightGray(int a, int b) { g_cap.lightGrayA = a; g_cap.lightGrayB = b; }
void hTexRelease(void*) { ++g_cap.textureReleaseCalls; }
void hAnimRelease(void*) { ++g_cap.animReleaseCalls; }

ObjLife4Hooks makeHooks() {
    ObjLife4Hooks h{};
    h.allocDebug = hAlloc;
    h.freeDebug = hFree;
    h.initStruct = hInit;
    h.strNCopyPad = hStr;
    h.changeTransparency = hChange;
    h.walkAndInvoke = hWalk;
    h.textureRelease = hTexRelease;
    h.animReleaseMesh = hAnimRelease;
    h.lightRemoveCache = hLightRemove;
    h.shadowClearAll = hShadowClear;
    h.invalidateCurrent = hInvalidate;
    h.setWorldTranslation = hSetWorld;
    h.floorFreeTiles = hFloorFree;
    h.floorAllocInflate = hFloorAlloc;
    h.floorBuildPolys = hFloorBuild;
    h.requestBuildOp = hReqBuild;
    h.lightSetGray = hLightGray;
    return h;
}

void resetAll() {
    g_cap = Captor{};
    ObjLife4ResetHooks();
    g_objCurrent4 = nullptr;
    g_sceneListHead = nullptr;
    g_sceneListSentinel = nullptr;
    g_activeUniverse = nullptr;
    g_lightInfoForce5 = false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Spawn: node-type classification golden table.
// ---------------------------------------------------------------------------
TEST(ObjLife4Spawn, KindBelow5IsLiteral) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    for (u8 k = 0; k < 5; ++k) {
        ObjNode4* n = ObjectSpawn(k, "anything");
        CHECK(n != nullptr);
        CHECK_EQ((int)n->nodeType, (int)k);
        CHECK_EQ((int)n->nodeTypeShadow, (int)k);
        // kind<5 does not allocate a light-info block.
        CHECK(n->lightInfo == nullptr);
    }
    CHECK_EQ(g_cap.initStructCalls, 5);
    CHECK_EQ(g_cap.strncopyCalls, 5);
    CHECK_EQ(g_cap.copiedN, 64);
}

TEST(ObjLife4Spawn, FirstCharClassify) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    struct { const char* name; int type; } cases[] = {
        {"prop", 7},   // 'p'
        {"room", 6},   // 'r'
        {"sound", 8},  // 's'
        {"abc", 5},    // < 'p'
        {"zzz", 5},    // > 's'
        {"qux", 5},    // 'q'(113) < 'r' and != 'p' -> 5
    };
    for (auto& c : cases) {
        ObjNode4* n = ObjectSpawn(5, c.name);
        CHECK_EQ((int)n->nodeType, c.type);
        // kind>=5 allocates a light-info block.
        CHECK(n->lightInfo != nullptr);
    }
    // 2 allocs per spawn (node + lightinfo): 6 cases -> 12 allocs.
    CHECK_EQ((int)g_cap.allocs.size(), 12);
    CHECK_EQ(g_cap.allocs[0].first, kNodeAllocSize);
    CHECK_EQ(g_cap.allocs[1].first, kLightInfoSize);
}

TEST(ObjLife4Spawn, Force5CollapsesRoom) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    g_lightInfoForce5 = true;
    ObjNode4* room = ObjectSpawn(5, "room");
    CHECK_EQ((int)room->nodeType, 5);  // 6 collapsed to 5
    // sound is unaffected by force5.
    ObjNode4* snd = ObjectSpawn(5, "snd");
    CHECK_EQ((int)snd->nodeType, 8);
}

TEST(ObjLife4Spawn, Type8SetsFlag529Bit2) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4* snd = ObjectSpawn(5, "snd");
    CHECK_EQ((int)snd->nodeType, 8);
    CHECK((snd->flags529 & 4) != 0);
}

TEST(ObjLife4Spawn, UniverseAttached) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    int dummyUniverse = 0;
    g_activeUniverse = &dummyUniverse;
    ObjNode4* n = ObjectSpawn(2, "x");
    CHECK(n->universe == &dummyUniverse);
}

// ---------------------------------------------------------------------------
// InitSubMeshEntry: field zeroing + 0xFF tag + LOD slot clears.
// ---------------------------------------------------------------------------
TEST(ObjLife4SubMesh, InitClearsAndTags) {
    resetAll();
    SubMeshEntry e;
    // dirty everything first.
    int junk = 0;
    e.points = e.polys = e.mesh = e.texArray = &junk;
    e.polyCount = e.pointCount = 99;
    e.tag = 0; e.flagsByte = 0xFF;
    for (auto& s : e.lod) {
        s.meshData = s.extraPtr = &junk;
        s.field28 = s.field32 = s.field36 = 7;
        s.slotFlags = 0xFF;
    }
    void* parent = &junk;
    ObjectInitSubMeshEntry(&e, parent);
    CHECK(e.points == nullptr);
    CHECK(e.polys == nullptr);
    CHECK(e.mesh == nullptr);
    CHECK_EQ(e.polyCount, 0);
    CHECK_EQ(e.pointCount, 0);
    CHECK(e.parentDraw == parent);
    CHECK_EQ((int)e.tag, 0xFF);
    for (auto& s : e.lod) {
        CHECK(s.meshData == nullptr);
        CHECK(s.extraPtr == nullptr);
        CHECK_EQ(s.field28, 0);
        CHECK_EQ(s.field32, 0);
        CHECK_EQ(s.field36, 0);
        CHECK_EQ((int)(s.slotFlags & 2), 0);   // bit1 cleared
    }
}

// ---------------------------------------------------------------------------
// AllocPolysAndPoints: size arithmetic.
// ---------------------------------------------------------------------------
TEST(ObjLife4SubMesh, AllocSizes) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    SubMeshEntry e;
    e.polyCount = 10;
    e.pointCount = 7;
    void* res = ObjectAllocPolysAndPoints(&e);
    // polys = 40 * pointCount = 280 ; points = 80 * (polyCount+8) = 80*18 = 1440.
    CHECK_EQ((int)g_cap.allocs.size(), 2);
    CHECK_EQ(g_cap.allocs[0].first, 280);
    CHECK_EQ(g_cap.allocs[1].first, 1440);
    CHECK(res == e.points);            // returns points block
    CHECK(e.polys != nullptr);
}

TEST(ObjLife4SubMesh, AllocNoopWhenCountsZero) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    SubMeshEntry e;
    e.polyCount = 0;
    e.pointCount = 5;
    ObjectAllocPolysAndPoints(&e);
    CHECK_EQ((int)g_cap.allocs.size(), 0);
}

// ---------------------------------------------------------------------------
// SetLowNibbleFlag: nibble swap + side-effect ordering.
// ---------------------------------------------------------------------------
TEST(ObjLife4Floor, NibbleSwap) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    u8 tile = 0xA3;  // hi=0xA0, lo=0x3
    u8 r = ObjectSetLowNibbleFlag(&tile, 0x5, false, &node);
    CHECK_EQ((int)r, 0xA5);
    CHECK_EQ((int)tile, 0xA5);
    CHECK_EQ(g_cap.floorFreeCalls, 1);
    CHECK_EQ(g_cap.floorAllocCalls, 1);
    CHECK_EQ(g_cap.floorBuildCalls, 0);  // rebuild=false
}

TEST(ObjLife4Floor, NibbleUnchangedNoop) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    u8 tile = 0xA5;
    u8 r = ObjectSetLowNibbleFlag(&tile, 0x5, true, &node);
    CHECK_EQ((int)r, 0x5);          // returns newNibble, unchanged path
    CHECK_EQ((int)tile, 0xA5);
    CHECK_EQ(g_cap.floorFreeCalls, 0);
    CHECK_EQ(g_cap.floorBuildCalls, 0);
}

TEST(ObjLife4Floor, NibbleSwapWithRebuild) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    u8 tile = 0x10;
    ObjectSetLowNibbleFlag(&tile, 0xF, true, &node);
    CHECK_EQ((int)tile, 0x1F);
    CHECK_EQ(g_cap.floorBuildCalls, 1);
}

// ---------------------------------------------------------------------------
// Scene-list link / unlink pointer surgery.
// ---------------------------------------------------------------------------
TEST(ObjLife4Link, LinkIntoSceneHeadInsert) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 sentinel, oldHead, n;
    g_sceneListSentinel = &sentinel;
    g_sceneListHead = &oldHead;
    n.nodeType = 2;  // non-zero -> linked
    ObjNode4* r = ObjectLinkIntoScene(&n);
    CHECK(r == &n);
    CHECK(n.prevList == &sentinel);
    CHECK(n.nextList == &oldHead);
    CHECK(oldHead.prevList == &n);   // old head's prev now points at n
    CHECK(g_sceneListHead == &n);    // head updated
    CHECK((n.flags528 & 3) == 3);
}

TEST(ObjLife4Link, LinkType3BecomesCurrent) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 n;
    n.nodeType = 3;
    ObjectLinkIntoScene(&n);
    CHECK(g_objCurrent4 == &n);
    CHECK_EQ(g_cap.invalidateCalls, 1);
    CHECK_EQ(g_cap.setWorldCalls, 1);
}

TEST(ObjLife4Link, UnlinkFromSceneSplices) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 prev, n, next;
    n.nodeType = 2;
    n.flags528 = 2;  // linked bit set
    n.prevList = &prev;
    n.nextList = &next;
    ObjectUnlinkFromScene(&n);
    CHECK(prev.nextList == &next);   // prev.next = next
    CHECK(next.prevList == &prev);   // next.prev = prev
    CHECK((n.flags528 & 2) == 0);    // linked bit cleared
}

TEST(ObjLife4Link, UnlinkType3ClearsCurrent) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 n;
    n.nodeType = 3;
    g_objCurrent4 = &n;
    ObjectUnlinkFromScene(&n);
    CHECK(g_objCurrent4 == nullptr);
}

TEST(ObjLife4Link, UnlinkType0OnlyClearsBit) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 prev, n, next;
    n.nodeType = 0;
    n.flags528 = 0x7;
    n.prevList = &prev;
    n.nextList = &next;
    ObjectUnlinkFromScene(&n);
    CHECK((n.flags528 & 2) == 0);
    // prev/next NOT touched in the default path.
    CHECK(prev.nextList == nullptr);
}

// ---------------------------------------------------------------------------
// Sibling-chain unlink (UnlinkFromList).
// ---------------------------------------------------------------------------
TEST(ObjLife4Link, UnlinkFromListMiddle) {
    resetAll();
    ObjNode4 prev, n, next;
    n.prevList = &prev;
    n.nextList = &next;
    n.flags528 = 2;
    ObjectUnlinkFromList(&n);
    CHECK(prev.nextList == &next);   // prev.next = next
    CHECK(next.prevList == &prev);   // next.prev = prev
    CHECK(n.parent == nullptr);
    CHECK(n.prevList == nullptr);
    CHECK(n.nextList == nullptr);
    CHECK((n.flags528 & 2) == 0);
}

TEST(ObjLife4Link, UnlinkFromListTailFixesParentFirst) {
    resetAll();
    // node is the tail (no next); has prev; parent.firstChild tracked the tail.
    ObjNode4 parent, prev, n;
    n.prevList = &prev;
    n.nextList = nullptr;
    n.parent = &parent;
    parent.firstChild = &n;
    ObjectUnlinkFromList(&n);
    CHECK(prev.nextList == nullptr);     // prev.next = next(=0)
    CHECK(parent.firstChild == &prev);   // first updated to prev
}

TEST(ObjLife4Link, UnlinkFromListHeadFixesNextPrev) {
    resetAll();
    // node is the head (no prev); has next.
    ObjNode4 n, next;
    n.prevList = nullptr;
    n.nextList = &next;
    next.prevList = &n;
    ObjectUnlinkFromList(&n);
    CHECK(next.prevList == nullptr);   // next.prev = 0
}

TEST(ObjLife4Link, UnlinkFromListOnlyChildClearsParentFirst) {
    resetAll();
    ObjNode4 parent, n;
    n.prevList = nullptr;
    n.nextList = nullptr;
    n.parent = &parent;
    parent.firstChild = &n;
    ObjectUnlinkFromList(&n);
    CHECK(parent.firstChild == nullptr);
}

// ---------------------------------------------------------------------------
// Transparency fan-out + tree walk.
// ---------------------------------------------------------------------------
TEST(ObjLife4Transparency, SubMeshesFanOut) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    DrawData dd;
    SubMeshEntry subs[3];
    dd.subMeshCount = 3;
    dd.subMeshes = subs;
    ObjNode4 node;
    node.drawData = &dd;
    int alpha = 128;
    char r = ObjectChangeTransparencySubMeshes(&node, &alpha);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_cap.changeTransparencyCalls, 3);
    CHECK_EQ(g_cap.lastAlpha, 128);
}

TEST(ObjLife4Transparency, TreeWalkInvokesWalker) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    g_cap.walkRet = 1;
    char r = ObjectApplyTransparencyTree(&node, 200);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_cap.walkCalls, 1);
    CHECK_EQ(g_cap.lastWalkMask, 576);
    CHECK_EQ(g_cap.lastWalkArg, 200);
}

TEST(ObjLife4Transparency, TreeWalkNullNodeNoop) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    char r = ObjectApplyTransparencyTree(nullptr, 200);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(g_cap.walkCalls, 0);
}

// ---------------------------------------------------------------------------
// FreeDrawData / FreeSubMeshData.
// ---------------------------------------------------------------------------
TEST(ObjLife4Free, FreeDrawDataNullNoop) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    int junk = 0;
    node.meshFrame = &junk;
    node.drawData = nullptr;
    int r = ObjectFreeDrawData(&node);
    CHECK_EQ(r, 1);
    CHECK(node.meshFrame == nullptr);  // meshFrame cleared even on null path
    CHECK_EQ((int)g_cap.frees.size(), 0);
}

TEST(ObjLife4Free, FreeSubMeshReleasesBuffers) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    SubMeshEntry e;
    int polysBlock = 0, pointsBlock = 0;
    e.tag = 0x00;          // != 0xFF -> transparency reset fires
    e.points = &polysBlock;
    e.polyCount = 5;       // >0 -> points freed
    e.polys = &pointsBlock;
    e.pointCount = 3;      // >0 -> polys freed
    ObjectFreeSubMeshData(&e);
    CHECK_EQ(g_cap.changeTransparencyCalls, 1);
    CHECK_EQ(g_cap.lastAlpha, 255);
    CHECK_EQ((int)g_cap.frees.size(), 2);    // points + polys freed
    CHECK(e.points == nullptr);
    CHECK(e.polys == nullptr);
    CHECK_EQ(e.polyCount, 0);
    CHECK_EQ(e.pointCount, 0);
}

// ---------------------------------------------------------------------------
// Tiny command thunks.
// ---------------------------------------------------------------------------
TEST(ObjLife4Thunks, MarkStateAndReset) {
    resetAll();
    ObjLife4SetHooks(makeHooks());
    ObjNode4 node;
    CHECK_EQ(ObjectMarkState2(&node), 1);
    CHECK_EQ(g_cap.requestBuildOp, 2);
    CHECK_EQ(ObjectResetState(&node), 1);
    CHECK_EQ(g_cap.lightGrayA, 0);
    CHECK_EQ(g_cap.lightGrayB, 4608);
    CHECK_EQ(ObjectResetStateAlt(&node), 1);
}
