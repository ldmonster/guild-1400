// anim_recon4_mesh_lru_test.cpp — golden tests for the recon4 anim/mesh cluster.
// Verifies LRU eviction order, bone-name matching, mesh memory-size arithmetic,
// animation flag/scale bit math, the OAM post-load transform, and strcmp.
//
// Headless: no third-party deps, no main() (test_main.cpp supplies it).

#include "render/anim_recon4_mesh_lru.h"
#include "test.h"

#include <cstring>
#include <cstdint>
#include <vector>

using namespace guild::render::anim_recon4;
using guild::u8;
using guild::u16;
using guild::u32;
using guild::i32;

// A mesh node is a flat byte buffer big enough for the highest offset we touch
// (sub-array region + a few sub-records). 4096 is ample.
static constexpr int kMeshSize = 4096;

namespace {

// A single byte-addressable buffer standing in for the 32-bit process address
// space. Records (mesh nodes, sub-arrays, etc.) live at fixed offsets within it;
// link fields store 4-byte offsets-from-base exactly as the binary stores 32-bit
// pointers. Offset 0 is reserved as NULL.
struct Mem {
    std::vector<u8> buf;
    Mem() : buf(1 << 16, 0) {}
    Arena arena() { Arena a; a.base = buf.data(); return a; }
    void* at(u32 off) { return buf.data() + off; }
};

// Single-buffer node used by the non-list tests (mesh == arena base).
struct Node {
    std::vector<u8> buf;
    Node() : buf(kMeshSize, 0) {}
    void*  p() { return buf.data(); }
};

// ---- free-order recording hook ----
static std::vector<u32> g_freed;
static void RecordFree(u32 ptr, void*) { g_freed.push_back(ptr); }

} // namespace

// ===========================================================================
// VIBE_Util_StrCmp (0x5d3f10)
// ===========================================================================
TEST(AnimRecon4, StrCmp) {
    CHECK_EQ(UtilStrCmp("bone_root", "bone_root"), 0);
    CHECK_EQ(UtilStrCmp("", ""), 0);
    CHECK_EQ(UtilStrCmp("a", "b"), -1);
    CHECK_EQ(UtilStrCmp("b", "a"), 1);
    CHECK_EQ(UtilStrCmp("abc", "abd"), -1);
    CHECK_EQ(UtilStrCmp("abc", "ab"), 1);     // longer > prefix
    CHECK_EQ(UtilStrCmp("ab", "abc"), -1);
    const char* s = "same";
    CHECK_EQ(UtilStrCmp(s, s), 0);            // pointer-equal fast path
}

// ===========================================================================
// VIBE_Anim_ComputeMeshMemorySize (0x5cfd24)
// ===========================================================================
TEST(AnimRecon4, ComputeMeshMemorySize) {
    Mem mem; Arena ar = mem.arena();
    // null mesh -> -1
    CHECK_EQ(ComputeMeshMemorySize(nullptr, ar), -1);

    const u32 meshOff = 256;
    void* m = mem.at(meshOff);
    // sub-array base = 0 -> -1
    wr_u32(m, mesh_off::SUBARRAY, 0);
    CHECK_EQ(ComputeMeshMemorySize(m, ar), -1);

    // Lay a sub-record at arena offset 4096.
    const u32 subOff = 4096;
    wr_u32(m, mesh_off::SUBARRAY, subOff);
    wr_i32(m, mesh_off::VERT_COUNT, 10);   // a1[80]
    wr_i32(m, mesh_off::SUB_COUNT, 2);     // a1[82]

    // No buffers present: v2 = 0 -> ret = 2*(0+192)+364 = 748
    CHECK_EQ(ComputeMeshMemorySize(m, ar), 2 * (0 + 192) + 364);

    // C buffer present: +12*vert. A: +3*vert. B: +3*vert.
    wr_u32(mem.at(subOff), mesh_off::SUB_BUF_C, 0xCAFE);  // +188
    wr_u32(mem.at(subOff), mesh_off::SUB_BUF_A, 0xBEEF);  // +180
    wr_u32(mem.at(subOff), mesh_off::SUB_BUF_B, 0xF00D);  // +184
    const i32 v2 = 12 * 10 + 3 * 10 + 3 * 10; // 180
    CHECK_EQ(ComputeMeshMemorySize(m, ar), 2 * (v2 + 192) + 364);
}

