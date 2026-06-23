// Golden-vector unit tests for guild::render object-list / texture-budget leaves.
// Vectors derived directly from the gilde.exe Hex-Rays reference of record.
#include "tests/framework/test.h"
#include "render/render_recon_objlist.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// 0x5e0e00 InitObjectList — exact global state after reset.
// ---------------------------------------------------------------------------
TEST(RenderReconObjList, InitObjectListWritesExactBlock) {
    u32 ret = InitObjectList();
    CHECK_EQ(ret, 1065353216u); // returns 1.0f bit pattern

    ObjListState& g = ObjListGlobals();
    CHECK_EQ(g.word_748, 0u);
    CHECK_EQ(g.word_43C, 0u);
    CHECK_EQ(g.word_74C, 0x1408130u);
    CHECK_EQ(g.block[0], 0u);
    CHECK_EQ(g.block[1], 0u);
    CHECK_EQ(g.block[2], 1065353216u);
    CHECK_EQ(g.block[3], 1065353216u);
    CHECK_EQ(g.block[4], 0u);
    CHECK_EQ(g.block[5], 1065353216u);
    CHECK_EQ(g.block[6], 0u);
    CHECK_EQ(g.block[7], 0u);
    CHECK_EQ(g.block[8], 1065353216u);
    CHECK_EQ(g.block[9], 0u);
    CHECK_EQ(g.block[10], 1065353216u);
    CHECK_EQ(g.block[11], 1065353216u);
    CHECK_EQ(g.listHead, ObjListSentinel());
}

// ---------------------------------------------------------------------------
// 0x5dcd50 FormatCardInfo — bit-field decode golden vectors.
// ---------------------------------------------------------------------------
namespace {
struct CapVec { int translucent, fake, persp, linear, clip; };

// Reference decode straight from the original shifts.
CapVec RefDecode(u8 f) {
    CapVec v;
    v.translucent = f & 1;
    v.fake   = (u8)((u8)(f << 6) >> 7);
    v.persp  = (u8)((u8)(32 * f) >> 7);
    v.linear = (u8)((u8)(8 * f) >> 7);
    v.clip   = (u8)((u8)(16 * f) >> 7);
    return v;
}

// Capture hook so we can assert the decoded bits the function passes to sprintf.
CapVec g_captured;
int g_capMinW, g_capMaxH;
int CaptureSprintf(char* out, int t, int fa, int pe, int li, int cl,
                   int minW, int minH, int maxW, int maxH) {
    g_captured = {t, fa, pe, li, cl};
    g_capMinW = minW;
    g_capMaxH = maxH;
    return std::sprintf(out, "t=%i", t); // small deterministic return
}
} // namespace

TEST(RenderReconObjList, FormatCardInfoBitDecode) {
    auto& h = ObjListHooks();
    auto* saved = h.sprintfCardInfo;
    h.sprintfCardInfo = &CaptureSprintf;

    // Sweep all 256 flag bytes; decoded bits must match the reference shifts.
    for (int i = 0; i < 256; ++i) {
        CardCaps caps{};
        caps.flags = (u8)i;
        caps.minTexW = 7;
        caps.minTexH = 9;
        caps.maxTexW = 11;
        caps.maxTexH = 13;
        char buf[256];
        FormatCardInfo(&caps, buf);
        CapVec ref = RefDecode((u8)i);
        CHECK_EQ(g_captured.translucent, ref.translucent);
        CHECK_EQ(g_captured.fake, ref.fake);
        CHECK_EQ(g_captured.persp, ref.persp);
        CHECK_EQ(g_captured.linear, ref.linear);
        CHECK_EQ(g_captured.clip, ref.clip);
    }
    // Spot-check a known byte: 0b00011111 = 0x1F.
    //   translucent=1, fake=(0x1F>>1)&1=1, persp=(0x1F>>2)&1=1,
    //   linear=(0x1F>>4)&1=1, clip=(0x1F>>3)&1=1
    CardCaps caps{};
    caps.flags = 0x1F;
    char buf[256];
    FormatCardInfo(&caps, buf);
    CHECK_EQ(g_captured.translucent, 1);
    CHECK_EQ(g_captured.fake, 1);
    CHECK_EQ(g_captured.persp, 1);
    CHECK_EQ(g_captured.linear, 1);
    CHECK_EQ(g_captured.clip, 1);
    // And 0x04 (perspective_correction only): bit2 set.
    caps.flags = 0x04;
    FormatCardInfo(&caps, buf);
    CHECK_EQ(g_captured.translucent, 0);
    CHECK_EQ(g_captured.fake, 0);
    CHECK_EQ(g_captured.persp, 1);
    CHECK_EQ(g_captured.linear, 0);
    CHECK_EQ(g_captured.clip, 0);

    h.sprintfCardInfo = saved;
}

