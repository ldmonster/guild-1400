// Unit tests for object_lifecycle7 (VIBE_Object_* remaining leaves).
// Golden vectors computed by hand / python from the original control flow.
#include "test.h"

#include "sim/object_lifecycle7.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// MarkDirtyFlag (0x5af298): pure bit ops on three flag bytes.
// ---------------------------------------------------------------------------
TEST(ObjectLifecycle7, MarkDirtyFlagClearHi) {
    SceneNode7 n;
    n.b(528) = 0x01;        // bit2 will be OR'd in -> 0x05
    n.b(530) = 0xFF;        // clearHi clears bit7 -> 0x7F
    n.b(531) = 0xFF;        // bit0 cleared -> 0xFE
    char r = ObjectMarkDirtyFlag(&n, /*clearHi*/ 1);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)n.b(528), 0x05);
    CHECK_EQ((int)n.b(530), 0x7F);
    CHECK_EQ((int)n.b(531), 0xFE);
}

TEST(ObjectLifecycle7, MarkDirtyFlagNoClearHi) {
    SceneNode7 n;
    n.b(528) = 0x00;
    n.b(530) = 0xFF;        // untouched when clearHi == 0
    n.b(531) = 0x03;        // bit0 cleared -> 0x02
    ObjectMarkDirtyFlag(&n, /*clearHi*/ 0);
    CHECK_EQ((int)n.b(528), 0x04);
    CHECK_EQ((int)n.b(530), 0xFF);
    CHECK_EQ((int)n.b(531), 0x02);
}

// ---------------------------------------------------------------------------
// Reinitialize (0x40e818): clip-clamp a rect into the dirty-rect queue.
// Golden: x=5 (odd) -> w+=2 then &0xFE, x-- ; bounds [0,0,640,480].
//   inputs x=5,y=10,w=100,h=50,obj=1
//   x odd -> w=100+2=102, w&=0xFE=102, x=4
//   h+y=60 <= 480 -> v6=50
//   w+x=106 <= 640 -> w=102
//   y(10) >= minY(0) -> v9=10 ; x(4) >= minX(0)
//   record: x=4,y=10,w=102,h=50,obj=1 at slot 0
// ---------------------------------------------------------------------------
TEST(ObjectLifecycle7, ReinitializeClampAndInsert) {
    DirtyRectQueue q;
    q.base = 0x1000;
    int result = ObjectReinitialize(&q, /*x*/ 5, /*y*/ 10, /*w(a4)*/ 100,
                                    /*h(a3)*/ 50, /*obj*/ 1,
                                    /*minX*/ 0, /*minY*/ 0, /*maxX*/ 640,
                                    /*maxY*/ 480);
    CHECK_EQ(q.slots[0].x, 4);
    CHECK_EQ(q.slots[0].y, 10);
    CHECK_EQ(q.slots[0].w, 102);
    CHECK_EQ(q.slots[0].h, 50);
    CHECK_EQ(q.slots[0].obj, 1);
    CHECK_EQ(q.count, 1);
    CHECK_EQ(result, 0x1000 + 0);   // base + 20*0
}

TEST(ObjectLifecycle7, ReinitializeClampsToBounds) {
    DirtyRectQueue q;
    q.base = 0;
    // x=700 > maxX(640) -> x=640; even so no odd-adjust; w+x=640+30 > 640 -> w=0
    // -> w<0? no (0) ; y=-5 < minY(0) -> v9=0 ; h+y = 20-5=15 <= 480 -> v6=20
    ObjectReinitialize(&q, /*x*/ 700, /*y*/ -5, /*w*/ 30, /*h*/ 20, /*obj*/ 7,
                       0, 0, 640, 480);
    CHECK_EQ(q.slots[0].x, 640);
    CHECK_EQ(q.slots[0].y, 0);
    CHECK_EQ(q.slots[0].w, 0);
    CHECK_EQ(q.slots[0].h, 20);
    CHECK_EQ(q.slots[0].obj, 7);
}

TEST(ObjectLifecycle7, ReinitializeNullObjRejected) {
    DirtyRectQueue q;
    q.base = 0x500;
    int result = ObjectReinitialize(&q, 1, 2, 3, 4, /*obj*/ 0, 0, 0, 100, 100);
    CHECK_EQ(result, 0);       // result == obj == 0
    CHECK_EQ(q.count, 0);      // nothing inserted
}

// ---------------------------------------------------------------------------
// ApplyAnimScale (0x41e388): scale > 1 multiplies, scale < -1 divides.
// ---------------------------------------------------------------------------
static int g_scale, g_baseW, g_baseH, g_animOk;
static int FakeAnimFlags(int /*h*/, int* scale, int* w, int* hh) {
    *scale = g_scale; *w = g_baseW; *hh = g_baseH;
    return g_animOk;
}