// ===========================================================================
// VIBE_Anim_EvictMeshesForBudget (0x5cfdb8) — LRU victim selection order.
// Build a real intrusive list: head -> A -> B -> C -> sentinel.
// LRU keys A=300, B=100, C=200. With a large seed, the smallest-key non-busy
// node (B, then C, then A) is evicted in order until usage <= budget.
// ===========================================================================
TEST(AnimRecon4, EvictLruOrder) {
    g_freed.clear();
    Mem mem; Arena ar = mem.arena();

    // Arena offsets for the four records (non-zero, well-separated).
    const u32 SENT = 0x100, A = 0x2000, B = 0x4000, C = 0x6000;

    auto link = [&](u32 node, u32 next, u32 prev, u32 key, u32 refcount,
                    u32 subOff, i32 verts, i32 subc) {
        void* p = mem.at(node);
        wr_u32(p, mesh_off::LIST_NEXT, next);
        wr_u32(p, mesh_off::LIST_PREV, prev);
        wr_u32(p, mesh_off::LRU_KEY, key);
        wr_u32(p, mesh_off::REFCOUNT, refcount);
        wr_u32(p, mesh_off::SUBARRAY, subOff);
        wr_i32(p, mesh_off::VERT_COUNT, verts);
        wr_i32(p, mesh_off::SUB_COUNT, subc);
    };
    // size per node with verts=0, subc=1 -> 1*(0+192)+364 = 556
    // Shared sub-array record at offset 0x8000 (no buffers -> size base only).
    link(A, SENT, B, 300, 0, 0x8000, 0, 1);
    link(B, A,    C, 100, 0, 0x8000, 0, 1);
    link(C, B, SENT, 200, 0, 0x8000, 0, 1);
    // Traversal head..sentinel from C: C,B,A.

    MeshAnimHooks hooks;
    hooks.freeDebug = &RecordFree;

    // usage = 3*556 = 1668, budget = 600 -> evict until <= 600.
    // smallest-key non-busy victim first: B(100) then C(200).
    char ok = EvictMeshesForBudget(/*budget=*/600, /*usage=*/1668,
                                   /*head=*/C, /*sentinel=*/SENT,
                                   /*lruSeed=*/0xFFFFFFFFu, ar, hooks);
    CHECK_EQ(static_cast<int>(ok), 1);
    // B (min key) and then C (next-min) are the victims, freed in that order;
    // A (largest key) survives. The freeDebug hook records each freed node.
    auto freedNode = [](u32 off) {
        for (u32 v : g_freed) if (v == off) return true;
        return false;
    };
    CHECK(freedNode(B));
    CHECK(freedNode(C));
    CHECK(!freedNode(A));
    // B was the first victim chosen (min key), so it is freed before C.
    u32 firstNodeFreed = 0;
    for (u32 v : g_freed) { if (v == B || v == C) { firstNodeFreed = v; break; } }
    CHECK_EQ(firstNodeFreed, B);
}

// Direct victim-selection check: with seed=max, the min-key non-busy node wins;
// busy nodes (refcount != 0) are skipped.
TEST(AnimRecon4, EvictSkipsBusyAndPicksMinKey) {
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100, A = 0x2000, B = 0x4000, C = 0x6000;
    auto setup = [&](u32 node, u32 next, u32 prev, u32 key, u32 ref) {
        void* p = mem.at(node);
        wr_u32(p, mesh_off::LIST_NEXT, next);
        wr_u32(p, mesh_off::LIST_PREV, prev);
        wr_u32(p, mesh_off::LRU_KEY, key);
        wr_u32(p, mesh_off::REFCOUNT, ref);
        wr_u32(p, mesh_off::SUBARRAY, 0x8000);
        wr_i32(p, mesh_off::VERT_COUNT, 0);
        wr_i32(p, mesh_off::SUB_COUNT, 1);
    };
    // B has smallest key (50) but is BUSY (ref=2) -> skipped.
    // Among non-busy A(300) and C(200), C (200) wins.
    setup(A, SENT, B, 300, 0);
    setup(B, A,    C, 50,  2);
    setup(C, B, SENT, 200, 0);

    MeshAnimHooks hooks; hooks.freeDebug = &RecordFree;
    g_freed.clear();

    // usage = 556 (just over), budget 555 -> evict one node (C, min non-busy).
    char ok = EvictMeshesForBudget(555, 556, C, SENT, 0xFFFFFFFFu, ar, hooks);
    CHECK_EQ(static_cast<int>(ok), 1);
    // C (min non-busy key) is the victim and gets freed; busy B is never freed.
    bool freedC = false, freedB = false;
    for (u32 v : g_freed) { if (v == C) freedC = true; if (v == B) freedB = true; }
    CHECK(freedC);
    CHECK(!freedB);
    // Busy B's refcount untouched.
    CHECK_EQ(rd_u32(mem.at(B), mesh_off::REFCOUNT), 2u);
}

// No eligible victim -> returns 0 (failure). All nodes busy.
TEST(AnimRecon4, EvictNoVictimFails) {
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100, A = 0x2000;
    void* pa = mem.at(A);
    wr_u32(pa, mesh_off::LIST_NEXT, SENT);
    wr_u32(pa, mesh_off::LIST_PREV, SENT);
    wr_u32(pa, mesh_off::LRU_KEY, 100);
    wr_u32(pa, mesh_off::REFCOUNT, 1); // busy
    MeshAnimHooks hooks;
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(0, 1000, A, SENT, 0xFFFFFFFFu, ar, hooks)), 0);
    // budget < 0 -> immediate 0
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(-1, 1000, A, SENT, 0xFFFFFFFFu, ar, hooks)), 0);
    // usage already <= budget -> 1 with no scan.
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(2000, 1000, A, SENT, 0xFFFFFFFFu, ar, hooks)), 1);
}

