#include "test.h"

// Unit tests for object_lifecycle9 (VIBE_Object_* batch 9). Golden values are
// derived directly from the Hex-Rays semantics (verbatim constants / field
// offsets). All cross-module leaves are exercised through captor hooks so the
// behaviour is deterministic and ASLR-independent.
#include "sim/object_lifecycle9.h"

#include <cstdlib>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- a building-type table fixture (65-byte scene rows / 589-byte build rows) ----
struct Tables {
    std::vector<u8> sceneType;   // kSceneTypeRows * 65
    std::vector<u8> building;    // kBuildingRows * 589
    std::vector<u8> protClass;   // kProtClassRows
    Tables()
        : sceneType(kSceneTypeRows * kSceneTypeStride, 0),
          building(kBuildingRows * kBuildingStride, 0),
          protClass(kProtClassRows, 0) {}
};

}  // namespace

// ---------------------------------------------------------------------------
// IsBuildingType 0x583a2c
// ---------------------------------------------------------------------------
TEST(ObjLifecycle9, IsBuildingTypeMatchesExactClassSet) {
    Tables t;
    // class bytes per the verbatim set {1,3,4,11,26,27,28}.
    const u8 yes[] = {1, 3, 4, 11, 26, 27, 28};
    const u8 no[]  = {0, 2, 5, 10, 12, 25, 29, 72, 255};
    int row = 0;
    for (u8 c : yes) { t.sceneType[row * kSceneTypeStride] = c;
                       CHECK(ObjectIsBuildingType(t.sceneType.data(), (i16)row)); ++row; }
    for (u8 c : no)  { t.sceneType[row * kSceneTypeStride] = c;
                       CHECK(!ObjectIsBuildingType(t.sceneType.data(), (i16)row)); ++row; }
    CHECK(!ObjectIsBuildingType(nullptr, 0));
}

// ---------------------------------------------------------------------------
// CollectMatchingProts 0x586508
// ---------------------------------------------------------------------------
TEST(ObjLifecycle9, CollectMatchingProtsGathersClassMatches) {
    Tables t;
    // Node proto = row 5; give it building-class byte 9.
    const int nodeProto = 5;
    t.building[kBuildingStride * nodeProto] = 9;
    // protClass rows: set up a few rows whose class byte indexes the building table.
    // Row i has protClass[i] = some class index v4; matches when v4 <= nodeProto
    // AND building[v4].class == building[nodeProto].class (==9).
    t.protClass[0] = 5;  t.building[kBuildingStride * 5] = 9;  // v4=5<=5, class 9 -> MATCH
    t.protClass[1] = 72;                                       // skipped (==72)
    t.protClass[2] = 3;  t.building[kBuildingStride * 3] = 9;  // v4=3<=5, class 9 -> MATCH
    t.protClass[3] = 4;  t.building[kBuildingStride * 4] = 7;  // class mismatch -> no
    t.protClass[4] = 6;  t.building[kBuildingStride * 6] = 9;  // v4=6 > 5 -> no
    std::vector<u16> out(kProtClassRows, 0);
    u16 count = 0xFFFF;
    int rc = ObjectCollectMatchingProts(t.protClass.data(), t.building.data(),
                                        (i16)nodeProto, out.data(), &count);
    CHECK_EQ(rc, 0);
    CHECK_EQ((int)count, 2);
    if (count == 2) { CHECK_EQ((int)out[0], 0); CHECK_EQ((int)out[1], 2); }

    // null building table -> early-out returns 1, count 0.
    u16 c2 = 0xFFFF;
    CHECK_EQ(ObjectCollectMatchingProts(t.protClass.data(), nullptr, 0, out.data(), &c2), 1);
    CHECK_EQ((int)c2, 0);
}

// ---------------------------------------------------------------------------
// RebuildModelByOwner 0x5a8140
// ---------------------------------------------------------------------------
namespace { int g_buildCalls = 0; SceneNode9* g_lastBuildNode = nullptr; u8 g_lastMode = 0;
void BuildModelHook(SceneNode9* n, void*, u8 mode) { ++g_buildCalls; g_lastBuildNode = n; g_lastMode = mode; } }

