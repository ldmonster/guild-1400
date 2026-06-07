#include "test.h"

#include "sim/object_lifecycle10.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

// ===========================================================================
// Captured-call recorders for the hooks.
// ===========================================================================
namespace {

struct Rec {
    int spawnCalls = 0;
    int setParentCalls = 0;
    int setPositionCalls = 0;
    int setWorldXlateCalls = 0;
    int linkSceneCalls = 0;
    int disposeCalls = 0;
    int attachLodsCalls = 0;
    int switchSlotCalls = 0;
    int resetSlotCalls = 0;
    int freeNodeCalls = 0;
    int heightmapFreeCalls = 0;
    int initEntryCalls = 0;
    float lastPos[3] = {0, 0, 0};
    float lastXlate[3] = {0, 0, 0};
    int lastSwitchHi = -777;
    int lastSwitchSlot = -777;
};
Rec g_rec;

SceneNode10* g_spawned = nullptr;

void* SpawnReturnsNode(int kind, const char*) {
    g_rec.spawnCalls++;
    (void)kind;
    return g_spawned;
}
void SetParent(void*, SceneNode10*) { g_rec.setParentCalls++; }
void SetPosition(SceneNode10*, const float* p) {
    g_rec.setPositionCalls++;
    if (p) std::memcpy(g_rec.lastPos, p, sizeof(g_rec.lastPos));
}
void SetWorldXlate(SceneNode10*, const float* p) {
    g_rec.setWorldXlateCalls++;
    if (p) std::memcpy(g_rec.lastXlate, p, sizeof(g_rec.lastXlate));
}
void LinkScene(SceneNode10*) { g_rec.linkSceneCalls++; }
void Dispose(SceneNode10*) { g_rec.disposeCalls++; }
void AttachLods(SceneNode10*, const char*) { g_rec.attachLodsCalls++; }
void SwitchSlot(int hi, int, int slot, void*) {
    g_rec.switchSlotCalls++;
    g_rec.lastSwitchHi = hi;
    g_rec.lastSwitchSlot = slot;
}
int ResetSlot(void*) { return ++g_rec.resetSlotCalls; }
void FreeNode(void*) { g_rec.freeNodeCalls++; }
int HeightmapFree(void*, void*) { return ++g_rec.heightmapFreeCalls; }
void CaptorInitEntry(u8* entry, void* node) {
    g_rec.initEntryCalls++;
    // mirror just the node-slot write so the test can confirm wiring.
    i32 v = static_cast<i32>(reinterpret_cast<std::intptr_t>(node));
    std::memcpy(entry + 24, &v, 4);
}

std::vector<unsigned char>* g_arena = nullptr;
void* ArenaAlloc(unsigned size, const char*) {
    if (!g_arena) return nullptr;
    g_arena->assign(size, 0);
    return g_arena->data();
}

}  // namespace

// ===========================================================================
// InitStruct — seeds the default flag bytes and the light-block defaults, and routes
// the zero re-seat through the position / world-translation hooks.
// ===========================================================================
TEST(ObjLifecycle10, InitStructSeedsFlagsAndLightBlockAndReSeatsZero) {
    SceneNode10 node;
    node.b(528) = 0xFF; node.b(529) = 0xFF; node.b(530) = 0xFF; node.b(531) = 0xFF;
    node.b(532) = 0xFF;
    std::vector<unsigned char> light(512, 0xCD);   // passed explicitly (LP64)

    ObjLife10Hooks h{};
    h.objSetPosition = &SetPosition;
    h.objSetWorldTranslation = &SetWorldXlate;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    void* rc = ObjectInitStruct(&node, light.data());
    CHECK(rc == &node);

    CHECK_EQ((int)node.b(533), 1);                     // attachKind = 1
    CHECK_EQ((int)node.b(535), 0);
    CHECK_EQ((int)node.b(532), 0);
    CHECK_EQ((int)node.b(529), 0xA2);                  // (0xFF&0xA0)|2
    CHECK_EQ((int)node.b(528), 0x7D);                  // ((0xFF&0x7D)&0x9A)|0x65
    CHECK_EQ((int)node.b(530), 0xC1);                  // 0xFF&0xC1
    CHECK_EQ((int)node.b(531), 0xCF);                  // 0xFF&0xCF

    auto lrd = [&](int off) { i32 v; std::memcpy(&v, light.data() + off, 4); return v; };
    CHECK_EQ(lrd(408), 0);
    CHECK_EQ(lrd(412), kInitFloatBits);
    CHECK_EQ(lrd(416), 15);
    CHECK_EQ(lrd(420), 0);
    CHECK_EQ(lrd(52), 15);                  // i=0 body
    CHECK_EQ(lrd(56 - 8), kInitFloatBits);  // i=56 increment-clause => +48

    CHECK_EQ(g_rec.setWorldXlateCalls, 1);
    CHECK_EQ(g_rec.setPositionCalls, 1);
    CHECK_EQ(g_rec.lastPos[0], 0.0f);
    CHECK_EQ(g_rec.lastPos[1], 0.0f);
    CHECK_EQ(g_rec.lastPos[2], 0.0f);

    ObjLife10ResetHooks();
}

