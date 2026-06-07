#include "test.h"

// Integration: drive object_lifecycle10 against REAL reconstructed siblings, not
// mocks, exactly as the live binary wires them:
//
//   * FindByName (0x5b7cb0) walks the scene with the REAL VIBE_Object_MatchNameCallback
//     (object_lifecycle3 @0x5b7c48) — the genuine predicate the original passes to
//     WalkAndInvoke, REUSED (not re-translated) — whose name compare is the live
//     case-insensitive StrCmpNoCase. We drive a real FindByName walk whose installed
//     walk hook invokes that real predicate against the candidate nodes and assert
//     the case-insensitive match the live game performs.
//   * AllocDrawData (0x5b107c) seeds each submesh entry through the REAL
//     VIBE_Object_InitSubMeshEntry sibling (object_lifecycle4 @0x5b0e18). We forward
//     the initSubMeshEntry hook into it (casting the raw entry bytes to the sibling's
//     SubMeshEntry view) and assert the cross-module seed wrote the expected slots.
#include "sim/object_lifecycle10.h"
#include "sim/object_lifecycle3.h"   // REAL: ObjectMatchNameCallback, FindCtx, SceneNode3
#include "sim/object_lifecycle4.h"   // REAL sibling: ObjectInitSubMeshEntry (0x5b0e18)
#include "util/string_ops.h"         // REAL sibling: StrCmpNoCase (0x5cb8f0)

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
// Walk hook visiting a fixed list of candidate nodes, invoking the REAL predicate.
std::vector<SceneNode3*>* g_candidates = nullptr;
void WalkCandidates(void*, void*, void* cb, u16, void* resultSlot) {
    auto fn = reinterpret_cast<bool (*)(SceneNode3*, FindCtx*)>(cb);
    if (!g_candidates) return;
    for (SceneNode3* n : *g_candidates) {
        if (!fn(n, static_cast<FindCtx*>(resultSlot)))
            break;   // predicate returns false once the match is the current node
    }
}

// Forward AllocDrawData's per-entry init into the REAL object_lifecycle4 sibling.
void RealInitSubMeshEntry(u8* entryBytes, void* node) {
    guild::sim::ObjectInitSubMeshEntry(
        reinterpret_cast<SubMeshEntry*>(entryBytes), node);
}

std::vector<unsigned char>* g_arena = nullptr;
void* ArenaAlloc(unsigned size, const char*) {
    if (!g_arena) return nullptr;
    g_arena->assign(size, 0);
    return g_arena->data();
}
}  // namespace

// FindByName's case-insensitive match runs through the REAL util::StrCmpNoCase via
// the REAL MatchNameCallback predicate: a mixed-case query must still bind to a
// lower-case node name among several candidates.
TEST(ObjLifecycle10Itest, FindByNameUsesRealStrCmpNoCaseAcrossCandidates) {
    SceneNode3 a, b, c;
    std::strcpy(reinterpret_cast<char*>(a.raw), "torch");
    std::strcpy(reinterpret_cast<char*>(b.raw), "lantern");
    std::strcpy(reinterpret_cast<char*>(c.raw), "well");
    std::vector<SceneNode3*> cands{&a, &b, &c};
    g_candidates = &cands;

    ObjLife10Hooks h{};
    h.sceneWalkAndInvoke = &WalkCandidates;   // walk invokes REAL MatchNameCallback
    ObjLife10SetHooks(h);

    SceneNode3 root;
    // Mixed-case "LANTERN" must match lower-case node b via REAL StrCmpNoCase.
    SceneNode3* found = ObjectFindByName(&root, 0, "LANTERN", nullptr, nullptr);
    CHECK(found == &b);

    // A query with no candidate match returns null.
    SceneNode3* miss = ObjectFindByName(&root, 0, "windmill", nullptr, nullptr);
    CHECK(miss == nullptr);

    // Confirm the REAL util sibling independently behaves as the bind relies on.
    CHECK_EQ(guild::util::StrCmpNoCase("LANTERN", "lantern"), 0);
    CHECK(guild::util::StrCmpNoCase("torch", "lantern") != 0);

    g_candidates = nullptr;
    ObjLife10ResetHooks();
}

// AllocDrawData seeds each submesh entry through the REAL object_lifecycle4
// InitSubMeshEntry: the cross-module seed must write the parentDraw (node) slot and
// the 0xFF tag exactly as the sibling does.
TEST(ObjLifecycle10Itest, AllocDrawDataSeedsViaRealInitSubMeshEntry) {
    std::vector<unsigned char> arena;
    g_arena = &arena;

    ObjLife10Hooks h{};
    h.memAllocDebug = &ArenaAlloc;
    h.initSubMeshEntry = &RealInitSubMeshEntry;   // REAL object_lifecycle4 sibling
    ObjLife10SetHooks(h);

    SceneNode10 node;
    void* blk = ObjectAllocDrawData(&node);
    CHECK(blk != nullptr);
    if (blk) {
        unsigned char* draw = static_cast<unsigned char*>(blk);
        // The real sibling sets entry->tag (0x376) to 0xFF and parentDraw (+24) to
        // the node. Confirm the first of the 4 entries (base +244).
        SubMeshEntry* e0 = reinterpret_cast<SubMeshEntry*>(draw + 244);
        CHECK_EQ((int)e0->tag, 0xFF);
        CHECK(e0->parentDraw == &node);
        // Trailer defaults still applied by AllocDrawData itself.
        i32 scale; std::memcpy(&scale, draw + 2296, 4);
        CHECK_EQ(scale, kOneFloatBits);
    }

    g_arena = nullptr;
    ObjLife10ResetHooks();
}