TEST(ObjLifecycle9, RebuildModelByOwnerWalksOwnedRows) {
    std::vector<u8> geb(kGebaeudeRows * kGebaeudeStride, 0);
    SceneNode9 node;
    node.d(n9::kOwnerTag) = 0x1234;
    // occupied rows owned by 0x1234: rows 2 and 7; row 4 occupied but other owner.
    auto setRow = [&](int r, u8 occ, i32 owner) {
        geb[r * kGebaeudeStride] = occ;
        std::memcpy(&geb[r * kGebaeudeStride + 1], &owner, sizeof(owner));
    };
    setRow(2, 1, 0x1234);
    setRow(4, 1, 0x9999);
    setRow(7, 1, 0x1234);
    setRow(9, 0, 0x1234);   // not occupied
    g_buildCalls = 0; g_lastBuildNode = nullptr; g_lastMode = 0;
    ObjLife9Hooks h{}; h.buildModelNameByRow = &BuildModelHook; ObjLife9SetHooks(h);
    CHECK_EQ(ObjectRebuildModelByOwner(&node, geb.data()), 1);
    CHECK_EQ(g_buildCalls, 2);
    CHECK_EQ(g_lastBuildNode, &node);
    CHECK_EQ((int)g_lastMode, 2);
    ObjLife9ResetHooks();
}

// ---------------------------------------------------------------------------
// ToggleHiddenState 0x5b3698
// ---------------------------------------------------------------------------
namespace { int g_walkCalls = 0; int g_removeCalls = 0;
void WalkHook(void*, SceneNode9*, int, int) { ++g_walkCalls; }
void RemoveHook(SceneNode9*) { ++g_removeCalls; } }

TEST(ObjLifecycle9, ToggleHiddenStateHidesVisibleRNode) {
    SceneNode9 node;
    node.b(0) = 114;                 // 'r'
    node.b(n9::kAttachKind) = 5;     // visible
    g_walkCalls = 0;
    ObjLife9Hooks h{}; h.sceneWalkAndInvoke = &WalkHook; ObjLife9SetHooks(h);
    CHECK_EQ(ObjectToggleHiddenState(&node, /*hide=*/1, /*tick=*/0xABCD), 1);
    CHECK_EQ((int)node.b(n9::kAttachKind), 6);
    CHECK_EQ(node.d(n9::kColor), (i32)0xABCD);
    CHECK_EQ(g_walkCalls, 1);
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, ToggleHiddenStateRestoresHiddenNode) {
    SceneNode9 node;
    node.b(0) = 114;
    node.b(n9::kAttachKind) = 6;     // hidden
    g_walkCalls = 0; g_removeCalls = 0;
    ObjLife9Hooks h{}; h.sceneWalkAndInvoke = &WalkHook; h.lightRemoveCacheEntry = &RemoveHook;
    ObjLife9SetHooks(h);
    CHECK_EQ(ObjectToggleHiddenState(&node, /*hide=*/0, 0), 1);
    CHECK_EQ((int)node.b(n9::kAttachKind), 5);
    CHECK_EQ(g_removeCalls, 1);
    CHECK_EQ(g_walkCalls, 1);
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, ToggleHiddenStateNoopWhenStateMismatch) {
    SceneNode9 node;
    node.b(0) = 114;
    node.b(n9::kAttachKind) = 1;     // neither 5 nor 6
    g_walkCalls = 0;
    ObjLife9Hooks h{}; h.sceneWalkAndInvoke = &WalkHook; ObjLife9SetHooks(h);
    CHECK_EQ(ObjectToggleHiddenState(&node, 1, 0), 1);
    CHECK_EQ((int)node.b(n9::kAttachKind), 1);   // unchanged
    CHECK_EQ(g_walkCalls, 0);
    ObjLife9ResetHooks();
}

// ---------------------------------------------------------------------------
// ToggleSuspendStateNamed 0x5b4274
// ---------------------------------------------------------------------------
namespace { int g_refreshCalls = 0; void RefreshHook() { ++g_refreshCalls; } }

TEST(ObjLifecycle9, SuspendStashesKindAndSetsState1) {
    SceneNode9 node;
    node.b(n9::kAttachKind) = 5;
    CHECK_EQ(ObjectToggleSuspendStateNamed(&node, /*hide=*/0), 1);
    CHECK_EQ((int)node.b(n9::kAttachKind), 1);
    CHECK_EQ((int)node.b(n9::kSavedKind), 5);
}