TEST(RenderReconObjList, FormatCardInfoDefaultStringExact) {
    // Default hook produces the verbatim gilde.exe format string.
    CardCaps caps{};
    caps.flags = 0x01;     // translucent only
    caps.minTexW = 1;
    caps.minTexH = 2;
    caps.maxTexW = 256;
    caps.maxTexH = 256;
    char buf[512];
    int n = FormatCardInfo(&caps, buf);
    const char* expect =
        "3D-Card Information:\n"
        " translucent: 1, fake_translucency: 0, perspective_correction: 0, "
        "linear_filter: 0, can_clip: 0\n"
        " min_texture_width: 1, min_texture_height: 2, max_texture_width: 256, "
        "max_texture_height: 256";
    CHECK_EQ(std::strcmp(buf, expect), 0);
    CHECK_EQ(n, (int)std::strlen(expect));
}

// ---------------------------------------------------------------------------
// 0x5e0f30 FreeObjectNode — doubly-linked splice + allocation release order.
// ---------------------------------------------------------------------------
namespace {
std::vector<u32> g_freedHandles;
std::vector<RenderNode*> g_freedNodes;
std::vector<u32> g_releasedTex;
std::vector<RenderNode*> g_unlinked;

void RecFreeDebug(u32 p) { g_freedHandles.push_back(p); }
void RecFreeNode(RenderNode* n) { g_freedNodes.push_back(n); }
void RecReleaseTex(u32 t) { g_releasedTex.push_back(t); }
void RecUnlink(RenderNode* n) { g_unlinked.push_back(n); }

void ResetRecorders() {
    g_freedHandles.clear();
    g_freedNodes.clear();
    g_releasedTex.clear();
    g_unlinked.clear();
}
} // namespace

TEST(RenderReconObjList, FreeObjectNodeOwnedSplice) {
    auto& h = ObjListHooks();
    auto sFD = h.memoryFreeDebug; auto sFN = h.memoryFreeNode;
    auto sRT = h.releaseTextureEntry; auto sUL = h.unlinkObjectNode;
    h.memoryFreeDebug = &RecFreeDebug;
    h.memoryFreeNode = &RecFreeNode;
    h.releaseTextureEntry = &RecReleaseTex;
    h.unlinkObjectNode = &RecUnlink;
    ResetRecorders();

    RenderNodeOwner owner{};
    // List: prevNode <-> mid <-> nextNode ; mid is head AND tail-adjacent test.
    RenderNode prevNode{}, mid{}, nextNode{};
    prevNode.next = &mid;   mid.prev = &prevNode;
    mid.next = &nextNode;   nextNode.prev = &mid;
    mid.owner = &owner;     // owned by the global list
    owner.head = &mid;      // mid is the head
    owner.tail = &nextNode; // tail is something else
    mid.texEntry = 0xABCD;
    mid.alloc28 = 0x111;
    mid.allocDC = 0x222;
    mid.allocE0 = 0x333;

    FreeObjectNode(&mid, &owner);

    // Splice: prevNode.next -> nextNode ; nextNode.prev -> prevNode.
    CHECK(prevNode.next == &nextNode);
    CHECK(nextNode.prev == &prevNode);
    // mid was head -> head becomes mid.prev (== prevNode), per original.
    CHECK(owner.head == &prevNode);
    CHECK(owner.tail == &nextNode); // unchanged (mid != tail)
    // Not the non-owned branch -> no unlink hook.
    CHECK_EQ((int)g_unlinked.size(), 0);
    // Texture released.
    CHECK_EQ((int)g_releasedTex.size(), 1);
    CHECK_EQ(g_releasedTex[0], 0xABCDu);
    // Three handle frees, in order 0x28, 0xDC, 0xE0.
    CHECK_EQ((int)g_freedHandles.size(), 3);
    CHECK_EQ(g_freedHandles[0], 0x111u);
    CHECK_EQ(g_freedHandles[1], 0x222u);
    CHECK_EQ(g_freedHandles[2], 0x333u);
    // Node itself freed last; handles zeroed.
    CHECK_EQ((int)g_freedNodes.size(), 1);
    CHECK(g_freedNodes[0] == &mid);
    CHECK_EQ(mid.alloc28, 0u);
    CHECK_EQ(mid.allocDC, 0u);
    CHECK_EQ(mid.allocE0, 0u);

    h.memoryFreeDebug = sFD; h.memoryFreeNode = sFN;
    h.releaseTextureEntry = sRT; h.unlinkObjectNode = sUL;
}