// ===========================================================================
// VIBE_Anim_ReleaseMeshData (0x5cfe30) — refcount + unlink + free order.
// ===========================================================================
TEST(AnimRecon4, ReleaseRefcountDecrementOnly) {
    Mem mem; Arena ar = mem.arena();
    const u32 M = 0x2000, SENT = 0x100;
    wr_i32(mem.at(M), mesh_off::REFCOUNT, 3);
    MeshAnimHooks hooks; hooks.freeDebug = &RecordFree;
    g_freed.clear();
    // unlink=true but refcount 3 -> 2 (>0) -> no free.
    ReleaseMeshData(M, true, M, SENT, 0x400000, ar, hooks);
    CHECK_EQ(rd_i32(mem.at(M), mesh_off::REFCOUNT), 2);
    CHECK_EQ(g_freed.size(), static_cast<size_t>(0));
}

TEST(AnimRecon4, ReleaseUnlinkAndFree) {
    g_freed.clear();
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100, PREV = 0x2000, NODE = 0x4000, NEXT = 0x6000;
    const u32 SUB = 0x8000;
    // List: prev <-> node <-> next.
    wr_u32(mem.at(NODE), mesh_off::LIST_NEXT, NEXT);
    wr_u32(mem.at(NODE), mesh_off::LIST_PREV, PREV);
    wr_u32(mem.at(PREV), mesh_off::LIST_NEXT, NODE);
    wr_u32(mem.at(NEXT), mesh_off::LIST_PREV, NODE);

    wr_i32(mem.at(NODE), mesh_off::REFCOUNT, 1);  // -> 0 -> free path
    wr_u32(mem.at(NODE), mesh_off::SUBARRAY, SUB);
    wr_i32(mem.at(NODE), mesh_off::SUB_COUNT, 1);
    // one sub-record at SUB with all three buffers present
    wr_u32(mem.at(SUB), mesh_off::SUB_BUF_C, 0x1111); // freed 1st (C, +188)
    wr_u32(mem.at(SUB), mesh_off::SUB_BUF_A, 0x2222); // freed 2nd (A, +180)
    wr_u32(mem.at(SUB), mesh_off::SUB_BUF_B, 0x3333); // freed 3rd (B, +184)

    MeshAnimHooks hooks; hooks.freeDebug = &RecordFree;
    ReleaseMeshData(NODE, true, NODE, SENT, 0x400000, ar, hooks);

    // Unlink: prev.next -> next, next.prev -> prev.
    CHECK_EQ(rd_u32(mem.at(PREV), mesh_off::LIST_NEXT), NEXT);
    CHECK_EQ(rd_u32(mem.at(NEXT), mesh_off::LIST_PREV), PREV);

    // Free order per decompile: C(+188), A(+180), B(+184), sub-array, node.
    CHECK_EQ(g_freed.size(), static_cast<size_t>(5));
    CHECK_EQ(g_freed[0], 0x1111u);
    CHECK_EQ(g_freed[1], 0x2222u);
    CHECK_EQ(g_freed[2], 0x3333u);
    CHECK_EQ(g_freed[3], SUB);
    CHECK_EQ(g_freed[4], NODE);

    // Buffers + subarray zeroed.
    CHECK_EQ(rd_u32(mem.at(SUB), mesh_off::SUB_BUF_C), 0u);
    CHECK_EQ(rd_u32(mem.at(NODE), mesh_off::SUBARRAY), 0u);
}

