// ===========================================================================
// object_lifecycle8_e2e_test.cpp — end-to-end flow across the batch-8 leaves:
// build a model node, attach a looping animation through the command pump, swap
// its texture set, then dispose the whole node tree (parent + child + alt child),
// verifying the teardown frees every tracked block and switches universe slots.
// ===========================================================================
#include <cstring>
#include <vector>

#include "test.h"
#include "sim/object_lifecycle8.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A tiny "world" the hooks drive so the e2e flow is observable.
struct World {
    int   attachCount = 0;
    u32   lastFlag = 0;
    int   loadCount = 0;
    int   textureSwaps = 0;
    std::vector<void*> freed;
    int   childDisposes = 0;
    int   slotSwitches = 0;
    int   freeDrawData = 0;
    int   soundDetaches = 0;
} g_w;

void* eAttach(void* m, u32 word) { g_w.attachCount++; g_w.lastFlag = word; (void)m; return reinterpret_cast<void*>(0x9); }
void  eLoad(const char* ) { g_w.loadCount++; }
int   eFindFree() { return 0; }
int   eSwap(void*, int, u8) { g_w.textureSwaps++; return 1; }
void  eFree(void* p) { g_w.freed.push_back(p); }
char  eSlot(int) { g_w.slotSwitches++; return 1; }
void  eFreeDraw(SceneNode8*) { g_w.freeDrawData++; }
void  eSound(i32) { g_w.soundDetaches++; }

}  // namespace

TEST(ObjLife8_E2E, AttachSwapDisposeFlow) {
    g_w = World();

    ObjLife8Hooks h;
    h.animAttachToBone = eAttach;
    h.animLoadStreamToStock = eLoad;
    h.animFindFreeMeshSlot = eFindFree;
    h.applyTextureSwap = eSwap;
    h.memFreeDebug = eFree;
    h.universeSwitchActiveSlot = eSlot;
    h.objectFreeDrawData = eFreeDraw;
    h.sound3dDetachIfValid = eSound;
    ObjLife8SetHooks(h);

    // --- build a parent node with a model, a child and an alt child ----------
    SceneNode8 parent;
    parent.d(460) = 0x5000;        // model
    parent.d(468) = 0x6000;        // draw block (freed on dispose)
    parent.d(488) = 0x7000;        // emitter block (freed on dispose)
    parent.d(492) = 0x8000;        // mesh block (texture set)
    parent.d(524) = 0x42;          // sound handle

    SceneNode8 child;
    child.d(460) = 0x5100;
    SceneNode8 alt;
    alt.b(528) = 0;                // bit0 clear -> alt child IS disposed

    *reinterpret_cast<SceneNode8**>(parent.raw + 508) = &child;  // firstChild
    *reinterpret_cast<SceneNode8**>(parent.raw + 496) = &alt;    // altChild

    // --- 1. attach a looping animation via the command pump (apply pass) -----
    CmdPumpState pump;
    int ar = ObjectCmdAttachAnimationLooped(&parent, "idle", /*loop on*/ 1, &pump, nullptr);
    CHECK_EQ(ar, 0);
    CHECK_EQ(g_w.attachCount, 1);
    CHECK_EQ(g_w.lastFlag, 0x025000u);   // looped flag word
    CHECK_EQ(g_w.loadCount, 1);          // findFree==0 forced a stream load

    // --- 2. swap the parent's texture set (3 poly groups) --------------------
    char ts = ObjectSelectTextureSet(&parent, /*set*/ 2, /*groups*/ 3, /*sets*/ 5, /*cur*/ 0);
    CHECK_EQ((int)ts, 1);
    CHECK_EQ(g_w.textureSwaps, 3);
    CHECK((parent.b(528) & 4) != 0);     // dirty bit set by the swap

    // --- 3. dispose the whole tree -------------------------------------------
    // node universe != g_activeUniverse -> a slot switch (to + restore == 2 calls).
    void* fakeUniverse = reinterpret_cast<void*>(0xABCD);
    char dr = ObjectDispose(&parent, fakeUniverse, nullptr, nullptr, /*slot*/ 3);
    CHECK_EQ((int)dr, 1);
    // freed: parent draw block, emitter block, parent node, child node (recursive),
    // alt node (recursive). Children recurse with the same universe (== fake !=
    // active) so each also tries a switch; we only assert the parent-level frees.
    bool freedDraw = false, freedEmitter = false, freedParent = false;
    for (void* p : g_w.freed) {
        if (p == reinterpret_cast<void*>(0x6000)) freedDraw = true;
        if (p == reinterpret_cast<void*>(0x7000)) freedEmitter = true;
        if (p == &parent) freedParent = true;
    }
    CHECK(freedDraw);
    CHECK(freedEmitter);
    CHECK(freedParent);
    CHECK(g_w.slotSwitches >= 2);        // switch-to + restore at least once
    CHECK(g_w.freeDrawData >= 1);
    CHECK(g_w.soundDetaches >= 1);

    ObjLife8ResetHooks();
}

// A second flow: the verified-attach command on a node with NO model bails before
// loading/attaching, and the once-attach on a real model both load and prune.
TEST(ObjLife8_E2E, VerifiedBailAndOncePrune) {
    g_w = World();
    static int prunes = 0; prunes = 0;
    ObjLife8Hooks h;
    h.animAttachToBone = eAttach;
    h.animLoadStreamToStock = eLoad;
    h.animFindFreeMeshSlot = eFindFree;
    h.animPruneExpiredAttachments = [](void*) { prunes++; };
    ObjLife8SetHooks(h);

    SceneNode8 noModel;                  // model == 0
    CmdPumpState pump;
    ObjectCmdAttachAnimLoopedVerified(&noModel, "x", 1, &pump, nullptr);
    CHECK_EQ(g_w.attachCount, 0);        // bailed: no model

    SceneNode8 withModel; withModel.d(460) = 0x1;
    ObjectCmdAttachAnimationOnce(&withModel, "open", nullptr);
    CHECK_EQ(g_w.attachCount, 1);
    CHECK_EQ(prunes, 1);

    ObjLife8ResetHooks();
}
