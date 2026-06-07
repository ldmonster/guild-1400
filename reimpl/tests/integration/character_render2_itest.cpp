#include "test.h"

// Integration: drive character_render2's UpdateSubMeshes/DrawSubMeshes against the
// REAL reconstructed comparator sibling util::StrCmpNoCase (gilde.exe 0x5cb8f0).
// UpdateSubMeshes already delegates its per-slot name test to util::StrCmpNoCase
// (the live wiring — character_render2.cpp #includes util/string_ops.h and calls
// guild::util::StrCmpNoCase directly). We feed the 3 sub-mesh slot names through a
// CharRender2Hooks.submeshName table and assert that the prune/dirty side effects
// fire on EXACTLY the case-insensitive matches the real comparator reports — i.e.
// the cross-module match decision and the loop's effect agree end to end.
#include "sim/character_render2.h"
#include "util/string_ops.h"     // REAL reconstructed sibling: util::StrCmpNoCase

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// A mesh "record" carrying its 3 slot names. submeshName returns slot[i] or null.
struct MeshRec { const char* slots[3]; };

const char* SubmeshNameHook(void* mesh, int slot) {
    auto* m = static_cast<MeshRec*>(mesh);
    if (!m || slot < 0 || slot >= 3) return nullptr;
    return m->slots[slot];   // may be null for an absent slot
}

// Effect counters: the renderer-side prune + dirty propagation per matched slot.
int g_pruneCalls;
int g_dirtyCalls;
void PruneHook(void* /*animBase*/) { ++g_pruneCalls; }
void DirtyHook(void* /*obj*/, int /*flag*/) { ++g_dirtyCalls; }

// Build a fully-populated hook table (we install a non-default table, so every
// slot the code under test calls must be non-null). Only the three exercised
// slots do anything; the rest are inert no-ops.
CharRender2Hooks MakeHooks() {
    CharRender2Hooks h{};
    h.inflateGeometry        = [](void*){};
    h.heightmapCreate        = [](void*)->void*{ return nullptr; };
    h.buildCollisionGrid     = [](void*){};
    h.switchUniverse         = [](int){};
    h.initLogAndInflate      = [](u8){};
    h.displayLogAndCleanup   = [](u8){};
    h.setObjectPosition      = [](void*, const float[3]){};
    h.setWorldTranslation    = [](void*, const float[3]){};
    h.buildLightCache        = [](void*){};
    h.pointThroughBoneChain  = [](const void*, const float[3], float[3]){};
    h.rotateVectorByHierarchy= [](const void*, const float[3], float[3]){};
    h.setVisible             = [](void*, int){};
    h.selectTextureSet       = [](void*, int)->int{ return 0; };
    h.attachToUniverseNode   = [](void*, const char*)->void*{ return nullptr; };
    h.applyParentTransform   = [](void*){};
    h.loadObjectAnimation    = [](void*, const char*, int){};
    h.walkScene              = [](void*, bool(*)(void*, const char*, void*), int, void*){};
    h.removeMeshFromTree     = [](void*){};
    h.detachAndRelease       = [](void*){};
    h.pruneExpiredAttachments= &PruneHook;
    h.propagateDirty         = &DirtyHook;
    h.lookupPerson           = [](u16)->const PersonRecord2*{ return nullptr; };
    h.runMeshWalk            = [](void*, int)->int{ return 0; };
    h.submeshName            = &SubmeshNameHook;
    return h;
}
} // namespace

// UpdateSubMeshes prunes EACH slot whose name matches `name` under the real
// case-insensitive comparator. Two slots match "BODY" (case-folded), one differs.
TEST(CharRender2Itest, UpdatePrunesRealCaseInsensitiveMatches) {
    // First, confirm the real sibling's verdict on the exact tokens.
    CHECK_EQ(util::StrCmpNoCase("body", "BODY"), 0);   // folds equal
    CHECK_EQ(util::StrCmpNoCase("BoDy", "BODY"), 0);
    CHECK(util::StrCmpNoCase("head", "BODY") != 0);    // differs

    CharRender2Hooks h = MakeHooks();
    SetCharRender2Hooks(&h);
    g_pruneCalls = g_dirtyCalls = 0;

    MeshRec mesh{ {"body", "head", "BoDy"} };
    int rc = UpdateSubMeshes(&mesh, "BODY");
    CHECK_EQ(rc, 1);
    // slots 0 and 2 fold-match "BODY"; slot 1 does not -> 2 prunes, 2 dirties.
    CHECK_EQ(g_pruneCalls, 2);
    CHECK_EQ(g_dirtyCalls, 2);

    SetCharRender2Hooks(nullptr);
}

// An absent slot (null name) is skipped before the comparator runs; a non-match
// slot is left alone. Only the single real fold-match prunes.
TEST(CharRender2Itest, UpdateSkipsNullAndNonMatchSlots) {
    CharRender2Hooks h = MakeHooks();
    SetCharRender2Hooks(&h);
    g_pruneCalls = g_dirtyCalls = 0;

    MeshRec mesh{ {nullptr, "TORSO", "torso"} };  // slot0 absent, slot2 folds to slot1's target
    int rc = UpdateSubMeshes(&mesh, "torso");
    CHECK_EQ(rc, 1);
    // slot0 null -> skip; slot1 "TORSO" folds == "torso" -> match; slot2 "torso" -> match.
    CHECK_EQ(util::StrCmpNoCase("TORSO", "torso"), 0);
    CHECK_EQ(g_pruneCalls, 2);
    CHECK_EQ(g_dirtyCalls, 2);

    SetCharRender2Hooks(nullptr);
}

// No slot matches -> the real comparator rejects every slot -> no side effects.
TEST(CharRender2Itest, UpdateNoMatchNoSideEffects) {
    CharRender2Hooks h = MakeHooks();
    SetCharRender2Hooks(&h);
    g_pruneCalls = g_dirtyCalls = 0;

    MeshRec mesh{ {"arm", "leg", "wing"} };
    UpdateSubMeshes(&mesh, "BODY");
    CHECK_EQ(g_pruneCalls, 0);
    CHECK_EQ(g_dirtyCalls, 0);

    SetCharRender2Hooks(nullptr);
}

// DrawSubMeshes prunes every PRESENT slot regardless of name (no comparator);
// contrast with UpdateSubMeshes to show the comparator is what gates the latter.
TEST(CharRender2Itest, DrawPrunesEveryPresentSlot) {
    CharRender2Hooks h = MakeHooks();
    SetCharRender2Hooks(&h);
    g_pruneCalls = g_dirtyCalls = 0;

    MeshRec mesh{ {"arm", nullptr, "wing"} };  // 2 present, 1 absent
    int rc = DrawSubMeshes(&mesh);
    CHECK_EQ(rc, 1);
    CHECK_EQ(g_pruneCalls, 2);   // both present slots pruned
    CHECK_EQ(g_dirtyCalls, 0);   // DrawSubMeshes never propagates dirty

    SetCharRender2Hooks(nullptr);
}