// InitStruct must tolerate a null light block (matching v2[122]==0).
TEST(ObjLifecycle10, InitStructNullLightBlockSkipsLightSeed) {
    SceneNode10 node;
    ObjLife10ResetHooks();
    void* rc = ObjectInitStruct(&node, nullptr);
    CHECK(rc == &node);
    CHECK_EQ((int)node.b(533), 1);
}

// ===========================================================================
// AllocDrawData — allocates the +492 block, seeds 4 submesh entries through the
// InitSubMeshEntry hook, sets the trailer defaults, and is idempotent.
// ===========================================================================
TEST(ObjLifecycle10, AllocDrawDataAllocatesSeedsEntriesAndTrailer) {
    std::vector<unsigned char> arena;
    g_arena = &arena;
    ObjLife10Hooks h{};
    h.memAllocDebug = &ArenaAlloc;
    h.initSubMeshEntry = &CaptorInitEntry;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    SceneNode10 node;
    node.p(n10::kDrawData) = nullptr;
    void* blk = ObjectAllocDrawData(&node);
    CHECK(blk != nullptr);
    CHECK_EQ(g_rec.initEntryCalls, 4);     // 4 submesh entries seeded
    if (blk) {
        unsigned char* draw = static_cast<unsigned char*>(blk);
        auto rd = [&](int off) { i32 v; std::memcpy(&v, draw + off, 4); return v; };
        for (int e = 0; e < 4; ++e) {
            int base = 244 + e * 384;
            CHECK_EQ(rd(base + 24),
                     (i32)reinterpret_cast<std::intptr_t>(&node));
        }
        CHECK_EQ((int)draw[2316], 0);
        CHECK_EQ(rd(2296), kOneFloatBits);   // 1.0f
        CHECK_EQ(rd(176), -1);
        CHECK_EQ((int)(draw[2317] & 0x40), 0x40);
        CHECK(node.p(n10::kDrawData) == blk);
    }
    void* blk2 = ObjectAllocDrawData(&node);
    CHECK(blk2 == blk);                      // idempotent, no realloc
    CHECK_EQ(g_rec.initEntryCalls, 4);       // not seeded again

    g_arena = nullptr;
    ObjLife10ResetHooks();
}

// The default (no-hook) entry seeder mirrors the original byte semantics: -1 sentinel
// at +376, node slot at +24, +22 flag bit1 cleared on the 3 sub-slots.
TEST(ObjLifecycle10, AllocDrawDataDefaultSeederWritesSentinels) {
    std::vector<unsigned char> arena;
    g_arena = &arena;
    ObjLife10Hooks h{};
    h.memAllocDebug = &ArenaAlloc;   // no initSubMeshEntry => default seeder
    ObjLife10SetHooks(h);

    SceneNode10 node;
    void* blk = ObjectAllocDrawData(&node);
    CHECK(blk != nullptr);
    if (blk) {
        unsigned char* draw = static_cast<unsigned char*>(blk);
        // first entry at +244: -1 sentinel byte at +376 within the entry.
        CHECK_EQ((int)draw[244 + 376], 0xFF);
        // node slot written.
        i32 ns; std::memcpy(&ns, draw + 244 + 24, 4);
        CHECK_EQ(ns, (i32)reinterpret_cast<std::intptr_t>(&node));
    }
    g_arena = nullptr;
    ObjLife10ResetHooks();
}