// ===========================================================================
// VIBE_Anim_AssignSubMeshBones (0x5cbfc0) — bone-name matching.
// Layout (all offsets relative to mesh base, which is also rec base here):
//   rec = mesh+492 -> we set it to point "into" the same buffer at recOff.
// ===========================================================================
TEST(AnimRecon4, AssignSubMeshBones) {
    Node m;
    // Put the rec at offset 0 conceptually: set *(mesh+492) = recBase.
    const i32 recBase = 600;
    wr_u32(m.p(), 492, static_cast<u32>(recBase));

    // slotBase = recBase + 272 ; flagBase = recBase + 244.
    const i32 slotBase = recBase + 272;
    const i32 flagBase = recBase + 244;

    // Slot 0: flag set. Its anim source at srcOff, with 2 bones "hip","knee".
    wr_u32(m.p(), flagBase + 132, 1); // *(v11+132) nonzero -> process slot 0
    const i32 srcOff = 1500;
    wr_u32(m.p(), slotBase + 104, static_cast<u32>(srcOff)); // v14 = src
    wr_i32(m.p(), srcOff + 324, 2);   // bone count
    std::memcpy(m.buf.data() + srcOff + 64 + 64 * 0, "hip", 4);
    std::memcpy(m.buf.data() + srcOff + 64 + 64 * 1, "knee", 5);

    // Child list: head at mesh+508. Two children: child0 name "knee", child1 "hip".
    const i32 child0 = 2000, child1 = 2200;
    wr_u32(m.p(), 508, static_cast<u32>(child0));        // head
    wr_u32(m.p(), child0 + 496, static_cast<u32>(child1)); // child0.next
    wr_u32(m.p(), child1 + 496, 0);                       // child1.next = end
    // child name lives at *(child+492)+180.
    const i32 c0rec = 2400, c1rec = 2600;
    wr_u32(m.p(), child0 + 492, static_cast<u32>(c0rec));
    wr_u32(m.p(), child1 + 492, static_cast<u32>(c1rec));
    std::memcpy(m.buf.data() + c0rec + 180, "knee", 5);  // child0 = knee
    std::memcpy(m.buf.data() + c1rec + 180, "hip", 4);   // child1 = hip

    // Slots 1 & 2: flag clear -> skipped.
    wr_u32(m.p(), flagBase + 116 + 132, 0);
    wr_u32(m.p(), flagBase + 116 * 2 + 132, 0);

    MeshAnimHooks hooks;
    char r = AssignSubMeshBones(m.p(), hooks);
    CHECK_EQ(static_cast<int>(r), 0);

    // Bone index 0 = "hip" matches child1 -> stored. Bone index 1 = "knee"
    // matches child0 -> stored. Slot's bone-index bytes are written in the order
    // bones are visited; for each bone the child list is scanned and matches
    // append into the next free slot.
    //   bone0 "hip": child0(knee) no, child1(hip) yes -> slot[112+0]=0
    //   bone1 "knee": child0(knee) yes -> slot[112+1]=1, child1(hip) no
    CHECK_EQ(rd_u8(m.p(), slotBase + 112 + 0), static_cast<u8>(0));
    CHECK_EQ(rd_u8(m.p(), slotBase + 112 + 1), static_cast<u8>(1));
    // Remaining two slots stay 0xFF.
    CHECK_EQ(rd_u8(m.p(), slotBase + 112 + 2), static_cast<u8>(0xFF));
    CHECK_EQ(rd_u8(m.p(), slotBase + 112 + 3), static_cast<u8>(0xFF));

    // null mesh / null rec -> 0, no crash.
    CHECK_EQ(static_cast<int>(AssignSubMeshBones(nullptr, hooks)), 0);
}

// ===========================================================================
// VIBE_Mesh_FreeAttachedBuffers (0x5b2ef8)
// ===========================================================================
TEST(AnimRecon4, FreeAttachedBuffers) {
    g_freed.clear();
    Node m;
    const i32 recBase = 600;
    wr_u32(m.p(), 492, static_cast<u32>(recBase));

    // Sub-record 0 (off 0): count>0, free both buffers.
    wr_i32(m.p(), recBase + 0 + 252, 5);
    wr_u32(m.p(), recBase + 0 + 244, 0xAAAA);
    wr_u32(m.p(), recBase + 0 + 248, 0xBBBB);
    wr_u8 (m.p(), recBase + 0 + 620, 0xFF);  // == 0xFF
    wr_u8 (m.p(), recBase + 0 + 622, 0);     // bit0 clear -> ChangeTransparency skipped

    // Sub-record 1 (off 384): count == 0 -> skipped entirely.
    wr_i32(m.p(), recBase + 384 + 252, 0);

    // Sub-records 2,3: count 0.
    wr_i32(m.p(), recBase + 768 + 252, 0);
    wr_i32(m.p(), recBase + 1152 + 252, 0);

    int transparencyCalls = 0;
    MeshAnimHooks hooks;
    hooks.freeDebug = &RecordFree;
    hooks.changeTransparency =
        [](void*, u32, int, void*) {}; // placeholder, but count via static below
    static int s_tcalls = 0; s_tcalls = 0;
    hooks.changeTransparency = [](void*, u32, int, void*) { ++s_tcalls; };

    char r = FreeAttachedBuffers(m.p(), hooks);
    CHECK_EQ(static_cast<int>(r), 0); // inert scene-graph walk default

    // 620==0xFF and 622&1==0 -> ChangeTransparency NOT called.
    CHECK_EQ(s_tcalls, 0);
    // Two buffers freed (244 then 248).
    CHECK_EQ(g_freed.size(), static_cast<size_t>(2));
    CHECK_EQ(g_freed[0], 0xAAAAu);
    CHECK_EQ(g_freed[1], 0xBBBBu);
    // Zeroed fields.
    CHECK_EQ(rd_u32(m.p(), recBase + 0 + 244), 0u);
    CHECK_EQ(rd_u32(m.p(), recBase + 0 + 248), 0u);
    CHECK_EQ(rd_u32(m.p(), recBase + 0 + 252), 0u);
    CHECK_EQ(rd_u32(m.p(), 460), 0u);
    (void)transparencyCalls;
}

TEST(AnimRecon4, FreeAttachedBuffersCallsTransparency) {
    Node m;
    const i32 recBase = 600;
    wr_u32(m.p(), 492, static_cast<u32>(recBase));
    wr_i32(m.p(), recBase + 252, 3);
    wr_u8 (m.p(), recBase + 620, 0x10); // != 0xFF -> ChangeTransparency called
    static int s_calls = 0; s_calls = 0;
    MeshAnimHooks hooks;
    hooks.changeTransparency = [](void*, u32, int v, void*) {
        ++s_calls;
    };
    FreeAttachedBuffers(m.p(), hooks);
    CHECK_EQ(s_calls, 1);
}