TEST(RenderReconObjList, FreeObjectNodeNonOwnedUsesUnlink) {
    auto& h = ObjListHooks();
    auto sFD = h.memoryFreeDebug; auto sFN = h.memoryFreeNode;
    auto sRT = h.releaseTextureEntry; auto sUL = h.unlinkObjectNode;
    h.memoryFreeDebug = &RecFreeDebug;
    h.memoryFreeNode = &RecFreeNode;
    h.releaseTextureEntry = &RecReleaseTex;
    h.unlinkObjectNode = &RecUnlink;
    ResetRecorders();

    RenderNodeOwner owner{};
    RenderNodeOwner other{};
    RenderNode n{};
    n.owner = &other;     // NOT the global owner -> unlink branch
    n.texEntry = 0;       // no texture -> no release
    n.alloc28 = 1; n.allocDC = 2; n.allocE0 = 3;

    FreeObjectNode(&n, &owner);

    CHECK_EQ((int)g_unlinked.size(), 1);
    CHECK(g_unlinked[0] == &n);
    CHECK_EQ((int)g_releasedTex.size(), 0); // texEntry==0
    CHECK_EQ((int)g_freedHandles.size(), 3);
    CHECK_EQ((int)g_freedNodes.size(), 1);

    h.memoryFreeDebug = sFD; h.memoryFreeNode = sFN;
    h.releaseTextureEntry = sRT; h.unlinkObjectNode = sUL;
}

// ---------------------------------------------------------------------------
// 0x5b379c AdjustTextureBudget — gating + pressure heuristic golden vectors.
// ---------------------------------------------------------------------------
namespace {
bool g_active = true;
bool RetActive() { return g_active; }
u32 g_total = 0, g_free = 0;
int g_queryCount = 0;
u32 RetVidMem(u32* freeOut) { ++g_queryCount; *freeOut = g_free; return g_total; }
int g_evictCount = 0;
u32 RetEvict() { ++g_evictCount; return 0; }
} // namespace

TEST(RenderReconObjList, AdjustBudgetGatedWhenInactive) {
    auto& h = ObjListHooks();
    auto sA = h.budgetActive; auto sQ = h.queryVidMem; auto sE = h.evictManagedTextures;
    h.budgetActive = &RetActive; h.queryVidMem = &RetVidMem; h.evictManagedTextures = &RetEvict;
    g_active = false; g_queryCount = 0; g_evictCount = 0;

    auto& g = TextureBudgetGlobals();
    g = TextureBudgetState{};
    u8 r = AdjustTextureBudget(0x1000, 0);
    CHECK_EQ((int)r, 0x00);          // low byte of tick (a1) unchanged
    CHECK_EQ(g_queryCount, 0);       // never queried vidmem

    h.budgetActive = sA; h.queryVidMem = sQ; h.evictManagedTextures = sE;
}

TEST(RenderReconObjList, AdjustBudgetGatedWhenTooSoon) {
    auto& h = ObjListHooks();
    auto sA = h.budgetActive; auto sQ = h.queryVidMem; auto sE = h.evictManagedTextures;
    h.budgetActive = &RetActive; h.queryVidMem = &RetVidMem; h.evictManagedTextures = &RetEvict;
    g_active = true; g_queryCount = 0; g_evictCount = 0;

    auto& g = TextureBudgetGlobals();
    g = TextureBudgetState{};
    g.lastTick = 0x1000;
    // tick - lastTick = 0x20 < 0x40 -> early return with low byte of delta.
    u8 r = AdjustTextureBudget(0x1020, 0);
    CHECK_EQ((int)r, 0x20);
    CHECK_EQ(g_queryCount, 0);

    h.budgetActive = sA; h.queryVidMem = sQ; h.evictManagedTextures = sE;
}