TEST(ObjectLifecycle7, ApplyAnimScalePositive) {
    ObjLife7Hooks hk{};
    hk.animationFlagsCompute = FakeAnimFlags;
    ObjLife7SetHooks(hk);
    g_animOk = 1; g_scale = 3; g_baseW = 4; g_baseH = 5;
    SceneNode7 n;
    int r = ObjectApplyAnimScale(&n, /*handle*/ 7);
    ObjLife7ResetHooks();
    CHECK_EQ(n.d(108), 3);
    CHECK_EQ((int)*reinterpret_cast<i16*>(n.raw + 20), 12);  // 3*4
    CHECK_EQ((int)*reinterpret_cast<i16*>(n.raw + 22), 15);  // 3*5
    CHECK_EQ(r, 1);
}

TEST(ObjectLifecycle7, ApplyAnimScaleNegative) {
    ObjLife7Hooks hk{};
    hk.animationFlagsCompute = FakeAnimFlags;
    ObjLife7SetHooks(hk);
    g_animOk = 1; g_scale = -4; g_baseW = 20; g_baseH = 12;
    SceneNode7 n;
    ObjectApplyAnimScale(&n, 7);
    ObjLife7ResetHooks();
    CHECK_EQ(n.d(108), -4);
    CHECK_EQ((int)*reinterpret_cast<i16*>(n.raw + 20), 5);   // 20/abs(-4)
    CHECK_EQ((int)*reinterpret_cast<i16*>(n.raw + 22), 3);   // 12/abs(-4)
}

TEST(ObjectLifecycle7, ApplyAnimScaleHandleMinusOne) {
    ObjLife7ResetHooks();
    int r = ObjectApplyAnimScale(nullptr, -1);
    CHECK_EQ(r, -1);   // returns handle untouched
}

// ---------------------------------------------------------------------------
// RequestChangeZustand (0x594a68): resolve, clamp delta, build packet, queue.
// ---------------------------------------------------------------------------
static int g_qNode, g_appendField, g_queueRet, g_beginCalls;
static void* FakeQuery(int, int, int, int, int) {
    return g_qNode ? reinterpret_cast<void*>(0xABC) : nullptr;
}
static void FakeBegin(void*, int) { g_beginCalls++; }
static void FakeAppend(unsigned, unsigned, const void* v, int) {
    g_appendField = *static_cast<const char*>(v);
}
static int FakeQueue() { return g_queueRet; }

TEST(ObjectLifecycle7, RequestChangeZustandClampsNegative) {
    ObjLife7Hooks hk{};
    hk.gameObjectQueryFind = FakeQuery;
    hk.commandBeginDeltaPacket = FakeBegin;
    hk.commandAppendRawField = FakeAppend;
    hk.commandQueueRequestState22 = FakeQueue;
    ObjLife7SetHooks(hk);
    g_qNode = 1; g_appendField = 99; g_queueRet = 42; g_beginCalls = 0;
    // curState=1, delta=-5 -> 1 + (-5) = -4 < 0 -> field clamped to 0
    int r = ObjectRequestChangeZustand(/*objId*/ 10, /*delta*/ (char)-5,
                                       /*ctx*/ 0, /*curState*/ 1);
    ObjLife7ResetHooks();
    CHECK_EQ(g_appendField, 0);
    CHECK_EQ(g_beginCalls, 1);
    CHECK_EQ(r, 42);
}

TEST(ObjectLifecycle7, RequestChangeZustandKeepsDelta) {
    ObjLife7Hooks hk{};
    hk.gameObjectQueryFind = FakeQuery;
    hk.commandBeginDeltaPacket = FakeBegin;
    hk.commandAppendRawField = FakeAppend;
    hk.commandQueueRequestState22 = FakeQueue;
    ObjLife7SetHooks(hk);
    g_qNode = 1; g_appendField = 99; g_queueRet = 7; g_beginCalls = 0;
    // curState=10, delta=3 -> 13 >= 0 -> field stays 3
    int r = ObjectRequestChangeZustand(10, 3, 0, 10);
    ObjLife7ResetHooks();
    CHECK_EQ(g_appendField, 3);
    CHECK_EQ(r, 7);
}