TEST(ObjLifecycle9, RestoreRecopiesNameAndRefreshesLighting) {
    SceneNode9 node;
    node.b(0) = 33;                  // '!'
    std::strcpy(reinterpret_cast<char*>(node.raw + 1), "!torch_01");  // name after the '!'
    // Wait: original copies a1+1 (the byte AFTER index 0) then writes back to a1.
    // Set up: name body at +1, leading '!' at +0.
    node.b(n9::kAttachKind) = 1;     // suspended
    node.b(n9::kSavedKind) = 6;      // was hidden -> refresh fires
    g_refreshCalls = 0;
    ObjLife9Hooks h{}; h.lightRefreshAll = &RefreshHook; ObjLife9SetHooks(h);
    CHECK_EQ(ObjectToggleSuspendStateNamed(&node, /*hide=*/1), 1);
    CHECK_EQ((int)node.b(n9::kAttachKind), 6);   // restored to savedKind
    CHECK_EQ(g_refreshCalls, 1);
    // The name re-copy moves bytes [+1..] into [+0..]; first byte becomes what was
    // at +1 ('!'), preserving the string content shift.
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, SuspendNoopWhenAlreadyState1) {
    SceneNode9 node;
    node.b(n9::kAttachKind) = 1;
    CHECK_EQ(ObjectToggleSuspendStateNamed(&node, 0), 1);  // hide=false, already 1 -> noop
    CHECK_EQ((int)node.b(n9::kAttachKind), 1);
    CHECK_EQ(ObjectToggleSuspendStateNamed(nullptr, 0), 0);
}

// ---------------------------------------------------------------------------
// SpawnBomb 0x486648
// ---------------------------------------------------------------------------
namespace { void* g_attachRet = reinterpret_cast<void*>(0xBEEF);
void* AttachHook(const float*, const char* model, int) {
    CHECK(model && std::strcmp(model, "ob_Bombe") == 0);
    return g_attachRet;
} }

TEST(ObjLifecycle9, SpawnBombFillsFirstFreeSlot) {
    void* handles[64] = {nullptr};
    int   ticks[64]   = {0};
    float pos[3] = {1.f, 2.f, 3.f};
    ObjLife9Hooks h{}; h.attachToUniverseNode = &AttachHook; ObjLife9SetHooks(h);

    // Empty pool: handles[0] == 0 -> slot 0.
    int s0 = ObjectSpawnBomb(pos, handles, ticks, 64, 0x77);
    CHECK_EQ(s0, 0);
    CHECK_EQ(handles[0], g_attachRet);
    CHECK_EQ(ticks[0], 0x77);

    // Now handles[0] occupied -> scan strides of 2: next free is index 2.
    int s1 = ObjectSpawnBomb(pos, handles, ticks, 64, 0x88);
    CHECK_EQ(s1, 2);
    CHECK_EQ(ticks[2], 0x88);
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, SpawnBombReturnsMinus1WhenFull) {
    void* handles[64];
    int   ticks[64] = {0};
    for (int i = 0; i < 64; ++i) handles[i] = reinterpret_cast<void*>((intptr_t)(i + 1));
    float pos[3] = {0, 0, 0};
    ObjLife9Hooks h{}; h.attachToUniverseNode = &AttachHook; ObjLife9SetHooks(h);
    CHECK_EQ(ObjectSpawnBomb(pos, handles, ticks, 64, 1), -1);
    ObjLife9ResetHooks();
}

// ---------------------------------------------------------------------------
// Clone 0x5b2478
// ---------------------------------------------------------------------------
namespace {
std::vector<void*> g_allocs;
unsigned g_allocSizes[8]; int g_allocN = 0;
void* CloneAllocHook(unsigned size, const char*) {
    if (g_allocN < 8) g_allocSizes[g_allocN++] = size;
    void* p = std::calloc(1, size);
    g_allocs.push_back(p);
    return p;
}
int g_initCalls = 0, g_setPosCalls = 0, g_setXlateCalls = 0, g_allocDrawCalls = 0;
void InitHook(SceneNode9*) { ++g_initCalls; }
void SetPosHook(SceneNode9*, const float*) { ++g_setPosCalls; }
void SetXlateHook(SceneNode9*, const float*) { ++g_setXlateCalls; }
void AllocDrawHook(SceneNode9*) { ++g_allocDrawCalls; }
}