TEST(RenderReconObjList, AdjustBudgetLowTotalHalvesLimitA) {
    auto& h = ObjListHooks();
    auto sA = h.budgetActive; auto sQ = h.queryVidMem; auto sE = h.evictManagedTextures;
    h.budgetActive = &RetActive; h.queryVidMem = &RetVidMem; h.evictManagedTextures = &RetEvict;
    g_active = true; g_queryCount = 0; g_evictCount = 0;

    auto& g = TextureBudgetGlobals();
    g = TextureBudgetState{};
    g.limitA = 8; g.limitB = 8; g.missesA = 0; g.missesB = 0; g.lastTick = 0;
    g_total = 4;  // total < limitA -> pressure on limitA path
    g_free = 0;

    // First call: elapsed = 0x100 (not > 0x100) -> ++missesB == 1, no halving yet.
    u8 r = AdjustTextureBudget(0x100, 0);
    CHECK_EQ(g_evictCount, 1);
    CHECK_EQ(g.missesA, 0u);
    CHECK_EQ(g.missesB, 1u);
    CHECK_EQ(g.limitA, 8u);          // not yet halved (missesB just reached 1)
    CHECK_EQ(g.lastTick, 0x100u);
    (void)r;

    // Second call: elapsed from 0x100 to 0x200 == 0x100 (not > 0x100) ->
    // ++missesB == 2 && limitA >= 2 -> missesB=0, limitA >>= 1 (8->4).
    r = AdjustTextureBudget(0x200, 0);
    CHECK_EQ(g_evictCount, 2);
    CHECK_EQ(g.missesB, 0u);
    CHECK_EQ(g.limitA, 4u);
    CHECK_EQ(g.lastTick, 0x200u);

    h.budgetActive = sA; h.queryVidMem = sQ; h.evictManagedTextures = sE;
}

TEST(RenderReconObjList, AdjustBudgetBigElapsedResetsMissB) {
    auto& h = ObjListHooks();
    auto sA = h.budgetActive; auto sQ = h.queryVidMem; auto sE = h.evictManagedTextures;
    h.budgetActive = &RetActive; h.queryVidMem = &RetVidMem; h.evictManagedTextures = &RetEvict;
    g_active = true; g_evictCount = 0;

    auto& g = TextureBudgetGlobals();
    g = TextureBudgetState{};
    g.limitA = 8; g.limitB = 8; g.lastTick = 0;
    g_total = 4; g_free = 0;
    // elapsed = 0x200 > 0x100 -> missesB = 1 (set, not incremented), no halving.
    u8 r = AdjustTextureBudget(0x200, 0);
    CHECK_EQ(g.missesA, 0u);
    CHECK_EQ(g.missesB, 1u);
    CHECK_EQ(g.limitA, 8u);
    (void)r;

    h.budgetActive = sA; h.queryVidMem = sQ; h.evictManagedTextures = sE;
}

TEST(RenderReconObjList, AdjustBudgetAmpleFreeMemNoEvict) {
    auto& h = ObjListHooks();
    auto sA = h.budgetActive; auto sQ = h.queryVidMem; auto sE = h.evictManagedTextures;
    h.budgetActive = &RetActive; h.queryVidMem = &RetVidMem; h.evictManagedTextures = &RetEvict;
    g_active = true; g_evictCount = 0;

    auto& g = TextureBudgetGlobals();
    g = TextureBudgetState{};
    g.limitA = 4; g.limitB = 4; g.lastTick = 0;
    g_total = 100;       // total >= limitA -> check_free branch
    g_free = 0x20000;    // free >= 0x20000 -> return, no evict
    u8 r = AdjustTextureBudget(0x100, 0);
    CHECK_EQ(g_evictCount, 0);
    // gilde.exe 0x5b37e1/0x5b3881: on the post-query early-out, `al` was
    // overwritten by QueryAvailableVidMem's return (= low byte of `free`),
    // NOT the prior `tick - lastTick`. free == 0x20000 -> low byte 0x00.
    CHECK_EQ((int)r, 0x00);

    // total >= limitA, free < 0x20000, arg2 < limitB -> pressure on limitB path.
    g_free = 0x1000;
    g.lastTick = 0; g.limitA = 4; g.limitB = 8; g.missesA = 0; g.missesB = 0;
    r = AdjustTextureBudget(0x100, 0 /*arg2 < limitB*/);
    CHECK_EQ(g_evictCount, 1);
    CHECK_EQ(g.missesB, 0u);     // missesB reset in total>=limitA branch
    CHECK_EQ(g.missesA, 1u);     // ++missesA == 1 (elapsed 0x100 not > 0x100)
    (void)r;

    h.budgetActive = sA; h.queryVidMem = sQ; h.evictManagedTextures = sE;
}