// ===========================================================================
// VIBE_Anim_FreeObjAnimDataAndReset (0x43fc38)
// ===========================================================================
TEST(AnimRecon4, FreeObjAnimDataAndReset) {
    FreeAnimResetState st;            // both globals start as sentinels
    static int s_freeCalls = 0; s_freeCalls = 0;
    static i32 s_a1 = 0, s_a2 = 0;
    FreeAnimResetHooks hooks;
    hooks.g_13FCD1C = 0x777;
    hooks.freeObjAnimData = [](u32 g, i32 a1, i32 a2, void*) {
        ++s_freeCalls; s_a1 = a1; s_a2 = a2; (void)g;
    };
    i32 ret = FreeObjAnimDataAndReset(11, 22, st, hooks);
    CHECK_EQ(ret, 0);
    CHECK_EQ(s_freeCalls, 1);
    CHECK_EQ(s_a1, 11);
    CHECK_EQ(s_a2, 22);
    CHECK_EQ(st.g_62D4E8, 0u);
    CHECK_EQ(st.g_62D4E4, 0u);
}

// ===========================================================================
// VIBE_AnimationFlags_Compute (0x41dc74)
// ===========================================================================
TEST(AnimRecon4, AnimationFlagsCompute) {
    std::vector<u8> table(740 * 6, 0);  // room for handles 0..5
    // State record returned by stateUpdate hook.
    static std::vector<u8> stRec;
    stRec.assign(512, 0);

    auto stHook = [](u32 /*h*/, void*) -> void* {
        return stRec.data();
    };
    MeshAnimHooks hooks; hooks.stateUpdate = stHook;

    // handle -1 -> 0
    CHECK_EQ(AnimationFlagsCompute(-1, table.data(), hooks).ret, 0);

    // handle 1, type 99 (invalid) -> 0
    wr_u8(table.data(), 740 * 1 + 24, 99);
    CHECK_EQ(AnimationFlagsCompute(1, table.data(), hooks).ret, 0);

    // handle 0, type 1 -> reads st+44 / st+46.
    wr_u8(table.data(), 0 + 24, 1);
    wr_u16(stRec.data(), 44, 0x0102);
    wr_u16(stRec.data(), 46, 0x0304);
    AnimFlagsResult r1 = AnimationFlagsCompute(0, table.data(), hooks);
    CHECK_EQ(r1.ret, 1);
    CHECK_EQ(r1.outW, static_cast<u16>(0x0102));
    CHECK_EQ(r1.outH, static_cast<u16>(0x0304));

    // handle 2, type 8 -> kf path. rec[+116] = sel, kfOff = *(st + 4*sel + 69).
    wr_u8(table.data(), 740 * 2 + 24, 8);
    const i32 sel = 3;
    wr_i32(table.data(), 740 * 2 + 116, sel);
    const u32 kfOff = 200;
    wr_u32(stRec.data(), 4 * sel + 69, kfOff);  // keyframe offset
    wr_u16(stRec.data(), kfOff + 6, 0x1111);
    wr_u16(stRec.data(), kfOff + 10, 0x2222);
    AnimFlagsResult r2 = AnimationFlagsCompute(2, table.data(), hooks);
    CHECK_EQ(r2.ret, 1);
    CHECK_EQ(r2.outW, static_cast<u16>(0x1111));
    CHECK_EQ(r2.outH, static_cast<u16>(0x2222));

    // type 5 with kfOff == 0 -> ret 1 but outputs untouched (default 0).
    wr_u8(table.data(), 740 * 3 + 24, 5);
    wr_i32(table.data(), 740 * 3 + 116, 0);
    wr_u32(stRec.data(), 4 * 0 + 69, 0);  // kfOff 0
    AnimFlagsResult r3 = AnimationFlagsCompute(3, table.data(), hooks);
    CHECK_EQ(r3.ret, 1);
    CHECK_EQ(r3.outW, static_cast<u16>(0));

    // stateUpdate returns null -> ret 0.
    MeshAnimHooks nullHooks;
    nullHooks.stateUpdate = [](u32, void*) -> void* { return nullptr; };
    wr_u8(table.data(), 740 * 4 + 24, 1);
    CHECK_EQ(AnimationFlagsCompute(4, table.data(), nullHooks).ret, 0);
}