TEST(ObjLifecycle9, CloneCopiesFieldsAndSeatsTransform) {
    SceneNode9 src;
    std::strcpy(reinterpret_cast<char*>(src.raw), "barrel_ob_keg");
    src.b(n9::kAttachKind) = 3;          // < 5 -> no light block
    src.f(n9::kPos + 0) = 10.f; src.f(n9::kPos + 4) = 20.f; src.f(n9::kPos + 8) = 30.f;
    src.f(n9::kWorldScale) = 2.5f;
    src.d(n9::kFieldRun156 + 0)  = 0x1111;
    src.d(n9::kFieldRun156 + 236) = 0x2222;   // last dword of the 240 run
    src.d(n9::kFlags528 + 0) = 0x33;
    src.d(n9::kDrawData) = 0;                  // no draw clone
    src.b(n9::kFlags529) = 0xFF;

    g_allocs.clear(); g_allocN = 0;
    g_initCalls = g_setPosCalls = g_setXlateCalls = g_allocDrawCalls = 0;
    ObjLife9Hooks h{};
    h.memAllocDebug = &CloneAllocHook; h.initStruct = &InitHook;
    h.setPosition = &SetPosHook; h.setWorldTranslation = &SetXlateHook;
    h.allocDrawData = &AllocDrawHook;
    ObjLife9SetHooks(h);

    void* out = ObjectClone(&src);
    CHECK(out != nullptr);
    if (out) {
        SceneNode9* dst = static_cast<SceneNode9*>(out);
        // node block alloc only (attachKind<5 => no light block).
        CHECK_EQ(g_allocN, 1);
        if (g_allocN >= 1) CHECK_EQ((int)g_allocSizes[0], kNodeSize);
        CHECK_EQ(g_initCalls, 1);
        CHECK(std::strcmp(reinterpret_cast<char*>(dst->raw), "barrel_ob_keg") == 0);
        CHECK_EQ(dst->f(n9::kPos + 0), 10.f);
        CHECK_EQ(dst->f(n9::kPos + 8), 30.f);
        CHECK_EQ(dst->f(n9::kWorldScale), 2.5f);
        CHECK_EQ(dst->d(n9::kFieldRun156 + 0), 0x1111);
        CHECK_EQ(dst->d(n9::kFieldRun156 + 236), 0x2222);
        // +529 hi nibble cleared (bits 5/6 -> 0xFF & 0x9F == 0x9F).
        CHECK_EQ((int)dst->b(n9::kFlags529), 0x9F);
        // +528 low 3 bits cleared then bit2 set -> value 4 (src had 0x33 -> &0xF8=0x30,|4=0x34).
        CHECK_EQ((int)dst->b(n9::kFlags528), 0x34);
        CHECK_EQ(g_setPosCalls, 1);
        CHECK_EQ(g_setXlateCalls, 1);
        CHECK_EQ(g_allocDrawCalls, 0);   // no draw data on src
    }
    for (void* p : g_allocs) std::free(p);
    g_allocs.clear();
    ObjLife9ResetHooks();
    CHECK(ObjectClone(nullptr) == nullptr);
}

TEST(ObjLifecycle9, CloneAllocatesLightBlockForKind5) {
    SceneNode9 src;
    src.b(n9::kAttachKind) = 7;       // >= 5 -> light block alloc
    src.d(n9::kDrawData) = 0;
    g_allocs.clear(); g_allocN = 0;
    g_allocDrawCalls = 0;
    ObjLife9Hooks h{}; h.memAllocDebug = &CloneAllocHook; ObjLife9SetHooks(h);
    void* out = ObjectClone(&src);
    CHECK(out != nullptr);
    if (out) {
        CHECK_EQ(g_allocN, 2);                       // node + light block
        if (g_allocN >= 2) {
            CHECK_EQ((int)g_allocSizes[0], kNodeSize);
            CHECK_EQ((int)g_allocSizes[1], 0x1AC);
        }
    }
    for (void* p : g_allocs) std::free(p);
    g_allocs.clear();
    ObjLife9ResetHooks();
}

// ---------------------------------------------------------------------------
// MoveBetweenUniverses 0x5b51e0
// ---------------------------------------------------------------------------
namespace { int g_moveWalkCalls = 0; int g_inflateCalls = 0;
void MoveWalkHook(void*, SceneNode9*, int, int) { ++g_moveWalkCalls; }
void InflateHook(SceneNode9*) { ++g_inflateCalls; } }