// ---------------------------------------------------------------------------
// 0x5b9ef4 SetGammaTable — change detection + reload sequencing.
// ---------------------------------------------------------------------------
namespace {
u8 g_gamma = 0;
int g_cacheResetCount = 0;
int CacheReset() { ++g_cacheResetCount; return 0; }
std::vector<u32> g_reloaded;
int FloorReload(u32 hnd) { g_reloaded.push_back(hnd); return (int)hnd; }
u32 g_primaryFloor = 0;
u32 RetPrimary() { return g_primaryFloor; }
// Floor table: a fixed set of handles; visit non-zero ones != primary.
std::vector<u32> g_floorTable;
void ForEachFloor(u32 primary, void (*visit)(u32)) {
    for (u32 rec : g_floorTable)
        if (rec && rec != primary) visit(rec);
}
} // namespace

TEST(RenderReconObjList, SetGammaNoChangeNoOp) {
    auto& h = ObjListHooks();
    auto sG = h.gammaByte; auto sCR = h.textureCacheReset;
    auto sFR = h.floorReloadTextures; auto sAF = h.activeFloor; auto sFE = h.forEachFloorRecord;
    g_gamma = 5; g_cacheResetCount = 0; g_reloaded.clear();
    h.gammaByte = &g_gamma;
    h.textureCacheReset = &CacheReset;
    h.floorReloadTextures = &FloorReload;
    h.activeFloor = &RetPrimary;
    h.forEachFloorRecord = &ForEachFloor;

    int r = SetGammaTable(5);     // same as current -> no work
    CHECK_EQ(g_cacheResetCount, 0);
    CHECK_EQ((int)g_reloaded.size(), 0);
    CHECK_EQ(r, 0);

    h.gammaByte = sG; h.textureCacheReset = sCR;
    h.floorReloadTextures = sFR; h.activeFloor = sAF; h.forEachFloorRecord = sFE;
}

TEST(RenderReconObjList, SetGammaChangeReloadsFloors) {
    auto& h = ObjListHooks();
    auto sG = h.gammaByte; auto sCR = h.textureCacheReset;
    auto sFR = h.floorReloadTextures; auto sAF = h.activeFloor; auto sFE = h.forEachFloorRecord;
    g_gamma = 5; g_cacheResetCount = 0; g_reloaded.clear();
    g_primaryFloor = 100;
    g_floorTable = {100, 0, 200, 300}; // primary repeated + a zero + two others
    h.gammaByte = &g_gamma;
    h.textureCacheReset = &CacheReset;
    h.floorReloadTextures = &FloorReload;
    h.activeFloor = &RetPrimary;
    h.forEachFloorRecord = &ForEachFloor;

    int r = SetGammaTable(9);
    CHECK_EQ(g_cacheResetCount, 1);
    CHECK_EQ((int)g_gamma, 9);                 // gamma recorded
    // primary first, then table entries != primary && != 0 -> 200, 300.
    CHECK_EQ((int)g_reloaded.size(), 3);
    CHECK_EQ(g_reloaded[0], 100u);             // primary
    CHECK_EQ(g_reloaded[1], 200u);
    CHECK_EQ(g_reloaded[2], 300u);
    CHECK_EQ(r, 300);                          // last reload's return propagated

    h.gammaByte = sG; h.textureCacheReset = sCR;
    h.floorReloadTextures = sFR; h.activeFloor = sAF; h.forEachFloorRecord = sFE;
}

// ---------------------------------------------------------------------------
// 0x5e0e74 FreeObjectList — drains until head reaches the sentinel.
// ---------------------------------------------------------------------------
namespace {
std::vector<RenderNode*> g_listNodes;
RenderNode g_sentinel{};
size_t g_listCursor = 0;
RenderNode* ListHead() {
    return g_listCursor < g_listNodes.size() ? g_listNodes[g_listCursor] : &g_sentinel;
}
void ListAdvance() { ++g_listCursor; }
} // namespace