TEST(ObjectLifecycle7, RequestChangeZustandNotFound) {
    ObjLife7Hooks hk{};
    hk.gameObjectQueryFind = FakeQuery;
    ObjLife7SetHooks(hk);
    g_qNode = 0;  // not found
    int r = ObjectRequestChangeZustand(123, 1, 0, 0);
    ObjLife7ResetHooks();
    // snprintf returns the length of the formatted error string.
    char buf[128];
    int expect = std::snprintf(
        buf, sizeof(buf),
        "gm_ChangeObjektZustand():Could not find Object-ID: %i", 123);
    CHECK_EQ(r, expect);
}

// ---------------------------------------------------------------------------
// FormatNameWithCountRecursive (0x510000): recursive "<name> (<count>)".
// ---------------------------------------------------------------------------
TEST(ObjectLifecycle7, FormatNameWithCountSingle) {
    ObjLife7ResetHooks();
    const char* names[] = {"Crate", "Barrel", "Chest"};
    NameCountNode n; n.typeIndex = 2; n.count = 7;
    char buf[600]; std::memset(buf, 0, sizeof(buf));
    char* r = ObjectFormatNameWithCountRecursive(&n, /*depth*/ 0, buf, names);
    CHECK(r != nullptr);
    if (r) CHECK_EQ(std::strcmp(buf, "Chest (7)"), 0);
}

TEST(ObjectLifecycle7, FormatNameWithCountRecurses) {
    ObjLife7ResetHooks();
    const char* names[] = {"Root", "Child"};
    NameCountNode child; child.typeIndex = 1; child.count = 2;
    NameCountNode root; root.typeIndex = 0; root.count = 9;
    root.firstChild = &child;
    char buf[600]; std::memset(buf, 0, sizeof(buf));
    char* r = ObjectFormatNameWithCountRecursive(&root, 0, buf, names);
    // root wrote at buf+0, child at buf+2 -> result points at the child (deepest).
    CHECK(r != nullptr);
    if (r) CHECK_EQ(std::strcmp(r, "Child (2)"), 0);
}

// ---------------------------------------------------------------------------
// CloneOrFreeData (0x5f1d00): alloc / free / shrink / realloc paths.
// ---------------------------------------------------------------------------
static void* g_allocRet; static int g_headerSize, g_freeCalls, g_allocCalls;
static void* g_shrinkRet;
static void* FakeAlloc(unsigned) { g_allocCalls++; return g_allocRet; }
static unsigned FakeHeader(const void*) { return (unsigned)g_headerSize; }
static void* FakeShrink(const void*) { return g_shrinkRet; }
static void FakeFree(const void*) { g_freeCalls++; }

TEST(ObjectLifecycle7, CloneOrFreeNullAllocates) {
    ObjLife7Hooks hk{}; hk.memAllocFromFreeList = FakeAlloc;
    ObjLife7SetHooks(hk);
    g_allocRet = reinterpret_cast<void*>(0x555); g_allocCalls = 0;
    void* r = ObjectCloneOrFreeData(nullptr, 64);
    ObjLife7ResetHooks();
    CHECK_EQ(r, reinterpret_cast<void*>(0x555));
    CHECK_EQ(g_allocCalls, 1);
}

TEST(ObjectLifecycle7, CloneOrFreeZeroSizeFrees) {
    ObjLife7Hooks hk{}; hk.memReturnToFreeList = FakeFree;
    ObjLife7SetHooks(hk);
    g_freeCalls = 0;
    int dummy = 0;
    void* r = ObjectCloneOrFreeData(&dummy, 0);
    ObjLife7ResetHooks();
    CHECK_EQ(r, nullptr);
    CHECK_EQ(g_freeCalls, 1);
}

TEST(ObjectLifecycle7, CloneOrFreeShrinkInPlace) {
    ObjLife7Hooks hk{};
    hk.memBlockHeaderClear = FakeHeader;
    hk.memShrinkBlock = FakeShrink;
    hk.memAllocFromFreeList = FakeAlloc;
    ObjLife7SetHooks(hk);
    g_headerSize = 16; g_shrinkRet = reinterpret_cast<void*>(0x999); g_allocCalls = 0;
    int dummy = 0;
    void* r = ObjectCloneOrFreeData(&dummy, 8);
    ObjLife7ResetHooks();
    CHECK_EQ(r, reinterpret_cast<void*>(0x999));     // shrink succeeded -> no alloc
    CHECK_EQ(g_allocCalls, 0);
}