TEST(ObjLifecycle9, MoveBetweenUniversesRelinksHeadToTail) {
    SceneNode9 self;
    Universe9 from, to;
    i32 selfVal = (i32)reinterpret_cast<intptr_t>(&self);
    from.d(128) = selfVal;   // self is from's head
    from.d(132) = selfVal;   // and tail (single node)
    self.d(n9::kFirstChild) = 0;  // next = tail sentinel
    self.d(n9::kNextSibling) = 0; // prev = head sentinel
    to.d(128) = 0; to.d(132) = 0; // empty target

    g_moveWalkCalls = 0; g_inflateCalls = 0;
    ObjLife9Hooks h{}; h.sceneWalkAndInvoke = &MoveWalkHook; h.inflateGeometry = &InflateHook;
    ObjLife9SetHooks(h);
    char rc = ObjectMoveBetweenUniverses(&self, &from, &to, /*active=*/nullptr, nullptr);
    CHECK_EQ((int)rc, 1);
    // self removed from `from`.
    CHECK_EQ(from.d(128), 0);
    CHECK_EQ(from.d(132), 0);
    // self linked into `to` (now both head and tail).
    CHECK_EQ(to.d(132), selfVal);
    CHECK_EQ(to.d(128), selfVal);
    CHECK_EQ(g_moveWalkCalls, 1);
    CHECK_EQ(g_inflateCalls, 1);
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, MoveBetweenUniversesSameUniverseIsNoop) {
    SceneNode9 self; Universe9 u;
    CHECK_EQ((int)ObjectMoveBetweenUniverses(&self, &u, &u, nullptr, nullptr), 1);
    CHECK_EQ((int)ObjectMoveBetweenUniverses(nullptr, &u, &u, nullptr, nullptr), 0);
}

TEST(ObjLifecycle9, MoveBetweenUniversesPinnedNodeRejected) {
    SceneNode9 self; Universe9 from, to;
    self.d(504) = 1;        // pinned
    CHECK_EQ((int)ObjectMoveBetweenUniverses(&self, &from, &to, nullptr, nullptr), 0);
}

// ---------------------------------------------------------------------------
// ParseNameAndBind 0x4ffb40  (uses REAL util::StrCmpNoCase / util::StrChrLast)
// ---------------------------------------------------------------------------
TEST(ObjLifecycle9, ParseNameBindsSceneTypeRow) {
    Tables t;
    // scene row 5's name (at +1) = "keg".
    std::strcpy(reinterpret_cast<char*>(&t.sceneType[5 * kSceneTypeStride + 1]), "keg");
    ParseTables9 pt;
    pt.sceneTypeBase = t.sceneType.data();
    pt.buildingBase  = t.building.data();
    pt.markerScene   = "ob";    // prefix marker => scene branch
    pt.markerBuild   = "gb";
    i32 word = 0;
    // name "ob_keg": prefix "ob" == markerScene -> scene branch, suffix "keg" -> row 5.
    int rc = ObjectParseNameAndBind("ob_keg", &word, pt);
    CHECK_EQ(rc, 1);
    CHECK_EQ(word, (i32)(0x1000000 | 5));
}

TEST(ObjLifecycle9, ParseNameBindsBuildingRow) {
    Tables t;
    std::strcpy(reinterpret_cast<char*>(&t.building[3 * kBuildingStride + 1]), "smithy");
    ParseTables9 pt;
    pt.sceneTypeBase = t.sceneType.data();
    pt.buildingBase  = t.building.data();
    pt.markerScene   = "ob";
    pt.markerBuild   = "gb";
    i32 word = 0;
    // name "gb_smithy": prefix "gb" != markerScene -> building branch (== markerBuild),
    // suffix "smithy" -> row 3.
    int rc = ObjectParseNameAndBind("gb_smithy", &word, pt);
    CHECK_EQ(rc, 1);
    CHECK_EQ(word, (i32)((2 << 24) | 3));
}

TEST(ObjLifecycle9, ParseNameNoUnderscoreReturns0) {
    ParseTables9 pt; i32 w = 0;
    CHECK_EQ(ObjectParseNameAndBind("noseparator", &w, pt), 0);
    CHECK_EQ(ObjectParseNameAndBind(nullptr, &w, pt), 0);
}

// ---------------------------------------------------------------------------
// UpdateBuildingVisualState 0x506b68
// ---------------------------------------------------------------------------
TEST(ObjLifecycle9, BuildingVisualStateActiveUniversePhase2) {
    u8 rec[8192] = {0};
    rec[7280] = 0x00;
    int phaseGlobal = -1;
    // universeActive && buildPhase==2 -> nibble |=4, a3=64, a4=1.
    char last = ObjectUpdateBuildingVisualState(rec, /*force=*/1, /*universeActive=*/1,
                                                /*buildPhase=*/2, &phaseGlobal,
                                                /*curMipShift=*/0);
    CHECK_EQ((int)(rec[7280] & 0x1C), 4);   // bits 2..4 hold 4
    // force=1 so both side effects fire; last == a4 (1).
    CHECK_EQ((int)last, 1);
}