// ===========================================================================
// AttachToUniverseNode — the resident-mesh success path links into the scene and
// seats position/world-translation; the non-resident path Disposes and returns null.
// ===========================================================================
TEST(ObjLifecycle10, AttachToUniverseNodeResidentLinksAndSeats) {
    SceneNode10 node;
    std::vector<unsigned char> draw(0x910, 0);
    void* firstSub = reinterpret_cast<void*>(static_cast<std::intptr_t>(1));
    SetBlockPtr(draw.data(), 260, firstSub);   // native-width resident marker
    draw[2316] = 0;                            // submesh count 0 => no tex walk
    node.p(n10::kDrawData) = draw.data();
    g_spawned = &node;

    ObjLife10Hooks h{};
    h.objSpawn = &SpawnReturnsNode;
    h.objSetParent = &SetParent;
    h.objSetPosition = &SetPosition;
    h.objSetWorldTranslation = &SetWorldXlate;
    h.objLinkIntoScene = &LinkScene;
    h.objDispose = &Dispose;
    h.meshAttachLods = &AttachLods;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    float pos[3] = {1.0f, 2.0f, 3.0f};
    float xlate[3] = {4.0f, 5.0f, 6.0f};
    void* out = ObjectAttachToUniverseNode(nullptr, pos, "house", xlate, nullptr);
    CHECK(out == &node);
    CHECK_EQ(g_rec.spawnCalls, 1);
    CHECK_EQ(g_rec.attachLodsCalls, 1);
    CHECK_EQ(g_rec.setPositionCalls, 1);
    CHECK_EQ(g_rec.setWorldXlateCalls, 1);
    CHECK_EQ(g_rec.linkSceneCalls, 1);   // no parent => LinkIntoScene
    CHECK_EQ(g_rec.setParentCalls, 0);
    CHECK_EQ(g_rec.disposeCalls, 0);
    CHECK_EQ(g_rec.lastPos[0], 1.0f);
    CHECK_EQ(g_rec.lastXlate[2], 6.0f);

    ObjLife10ResetHooks();
}

TEST(ObjLifecycle10, AttachToUniverseNodeNonResidentDisposesReturnsNull) {
    SceneNode10 node;
    std::vector<unsigned char> draw(0x910, 0);   // +260 == 0 => NOT resident
    node.p(n10::kDrawData) = draw.data();
    g_spawned = &node;

    ObjLife10Hooks h{};
    h.objSpawn = &SpawnReturnsNode;
    h.objDispose = &Dispose;
    h.meshAttachLods = &AttachLods;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "house", nullptr, nullptr);
    CHECK(out == nullptr);
    CHECK_EQ(g_rec.disposeCalls, 1);
    CHECK_EQ(g_rec.linkSceneCalls, 0);

    ObjLife10ResetHooks();
}

TEST(ObjLifecycle10, AttachToUniverseNodeNullSpawnReturnsNull) {
    g_spawned = nullptr;
    ObjLife10Hooks h{};
    h.objSpawn = &SpawnReturnsNode;
    ObjLife10SetHooks(h);
    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "x", nullptr, nullptr);
    CHECK(out == nullptr);
    ObjLife10ResetHooks();
}