// ===========================================================================
// VIBE_Mesh_LoadObjectAnimation post-load transform (0x5d367c, logic only)
// ===========================================================================
TEST(AnimRecon4, LoadObjectAnimationTransform) {
    std::vector<u8> oam(2048, 0);
    std::vector<u8> obj(256, 0);

    const i32 frameCount = 3;
    const i32 arr = 512; // keyframe array offset inside the oam buffer
    wr_i32(oam.data(), oam_off::FRAME_COUNT, frameCount);
    wr_u32(oam.data(), oam_off::ARR_PTR, static_cast<u32>(arr));

    // Per-keyframe count fields (stride 88, field at +0).
    wr_u32(oam.data(), arr + 88 * 0, 4);
    wr_u32(oam.data(), arr + 88 * 1, 7);   // becomes the source for last fixup
    wr_u32(oam.data(), arr + 88 * 2, 99);  // overwritten by [1] then *3

    // Owning-object transform source dwords.
    wr_u32(obj.data(), oam_off::OBJ_X0, 0x10);
    wr_u32(obj.data(), oam_off::OBJ_X1, 0x11);
    wr_u32(obj.data(), oam_off::OBJ_X2, 0x12);
    wr_u32(obj.data(), oam_off::OBJ_X3, 0x13);
    wr_u32(obj.data(), oam_off::OBJ_X4, 0x14);
    wr_u32(obj.data(), oam_off::OBJ_X5, 0x15);

    // flag bytes: set bit5 of +45 (should be cleared), and leave +46 with bit1 set.
    wr_u8(oam.data(), oam_off::FLAG45, 0x20); // bit5 -> cleared; bit1 stays 0
    wr_u8(oam.data(), oam_off::FLAG46, 0x02); // bit1 set -> cleared by &0xFD

    ObjAnimHooks hooks; // no advanceFrameIndex -> passthrough

    LoadObjectAnimation_ApplyTransform(oam.data(), obj.data(), /*loopFlag=*/0,
                                       hooks);

    // Last keyframe count copied from previous (7), then all *3.
    // After fixup: arr[2]=7. After *3: arr[0]=12, arr[1]=21, arr[2]=21.
    CHECK_EQ(rd_u32(oam.data(), arr + 88 * 0), 12u);
    CHECK_EQ(rd_u32(oam.data(), arr + 88 * 1), 21u);
    CHECK_EQ(rd_u32(oam.data(), arr + 88 * 2), 21u);

    // Transform dwords copied.
    CHECK_EQ(rd_u32(oam.data(), oam_off::XFORM0), 0x10u);
    CHECK_EQ(rd_u32(oam.data(), oam_off::XFORM5), 0x15u);

    // Flags: +45 bit5 cleared (0x20 -> 0). +46: (0x02 & 0xFD)=0, loopFlag 0 -> 0.
    CHECK_EQ(rd_u8(oam.data(), oam_off::FLAG45), static_cast<u8>(0x00));
    CHECK_EQ(rd_u8(oam.data(), oam_off::FLAG46), static_cast<u8>(0x00));

    // FLAG45 bit1 (2) is clear -> else branch: idx3=0, idx2=1, idx1=0.
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX3), 0);
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX2), 1);
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX1), 0);
}

TEST(AnimRecon4, LoadObjectAnimationLoopBranch) {
    std::vector<u8> oam(2048, 0);
    std::vector<u8> obj(256, 0);
    const i32 frameCount = 2;
    const i32 arr = 512;
    wr_i32(oam.data(), oam_off::FRAME_COUNT, frameCount);
    wr_u32(oam.data(), oam_off::ARR_PTR, static_cast<u32>(arr));
    wr_u32(oam.data(), arr + 88 * 0, 5);
    wr_u32(oam.data(), arr + 88 * 1, 9);

    // Pre-set FLAG45 bit1 so the loop branch is taken; loopFlag drives +46.
    // The transform clears bit5 of +45 (keep bit1). Then checks (+45 & 2).
    wr_u8(oam.data(), oam_off::FLAG45, 0x02); // bit1 set, survives &~0x20

    static int s_advCalls = 0; s_advCalls = 0;
    static i32 s_lastFrame = -1, s_frameCount = -1;
    ObjAnimHooks hooks;
    hooks.advanceFrameIndex = [](u8, i32, i32 lastFrame, i32, i32 fc, void*) -> i32 {
        ++s_advCalls; s_lastFrame = lastFrame; s_frameCount = fc;
        return 42;
    };

    LoadObjectAnimation_ApplyTransform(oam.data(), obj.data(), /*loopFlag=*/1,
                                       hooks);

    // last = frameCount-1 = 1.  arr[1] after fixup = arr[0]=5, then *3 -> 15.
    // idx1 = 1. idx3 = arr[last]-1 = 15-1 = 14. idx2 = advanceFrameIndex = 42.
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX1), 1);
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX3), 14);
    CHECK_EQ(rd_i32(oam.data(), oam_off::IDX2), 42);
    CHECK_EQ(s_advCalls, 1);
    CHECK_EQ(s_lastFrame, frameCount - 1);
    CHECK_EQ(s_frameCount, frameCount);
}

// ===========================================================================
// VIBE_Shape_LoadAndRegister registration logic (0x41f5e0, table reg only)
// ===========================================================================
namespace {
// A bounds record: u16 at +6 = width, u16 at +10 = height.
static std::vector<u8> g_bounds0;
static std::vector<u8> g_boundsK;
}