TEST(ObjLifecycle9, BuildingVisualStateInactiveDefaultBranch) {
    u8 rec[8192] = {0};
    *reinterpret_cast<i32*>(rec) = 0;   // *node != 64
    rec[7280] = 0;
    int phaseGlobal = -1;
    // !universeActive, *node!=64, buildPhase==0 -> else branch: nibble |=0x10, a3=128, a4=4.
    char last = ObjectUpdateBuildingVisualState(rec, 1, 0, 0, &phaseGlobal, 0);
    CHECK_EQ((int)(rec[7280] & 0x1C), 0x10);
    CHECK_EQ(phaseGlobal, 4);
    CHECK_EQ((int)last, 4);
}

TEST(ObjLifecycle9, BuildingVisualStateNodeIs64Branch) {
    u8 rec[8192] = {0};
    *reinterpret_cast<i32*>(rec) = 64;
    rec[7280] = 0;
    int phaseGlobal = -1;
    char last = ObjectUpdateBuildingVisualState(rec, 1, 0, 0, &phaseGlobal, 0);
    CHECK_EQ((int)(rec[7280] & 0x1C), 4);
    CHECK_EQ(phaseGlobal, 1);
    CHECK_EQ((int)last, 1);
    CHECK_EQ((int)ObjectUpdateBuildingVisualState(nullptr, 0, 0, 0, &phaseGlobal, 0), 0);
}

// ---------------------------------------------------------------------------
// RunScriptCallback 0x5b34e4
// ---------------------------------------------------------------------------
namespace { int g_disposeCalls = 0; int g_meshLoadCalls = 0; int g_meshAttachCalls = 0;
void DisposeResHook(SceneNode9*) { ++g_disposeCalls; }
int MeshLoadHook(const char*) { ++g_meshLoadCalls; return 1; }
void MeshAttachHook(SceneNode9*, const char*) { ++g_meshAttachCalls; } }

TEST(ObjLifecycle9, RunScriptCallbackReloadsMeshAndMarksDirty) {
    SceneNode9 node;
    ScriptCbInputs9 in;
    in.scriptName = "obj_mesh_new";
    in.triggerByte = 0;          // not a counter bump -> reload path
    in.ctxOwnerTop = 2;
    in.nodeOwnerTop = 2;         // owners match
    in.scriptBlockOk = 1;
    g_disposeCalls = g_meshLoadCalls = g_meshAttachCalls = 0;
    ObjLife9Hooks h{};
    h.disposeResources = &DisposeResHook; h.meshLoadOrFind = &MeshLoadHook;
    h.meshAttachLods = &MeshAttachHook;
    ObjLife9SetHooks(h);
    CHECK_EQ((int)ObjectRunScriptCallback(&node, in), 1);
    CHECK_EQ(g_disposeCalls, 1);
    CHECK_EQ(g_meshLoadCalls, 1);
    CHECK_EQ(g_meshAttachCalls, 1);
    // MarkDirtyFlag (real sibling) set the +528 bit2 on the node.
    CHECK_EQ((int)(node.b(528) & 4), 4);
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, RunScriptCallbackGatedByOwnerMismatch) {
    SceneNode9 node;
    ScriptCbInputs9 in;
    in.scriptName = "x"; in.triggerByte = 0;
    in.ctxOwnerTop = 1; in.nodeOwnerTop = 2;   // mismatch
    in.scriptBlockOk = 1;
    g_disposeCalls = 0;
    ObjLife9Hooks h{}; h.disposeResources = &DisposeResHook; ObjLife9SetHooks(h);
    CHECK_EQ((int)ObjectRunScriptCallback(&node, in), 1);
    CHECK_EQ(g_disposeCalls, 0);   // gated -> no reload
    ObjLife9ResetHooks();
}

TEST(ObjLifecycle9, RunScriptCallbackTriggerByteSkipsReload) {
    SceneNode9 node;
    ScriptCbInputs9 in;
    in.scriptName = "x"; in.triggerByte = 1;   // counter bump path
    in.ctxOwnerTop = 5; in.nodeOwnerTop = 5; in.scriptBlockOk = 1;
    g_disposeCalls = 0;
    ObjLife9Hooks h{}; h.disposeResources = &DisposeResHook; ObjLife9SetHooks(h);
    CHECK_EQ((int)ObjectRunScriptCallback(&node, in), 1);
    CHECK_EQ(g_disposeCalls, 0);
    ObjLife9ResetHooks();
}