// ---------------------------------------------------------------------------
// SetButtonCallback (0x41e49c): index = 87 * slot[+116]; table[index] = cb; ret idx*4.
// ---------------------------------------------------------------------------
TEST(ObjectLifecycle7, SetButtonCallbackWritesTable) {
    ObjLife7ResetHooks();
    // widget slot 2: its +116 field = 5 -> index = 87*5 = 435 -> ret = 1740.
    static u8 widgetMem[kWidgetSlotStride * 4];
    std::memset(widgetMem, 0, sizeof(widgetMem));
    *reinterpret_cast<i32*>(widgetMem + kWidgetSlotStride * 2 + kWidgetSlotIdxField) = 5;
    static i32 cbMem[1000];
    std::memset(cbMem, 0, sizeof(cbMem));
    WidgetTable wt; wt.base = widgetMem;
    CallbackTable ct; ct.base = cbMem;
    int r = ObjectSetButtonCallback(wt, ct, /*w*/ 2, /*cb*/ 0xCAFE);
    CHECK_EQ(r, 435 * 4);
    CHECK_EQ(cbMem[435], 0xCAFE);
}

// ---------------------------------------------------------------------------
// MoveNodeCallback (0x5b5184): free render nodes whose owner matches handle.
// ---------------------------------------------------------------------------
static int g_freedHandles[8], g_freedCount;
static void FakeRenderFree(void* /*n*/, int h) {
    if (g_freedCount < 8) g_freedHandles[g_freedCount] = h;
    g_freedCount++;
}

TEST(ObjectLifecycle7, MoveNodeCallbackFreesMatchingOwners) {
    ObjLife7Hooks hk{}; hk.renderFreeObjectNode = FakeRenderFree;
    ObjLife7SetHooks(hk);
    g_freedCount = 0;
    RenderNode sentinel;
    RenderNode c{}; c.owner = 9; c.next = &sentinel;       // matches handle 9
    RenderNode b{}; b.owner = 3; b.next = &c;               // no match
    RenderNode a{}; a.owner = 9; a.next = &b;               // matches handle 9
    char r = ObjectMoveNodeCallback(/*handle*/ 9, /*root*/ 0, /*ownerTag*/ 1,
                                    &a, &sentinel);
    ObjLife7ResetHooks();
    CHECK_EQ((int)r, 1);
    CHECK_EQ(g_freedCount, 2);
}

// ---------------------------------------------------------------------------
// CmdShowObject (0x43e624): gather pos, attach, build cache.
// ---------------------------------------------------------------------------
static float g_attachPos[3]; static int g_attachRet, g_cacheBuilt;
static int FakeAttach(int, const float* p, void*, int) {
    g_attachPos[0] = p[0]; g_attachPos[1] = p[1]; g_attachPos[2] = p[2];
    return g_attachRet;
}
static void FakeCache(int) { g_cacheBuilt++; }

TEST(ObjectLifecycle7, CmdShowObjectAttaches) {
    ObjLife7Hooks hk{};
    hk.attachToUniverseNode = FakeAttach;
    hk.lightBuildObjectCache = FakeCache;
    ObjLife7SetHooks(hk);
    g_attachRet = 0x77; g_cacheBuilt = 0;
    float a = 1.5f, b = 2.5f, c = 3.5f;
    int r = ObjectCmdShowObject(&a, &b, &c, nullptr);
    ObjLife7ResetHooks();
    CHECK_EQ(r, 0x77);
    CHECK_EQ(g_cacheBuilt, 1);
    CHECK(g_attachPos[0] == 1.5f && g_attachPos[1] == 2.5f && g_attachPos[2] == 3.5f);
}

TEST(ObjectLifecycle7, CmdShowObjectFailReturnsZero) {
    ObjLife7Hooks hk{};
    hk.attachToUniverseNode = FakeAttach;
    hk.lightBuildObjectCache = FakeCache;
    ObjLife7SetHooks(hk);
    g_attachRet = 0; g_cacheBuilt = 0;
    float a = 0, b = 0, c = 0;
    int r = ObjectCmdShowObject(&a, &b, &c, nullptr);
    ObjLife7ResetHooks();
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cacheBuilt, 0);   // cache NOT built on attach failure
}

// ---------------------------------------------------------------------------
// CmdShowObjectAtDummy (0x43e66c): null dummy -> 0; else transform + attach.
// ---------------------------------------------------------------------------
TEST(ObjectLifecycle7, CmdShowObjectAtDummyNullReturnsZero) {
    ObjLife7ResetHooks();
    int r = ObjectCmdShowObjectAtDummy(nullptr, nullptr, 0);
    CHECK_EQ(r, 0);
}