// AttachToUniverseNode: with a parent, SetParent fires and LinkIntoScene does NOT.
TEST(ObjLifecycle10, AttachToUniverseNodeWithParentSkipsLinkIntoScene) {
    SceneNode10 node;
    std::vector<unsigned char> draw(0x910, 0);
    SetBlockPtr(draw.data(), 260, reinterpret_cast<void*>(static_cast<std::intptr_t>(1)));
    draw[2316] = 0;
    node.p(n10::kDrawData) = draw.data();
    g_spawned = &node;

    ObjLife10Hooks h{};
    h.objSpawn = &SpawnReturnsNode;
    h.objSetParent = &SetParent;
    h.objLinkIntoScene = &LinkScene;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    int parentDummy = 1;
    void* out = ObjectAttachToUniverseNode(&parentDummy, nullptr, "house", nullptr, nullptr);
    CHECK(out == &node);
    CHECK_EQ(g_rec.setParentCalls, 1);
    CHECK_EQ(g_rec.linkSceneCalls, 0);

    ObjLife10ResetHooks();
}

// ===========================================================================
// FindByHandle / FindByName — null-query early-out, +124 link snapshot/restore, the
// walk-flags high bit, and the walk recording a hit via the REAL Match* predicates
// (object_lifecycle3) invoked through an installed walk hook. Find* operate on
// SceneNode3 (the reused FindCtx/predicate types).
// ===========================================================================
namespace {
SceneNode3* g_walkNode = nullptr;
void WalkOnce(void*, void*, void* cb, u16, void* resultSlot) {
    auto fn = reinterpret_cast<bool (*)(SceneNode3*, FindCtx*)>(cb);
    fn(g_walkNode, static_cast<FindCtx*>(resultSlot));
}
int g_walkCalls = 0;
u16 g_walkFlagsSeen = 0;
void WalkRecorder(void*, void*, void*, u16 flags, void*) {
    g_walkCalls++;
    g_walkFlagsSeen = flags;
}
}  // namespace

TEST(ObjLifecycle10, FindByHandleNullQueryReturnsNull) {
    ObjLife10ResetHooks();
    SceneNode3* r = ObjectFindByHandle(nullptr, 0, nullptr, nullptr, nullptr);
    CHECK(r == nullptr);
}

TEST(ObjLifecycle10, FindByHandleSnapshotsLinkSetsFlagBitAndWalks) {
    SceneNode3 root;
    void* linkVal = reinterpret_cast<void*>(static_cast<std::intptr_t>(0x1234));
    SetBlockPtr(root.raw, 496, linkVal);   // +124 selfLink (native-width)

    ObjLife10Hooks h{};
    h.sceneWalkAndInvoke = &WalkRecorder;
    ObjLife10SetHooks(h);
    g_walkCalls = 0; g_walkFlagsSeen = 0;

    SceneNode3* r = ObjectFindByHandle(&root, 0x0000, "x", nullptr, nullptr);
    CHECK(r == nullptr);                 // recorder didn't set found
    CHECK_EQ(g_walkCalls, 1);
    CHECK_EQ((int)(g_walkFlagsSeen & kWalkFlagHiBit), (int)kWalkFlagHiBit);  // bit set
    CHECK(BlockPtr(root.raw, 496) == linkVal);   // link restored

    ObjLife10ResetHooks();
}

TEST(ObjLifecycle10, FindByHandleWalkFindsNodeByHandle) {
    SceneNode3 root, target;
    g_walkNode = &target;
    ObjLife10Hooks h{};
    h.sceneWalkAndInvoke = &WalkOnce;
    ObjLife10SetHooks(h);

    SceneNode3* r = ObjectFindByHandle(&root, 0, nullptr, &target, nullptr);
    CHECK(r == &target);

    ObjLife10ResetHooks();
}

TEST(ObjLifecycle10, FindByNameWalkFindsNodeByName) {
    SceneNode3 root, target;
    std::strcpy(reinterpret_cast<char*>(target.raw), "well");
    g_walkNode = &target;
    ObjLife10Hooks h{};
    h.sceneWalkAndInvoke = &WalkOnce;
    ObjLife10SetHooks(h);

    SceneNode3* r = ObjectFindByName(&root, 0, "well", nullptr, nullptr);
    CHECK(r == &target);
    SceneNode3* r2 = ObjectFindByName(&root, 0, "WELL", nullptr, nullptr);  // ci
    CHECK(r2 == &target);

    ObjLife10ResetHooks();
}