TEST(RenderReconObjList, FreeObjectListDrains) {
    auto& h = ObjListHooks();
    auto sFD = h.memoryFreeDebug; auto sFN = h.memoryFreeNode;
    auto sRT = h.releaseTextureEntry; auto sUL = h.unlinkObjectNode;
    h.memoryFreeDebug = &RecFreeDebug;
    h.memoryFreeNode = &RecFreeNode;
    h.releaseTextureEntry = &RecReleaseTex;
    h.unlinkObjectNode = &RecUnlink;
    ResetRecorders();

    RenderNodeOwner owner{};
    RenderNode a{}, b{}, c{};
    // Mark all non-owned so FreeObjectNode takes the unlink-hook branch (no real
    // pointer splicing needed for this drain test).
    RenderNodeOwner other{};
    a.owner = &other; b.owner = &other; c.owner = &other;
    g_listNodes = {&a, &b, &c};
    g_listCursor = 0;

    FreeObjectList(&ListHead, &ListAdvance, &g_sentinel, &owner);

    CHECK_EQ((int)g_freedNodes.size(), 3);
    CHECK(g_freedNodes[0] == &a);
    CHECK(g_freedNodes[1] == &b);
    CHECK(g_freedNodes[2] == &c);
    CHECK_EQ((int)g_unlinked.size(), 3); // each non-owned -> unlink hook

    h.memoryFreeDebug = sFD; h.memoryFreeNode = sFN;
    h.releaseTextureEntry = sRT; h.unlinkObjectNode = sUL;
}

// wave-12 boundary: an EMPTY list (head() == sentinel on the very first probe)
// drains with zero frees and zero hook calls — the for-loop must break before any
// access (no deref of the sentinel as a node).
TEST(RenderReconObjList, FreeObjectListEmptyNoFrees) {
    auto& h = ObjListHooks();
    auto sFD = h.memoryFreeDebug; auto sFN = h.memoryFreeNode;
    auto sRT = h.releaseTextureEntry; auto sUL = h.unlinkObjectNode;
    h.memoryFreeDebug = &RecFreeDebug;
    h.memoryFreeNode = &RecFreeNode;
    h.releaseTextureEntry = &RecReleaseTex;
    h.unlinkObjectNode = &RecUnlink;
    ResetRecorders();

    RenderNodeOwner owner{};
    g_listNodes.clear();        // head() returns &g_sentinel immediately
    g_listCursor = 0;
    FreeObjectList(&ListHead, &ListAdvance, &g_sentinel, &owner);

    CHECK_EQ((int)g_freedNodes.size(), 0);
    CHECK_EQ((int)g_unlinked.size(), 0);
    CHECK_EQ((int)g_freedHandles.size(), 0);

    h.memoryFreeDebug = sFD; h.memoryFreeNode = sFN;
    h.releaseTextureEntry = sRT; h.unlinkObjectNode = sUL;
}

// wave-12 boundary: a single owned node that is BOTH head and tail. The original
// splices node->prev/next (which point at the node itself for a 1-element ring),
// then sets head=tail=node->prev. We model a self-ring and assert no OOB and the
// documented (literal) head/tail fixup. Pins the FreeObjectNode prev/next deref
// path on the smallest possible list.
TEST(RenderReconObjList, FreeObjectNodeSingleHeadTailSelfRing) {
    auto& h = ObjListHooks();
    auto sFD = h.memoryFreeDebug; auto sFN = h.memoryFreeNode;
    auto sRT = h.releaseTextureEntry; auto sUL = h.unlinkObjectNode;
    h.memoryFreeDebug = &RecFreeDebug;
    h.memoryFreeNode = &RecFreeNode;
    h.releaseTextureEntry = &RecReleaseTex;
    h.unlinkObjectNode = &RecUnlink;
    ResetRecorders();

    RenderNodeOwner owner{};
    RenderNode only{};
    only.prev = &only;          // 1-element ring: prev/next point at itself
    only.next = &only;
    only.owner = &owner;        // owned -> splice branch (derefs prev/next)
    owner.head = &only;
    owner.tail = &only;
    only.texEntry = 0;          // no texture -> release hook not called

    FreeObjectNode(&only, &owner);

    // Self-splice: head/tail both become only.prev (== only, written before free).
    CHECK(owner.head == &only);
    CHECK(owner.tail == &only);
    CHECK_EQ((int)g_unlinked.size(), 0);
    CHECK_EQ((int)g_releasedTex.size(), 0);   // texEntry == 0
    CHECK_EQ((int)g_freedNodes.size(), 1);
    CHECK(g_freedNodes[0] == &only);

    h.memoryFreeDebug = sFD; h.memoryFreeNode = sFN;
    h.releaseTextureEntry = sRT; h.unlinkObjectNode = sUL;
}