TEST(AnimRecon4, ShapeRegisterSingleType1) {
    std::vector<u8> table(84 * 8, 0);
    std::vector<u8> blob(256, 0);

    g_bounds0.assign(32, 0);
    wr_u16(g_bounds0.data(), 6, 0x00AA);   // width
    wr_u16(g_bounds0.data(), 10, 0x00BB);  // height

    ShapeRegHooks hooks;
    hooks.classifyType = [](const void*, void*) -> u8 { return 1; };
    hooks.coordTransform = [](const void*, u16 idx, void*) -> const void* {
        return idx == 0 ? g_bounds0.data() : nullptr;
    };

    // Pre-seed flags byte to verify (|1)&0xFD math: start 0x00 -> 0x01.
    const i32 start = 2;
    // Wide name "AB" -> bytes A,?,B,?,0
    char name[6] = { 'A', 1, 'B', 2, 0, 0 };

    i32 count = Shape_RegisterLoaded(table.data(), start, blob.data(),
                                     /*blobSize=*/4321, name, hooks);
    // gilde.exe 0x41f5e0: dword_62D208 = v38 stays at startIndex for a
    // single-frame shape (old pin start+1 predates the exact tail store).
    CHECK_EQ(count, start);

    const i32 base = 84 * start;
    CHECK_EQ(rd_u32(table.data(), base + shape_off::TYPE), 1u);
    CHECK_EQ(rd_u16(table.data(), base + shape_off::BOUND_W), static_cast<u16>(0x00AA));
    CHECK_EQ(rd_u16(table.data(), base + shape_off::BOUND_H), static_cast<u16>(0x00BB));
    CHECK_EQ(rd_u32(table.data(), base + shape_off::PARENT), static_cast<u32>(start));
    CHECK_EQ(rd_u32(table.data(), base + shape_off::SIZE), 4321u);
    // Name copied (wide, stride 2): bytes A,1,B,2,0
    CHECK_EQ(rd_u8(table.data(), base + 0), static_cast<u8>('A'));
    CHECK_EQ(rd_u8(table.data(), base + 1), static_cast<u8>(1));
    CHECK_EQ(rd_u8(table.data(), base + 2), static_cast<u8>('B'));
    CHECK_EQ(rd_u8(table.data(), base + 3), static_cast<u8>(2));
    CHECK_EQ(rd_u8(table.data(), base + 4), static_cast<u8>(0));
    // Flags: (0|1)&0xFD = 1.
    CHECK_EQ(rd_u8(table.data(), base + shape_off::FLAGS), static_cast<u8>(1));
}

TEST(AnimRecon4, ShapeRegisterType17) {
    std::vector<u8> table(84 * 4, 0);
    std::vector<u8> blob(64, 0);
    wr_u16(blob.data(), 12, 0x1234);  // type-17 width source
    wr_u16(blob.data(), 14, 0x5678);  // height source

    ShapeRegHooks hooks;
    hooks.classifyType = [](const void*, void*) -> u8 { return 17; };
    // coordTransform should NOT be used for type 17.
    hooks.coordTransform = [](const void*, u16, void*) -> const void* {
        return nullptr;
    };

    i32 count = Shape_RegisterLoaded(table.data(), 0, blob.data(), 100,
                                     nullptr, hooks);
    CHECK_EQ(count, 0);   // v38 unchanged for a single-frame (type 17) shape
    CHECK_EQ(rd_u32(table.data(), shape_off::TYPE), 17u);
    CHECK_EQ(rd_u16(table.data(), shape_off::BOUND_W), static_cast<u16>(0x1234));
    CHECK_EQ(rd_u16(table.data(), shape_off::BOUND_H), static_cast<u16>(0x5678));
}

TEST(AnimRecon4, ShapeRegisterMultiFrameType5) {
    std::vector<u8> table(84 * 16, 0);
    std::vector<u8> blob(256, 0);
    // blob+42 = frame count = 3 -> registers 2 extra sub-frames.
    wr_u16(blob.data(), 42, 3);

    g_bounds0.assign(32, 0);
    wr_u16(g_bounds0.data(), 6, 0x0010);
    wr_u16(g_bounds0.data(), 10, 0x0020);
    g_boundsK.assign(32, 0);
    wr_u16(g_boundsK.data(), 6, 0x0030);
    wr_u16(g_boundsK.data(), 10, 0x0040);

    ShapeRegHooks hooks;
    hooks.classifyType = [](const void*, void*) -> u8 { return 5; };
    hooks.coordTransform = [](const void*, u16 idx, void*) -> const void* {
        return idx == 0 ? g_bounds0.data() : g_boundsK.data();
    };

    const i32 start = 1;
    i32 count = Shape_RegisterLoaded(table.data(), start, blob.data(), 999,
                                     nullptr, hooks);
    // v38 = startIndex + (frames-1) = start + 2 (gilde.exe 0x41f5e0 tail store;
    // old pin start+3 predates the exact semantics).
    CHECK_EQ(count, start + 2);

    const i32 base = 84 * start;
    CHECK_EQ(rd_u32(table.data(), base + shape_off::TYPE), 5u);
    CHECK_EQ(rd_u16(table.data(), base + shape_off::BOUND_W), static_cast<u16>(0x0010));

    // Sub-frame slots inherit type 5, parent index = start, bounds from boundsK.
    for (i32 k = 1; k <= 2; ++k) {
        const i32 sb = 84 * (start + k);
        CHECK_EQ(rd_u32(table.data(), sb + shape_off::TYPE), 5u);
        CHECK_EQ(rd_u32(table.data(), sb + shape_off::PARENT), static_cast<u32>(start));
        CHECK_EQ(rd_u16(table.data(), sb + shape_off::BOUND_W), static_cast<u16>(0x0030));
        CHECK_EQ(rd_u16(table.data(), sb + shape_off::BOUND_H), static_cast<u16>(0x0040));
        // "..." wide-name first byte.
        CHECK_EQ(rd_u8(table.data(), sb + 0), static_cast<u8>('.'));
        // +48 zeroed.
        CHECK_EQ(rd_u32(table.data(), sb + 48), 0u);
    }
}