// A leading '!'(=33) in the node name is stripped before the compare (faithful to
// the REAL MatchNameCallback predicate).
TEST(ObjLifecycle10, FindByNameStripsLeadingBang) {
    SceneNode3 root, target;
    std::strcpy(reinterpret_cast<char*>(target.raw), "!barrel");
    g_walkNode = &target;
    ObjLife10Hooks h{};
    h.sceneWalkAndInvoke = &WalkOnce;
    ObjLife10SetHooks(h);
    SceneNode3* r = ObjectFindByName(&root, 0, "barrel", nullptr, nullptr);
    CHECK(r == &target);
    ObjLife10ResetHooks();
}

// ===========================================================================
// DestroySpawnedEntities — resets each active slot, frees the root node and the
// heightmap pool, and clears the active flags.
// ===========================================================================
TEST(ObjLifecycle10, DestroySpawnedEntitiesResetsActiveSlotsAndPool) {
    std::vector<unsigned char> slotsA(4 + 731 + 4, 0);
    slotsA[4 + 5] = 0x10;                 // slot 5 active (>0)
    slotsA[6 + 3] = 0x07;                 // dword at base+6 (=base+1+5), top byte hi 7
    std::vector<unsigned char> slotsB(3 + 72 + 4, 0);
    slotsB[3 + 2] = 0x20;                 // slot 2 active
    slotsB[2 + 3] = 0x09;                 // dword at base+2, top byte hi 9
    std::vector<i32> hm(15744, 0);
    hm[0] = 0xAAAA;
    hm[246] = 0xBBBB;

    ObjLife10Hooks h{};
    h.universeSwitchActiveSlot = &SwitchSlot;
    h.universeResetCurrentSlot = &ResetSlot;
    h.sceneGraphFreeNodeRecursive = &FreeNode;
    h.heightmapFree = &HeightmapFree;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    void* root = reinterpret_cast<void*>(static_cast<std::intptr_t>(0x1234));
    void* rootSlot = root;
    DestroyTables10 t;
    t.slotsA = slotsA.data();
    t.slotsB = slotsB.data();
    t.heightmaps = hm.data();
    int rc = ObjectDestroySpawnedEntities(0, root, &rootSlot, t, nullptr);

    CHECK_EQ(g_rec.freeNodeCalls, 1);
    CHECK(rootSlot == nullptr);
    CHECK_EQ(g_rec.resetSlotCalls, 3);   // initial + A + B
    CHECK_EQ(g_rec.heightmapFreeCalls, 2);
    CHECK_EQ(rc, 2);                     // last heightmapFree rc

    CHECK_EQ((int)slotsA[4 + 5], 0xFF);
    CHECK_EQ((int)slotsB[3 + 2], 0xFF);
    CHECK_EQ(hm[0], 0);
    CHECK_EQ(hm[246], 0);
    CHECK_EQ(g_rec.lastSwitchSlot, 2);   // last switch was table-B slot 2
    CHECK_EQ(g_rec.lastSwitchHi, 9);
    CHECK_EQ(g_rec.switchSlotCalls, 3);  // firstSlot + A + B

    ObjLife10ResetHooks();
}

// firstSlot is passed verbatim to the first SwitchActiveSlot (not the root node).
TEST(ObjLifecycle10, DestroySpawnedEntitiesPassesFirstSlotVerbatim) {
    ObjLife10Hooks h{};
    h.universeSwitchActiveSlot = &SwitchSlot;
    h.universeResetCurrentSlot = &ResetSlot;
    ObjLife10SetHooks(h);
    g_rec = Rec{};

    DestroyTables10 t;   // all tables null => only the first switch + reset
    int rc = ObjectDestroySpawnedEntities(42, nullptr, nullptr, t, nullptr);
    CHECK_EQ(g_rec.switchSlotCalls, 1);
    CHECK_EQ(g_rec.lastSwitchSlot, 42);
    CHECK_EQ(g_rec.lastSwitchHi, 0);
    CHECK_EQ(rc, 1);
    ObjLife10ResetHooks();
}