TEST(ObjectLifecycle7, CmdShowObjectAtDummyInertPassthrough) {
    ObjLife7Hooks hk{};
    hk.attachToUniverseNode = FakeAttach;   // no transform hook -> inert passthrough
    ObjLife7SetHooks(hk);
    g_attachRet = 0x55;
    SceneNode7 dummy;
    *reinterpret_cast<float*>(dummy.raw + 76) = 11.0f;   // local pos x
    *reinterpret_cast<float*>(dummy.raw + 80) = 22.0f;   // local pos y
    *reinterpret_cast<float*>(dummy.raw + 84) = 33.0f;   // local pos z
    int r = ObjectCmdShowObjectAtDummy(&dummy, nullptr, 0);
    ObjLife7ResetHooks();
    CHECK_EQ(r, 0x55);
    CHECK(g_attachPos[0] == 11.0f && g_attachPos[1] == 22.0f && g_attachPos[2] == 33.0f);
}

// ---------------------------------------------------------------------------
// CmdLoadScene (0x43ea80): always returns 1; reports error on failure.
// ---------------------------------------------------------------------------
static int g_loadRet, g_errCalls;
static int FakeLoad(const char*, int, i16, int) { return g_loadRet; }
static void FakeErr(const char*) { g_errCalls++; }

TEST(ObjectLifecycle7, CmdLoadSceneSuccessNoError) {
    ObjLife7Hooks hk{}; hk.sceneLoadFromStream = FakeLoad; hk.reportError = FakeErr;
    ObjLife7SetHooks(hk);
    g_loadRet = 1; g_errCalls = 0;
    int r = ObjectCmdLoadScene("x.scn", 0);
    ObjLife7ResetHooks();
    CHECK_EQ(r, 1);
    CHECK_EQ(g_errCalls, 0);
}

TEST(ObjectLifecycle7, CmdLoadSceneFailReportsError) {
    ObjLife7Hooks hk{}; hk.sceneLoadFromStream = FakeLoad; hk.reportError = FakeErr;
    ObjLife7SetHooks(hk);
    g_loadRet = 0; g_errCalls = 0;
    int r = ObjectCmdLoadScene("missing.scn", 0);
    ObjLife7ResetHooks();
    CHECK_EQ(r, 1);
    CHECK_EQ(g_errCalls, 1);
}

// ---------------------------------------------------------------------------
// BuildModelName (0x4ffe0c): mode 1 -> "ob_%s"+kind4; mode 2 -> "gb_%s"+kind3.
// ---------------------------------------------------------------------------
static int g_parseRet, g_restoreCalls;
static int FakeParse(SceneNode7*) { return g_parseRet; }
static void FakeRestore(SceneNode7*, unsigned) { g_restoreCalls++; }

TEST(ObjectLifecycle7, BuildModelNameModeOne) {
    ObjLife7Hooks hk{}; hk.parseNameAndBind = FakeParse;
    ObjLife7SetHooks(hk);
    g_parseRet = 123;
    SceneNode7 n;
    int r = ObjectBuildModelName(&n, /*typeIdx*/ 0, /*flag90*/ 0, /*attach*/ 0,
                                 /*firstByteIsTen*/ 0, /*mode*/ 1, "Tree");
    ObjLife7ResetHooks();
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(n.raw), "ob_Tree"), 0);
    CHECK_EQ((int)n.b(535), 4);
    CHECK_EQ(r, 123);
}

TEST(ObjectLifecycle7, BuildModelNameModeTwoRestores) {
    ObjLife7Hooks hk{};
    hk.parseNameAndBind = FakeParse;
    hk.universeRestoreObjectStates = FakeRestore;
    ObjLife7SetHooks(hk);
    g_parseRet = 1; g_restoreCalls = 0;
    SceneNode7 n;
    // attachKind == 1 and flag90 bit0 == 0 -> RestoreObjectStates fires; kind -> 3.
    int r = ObjectBuildModelName(&n, 0, /*flag90*/ 0, /*attachKind*/ 1,
                                 /*firstByteIsTen*/ 0, /*mode*/ 2, "Hall");
    ObjLife7ResetHooks();
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(n.raw), "gb_Hall"), 0);
    CHECK_EQ((int)n.b(535), 3);
    CHECK_EQ(g_restoreCalls, 1);
    CHECK_EQ(r, 1);
}

TEST(ObjectLifecycle7, BuildModelNameModeTwoFirstByteTen) {
    ObjLife7Hooks hk{}; hk.parseNameAndBind = FakeParse;
    ObjLife7SetHooks(hk);
    g_parseRet = 0;
    SceneNode7 n;
    n.b(535) = 0xEE; n.d(536) = 0x1234;
    ObjectBuildModelName(&n, 0, 0, 0, /*firstByteIsTen*/ 1, /*mode*/ 2, "X");
    ObjLife7ResetHooks();
    CHECK_EQ((int)n.b(535), 0);   // firstByteIsTen path zeroes +535 and +536
    CHECK_EQ(n.d(536), 0);
}