// ===========================================================================
// W11-ANIM hardening — LRU / mesh-size degenerate inputs (ASAN).
// ===========================================================================

// ComputeMeshMemorySize null mesh / null sub-array base -> -1 (no deref).
TEST(AnimRecon4Edge, MeshMemorySizeNullGuards) {
    Mem mem; Arena ar = mem.arena();
    CHECK_EQ(ComputeMeshMemorySize(nullptr, ar), -1);
    // mesh present but a1[87] (SUBARRAY) == 0 -> -1.
    const u32 M = 0x2000;
    wr_u32(mem.at(M), mesh_off::SUBARRAY, 0);
    CHECK_EQ(ComputeMeshMemorySize(mem.at(M), ar), -1);
}

// Empty list (head == sentinel): the scan finds no victim. With usage > budget that
// is a failure (returns 0); with usage <= budget it succeeds without scanning.
TEST(AnimRecon4Edge, EvictEmptyList) {
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100;
    MeshAnimHooks hooks;
    // head == sentinel -> nothing to scan, usage>budget -> 0.
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(10, 1000, SENT, SENT, 0xFFFFFFFFu, ar, hooks)), 0);
    // usage already within budget -> 1 (no scan needed).
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(1000, 10, SENT, SENT, 0xFFFFFFFFu, ar, hooks)), 1);
}

// Evict-during-use: a single node at capacity that is BUSY (refcount != 0) cannot be
// evicted; the budget can't be met -> returns 0, and the busy node is NOT freed.
TEST(AnimRecon4Edge, EvictAllBusyAtCapacity) {
    g_freed.clear();
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100, A = 0x2000, B = 0x4000;
    auto node = [&](u32 n, u32 next, u32 prev, u32 key, u32 ref) {
        void* p = mem.at(n);
        wr_u32(p, mesh_off::LIST_NEXT, next);
        wr_u32(p, mesh_off::LIST_PREV, prev);
        wr_u32(p, mesh_off::LRU_KEY, key);
        wr_u32(p, mesh_off::REFCOUNT, ref);
        wr_u32(p, mesh_off::SUBARRAY, 0x8000);
        wr_i32(p, mesh_off::VERT_COUNT, 0);
        wr_i32(p, mesh_off::SUB_COUNT, 1);
    };
    node(A, B,    SENT, 100, 1);   // busy
    node(B, SENT, A,    200, 3);   // busy
    MeshAnimHooks hooks; hooks.freeDebug = &RecordFree;
    CHECK_EQ(static_cast<int>(
        EvictMeshesForBudget(0, 2000, A, SENT, 0xFFFFFFFFu, ar, hooks)), 0);
    CHECK_EQ(g_freed.size(), static_cast<size_t>(0));   // nothing freed (all in use)
    CHECK_EQ(rd_u32(mem.at(A), mesh_off::REFCOUNT), 1u);
    CHECK_EQ(rd_u32(mem.at(B), mesh_off::REFCOUNT), 3u);
}

// ReleaseMeshData with subCount 0: the per-sub-record buffer loop is skipped (no OOB
// over a zero-length sub-array), but the node + (zero) sub-array are still freed.
TEST(AnimRecon4Edge, ReleaseZeroSubCount) {
    g_freed.clear();
    Mem mem; Arena ar = mem.arena();
    const u32 SENT = 0x100, NODE = 0x4000;
    wr_u32(mem.at(NODE), mesh_off::LIST_NEXT, 0);
    wr_u32(mem.at(NODE), mesh_off::LIST_PREV, 0);
    wr_i32(mem.at(NODE), mesh_off::REFCOUNT, 1);    // -> 0 -> free path
    wr_u32(mem.at(NODE), mesh_off::SUBARRAY, 0);    // no sub-array
    wr_i32(mem.at(NODE), mesh_off::SUB_COUNT, 0);   // zero sub-records
    MeshAnimHooks hooks; hooks.freeDebug = &RecordFree;
    ReleaseMeshData(NODE, true, NODE, SENT, 0x400000, ar, hooks);
    // node itself freed; no sub-buffer frees attempted.
    bool freedNode = false;
    for (u32 v : g_freed) if (v == NODE) freedNode = true;
    CHECK(freedNode);
}
