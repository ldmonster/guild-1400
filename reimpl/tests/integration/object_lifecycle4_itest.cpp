#include "test.h"

// Integration: drive object_lifecycle4's spawn / draw-data (de)allocation family
// against the REAL reconstructed memory sibling — the debug allocation tracker
// (mem/memory_debug.cpp, VIBE_Memory_AllocDebug @0x438f10 / VIBE_Memory_FreeDebug
// @0x43923c) layered on the REAL free-list heap (mem/heap.cpp). The ObjLife4Hooks
// allocDebug/freeDebug slots ARE those two functions in the live binary (the hook
// comments name them); we forward them straight into a real MemoryTracker, exactly
// as the game wires them, and assert the node/light/poly allocations the lifecycle
// code makes are accounted and reclaimed by the real allocator.
#include "sim/object_lifecycle4.h"
#include "mem/memory_debug.h"   // REAL reconstructed sibling: MemoryTracker
#include "mem/heap.h"           // REAL reconstructed sibling: Heap

using namespace guild;
using namespace guild::sim;

namespace {
// One real heap + tracker shared by the hooks (the game's single process-global).
guild::mem::Heap*           g_heap    = nullptr;
guild::mem::MemoryTracker*  g_tracker = nullptr;

void* AllocHook(int size, const char* tag) {
    return g_tracker->AllocDebug(static_cast<u32>(size), tag);
}
void FreeHook(void* p) { g_tracker->FreeDebug(p); }

// The real tracker layers on a fresh heap whose pages are zero-filled, so the
// freshly-carved node block starts zeroed — exactly the state the live
// VIBE_Object_InitStruct leaves it in. We therefore leave initStruct inert (the
// lightInfo pointer the spawn writes BEFORE InitStruct must survive, and the real
// InitStruct does not clobber it). Every other leaf also stays inert.
ObjLife4Hooks MakeHooks() {
    ObjLife4Hooks h{};            // every other leaf stays inert (nullptr)
    h.allocDebug  = &AllocHook;   // -> real MemoryTracker::AllocDebug
    h.freeDebug   = &FreeHook;    // -> real MemoryTracker::FreeDebug
    return h;
}

// The real tracker accounts userSize + 8 guard bytes per live block.
constexpr int kGuardOverhead = 8;
} // namespace

// Spawn a kind>=5 light node ('p' => type 7): the node block (0x21C) AND the
// light-info block (0x1AC) are both carved from the REAL tracker; confirm the
// tracker accounts exactly two live blocks summing to those sizes.
TEST(ObjLifecycle4Itest, SpawnAllocatesNodeAndLightInfoFromRealTracker) {
    guild::mem::Heap heap;
    guild::mem::MemoryTracker tracker(heap);
    tracker.Init(256);
    g_heap = &heap; g_tracker = &tracker;

    ObjLife4Hooks h = MakeHooks();
    ObjLife4SetHooks(h);
    g_lightInfoForce5 = false;

    i32 before = tracker.CurBytes();
    i32 beforeBlocks = tracker.CurBlocks();

    ObjNode4* node = ObjectSpawn(/*kind=*/7, "player_light");
    CHECK(node != nullptr);
    if (node) {
        CHECK_EQ(static_cast<int>(node->nodeType), 7);   // 'p' classifies to 7
        CHECK(node->lightInfo != nullptr);               // kind>=5 allocates lightInfo
    }
    // Two real allocations: the 0x21C node + the 0x1AC light-info block.
    CHECK_EQ(tracker.CurBlocks() - beforeBlocks, 2);
    CHECK_EQ(tracker.CurBytes() - before,
             kNodeAllocSize + kLightInfoSize + 2 * kGuardOverhead);
    CHECK_EQ(tracker.CorruptionCount(), 0);              // no guard-word overwrite

    // Hand both blocks back through the real free path; accounting returns to base.
    if (node) {
        FreeHook(node->lightInfo);
        FreeHook(node);
    }
    CHECK_EQ(tracker.CurBlocks(), beforeBlocks);
    CHECK_EQ(tracker.CurBytes(), before);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    ObjLife4ResetHooks();
    g_heap = nullptr; g_tracker = nullptr;
}

// A kind<5 node takes no light-info block: exactly one real allocation.
TEST(ObjLifecycle4Itest, SpawnPlainNodeAllocatesOneBlock) {
    guild::mem::Heap heap;
    guild::mem::MemoryTracker tracker(heap);
    tracker.Init(256);
    g_heap = &heap; g_tracker = &tracker;

    ObjLife4SetHooks(MakeHooks());

    i32 beforeBlocks = tracker.CurBlocks();
    ObjNode4* node = ObjectSpawn(/*kind=*/3, "camera");
    CHECK(node != nullptr);
    if (node) {
        CHECK_EQ(static_cast<int>(node->nodeType), 3);   // kind<5 copied verbatim
        CHECK(node->lightInfo == nullptr);
    }
    CHECK_EQ(tracker.CurBlocks() - beforeBlocks, 1);

    if (node) FreeHook(node);
    CHECK_EQ(tracker.CurBlocks(), beforeBlocks);

    ObjLife4ResetHooks();
    g_heap = nullptr; g_tracker = nullptr;
}

// AllocPolysAndPoints carves polys (40*pointCount) + points (80*(polyCount+8))
// from the REAL tracker; FreeSubMeshData hands both back. Confirms the lifecycle
// code's realloc/free arithmetic is honoured by the real allocator end to end.
TEST(ObjLifecycle4Itest, PolyPointBuffersRoundTripThroughRealTracker) {
    guild::mem::Heap heap;
    guild::mem::MemoryTracker tracker(heap);
    tracker.Init(256);
    g_heap = &heap; g_tracker = &tracker;

    ObjLife4SetHooks(MakeHooks());

    SubMeshEntry entry;        // value-constructed (zeroed pointers/counts)
    entry.polyCount  = 4;      // -> points block = 80 * (4 + 8) = 960
    entry.pointCount = 6;      // -> polys  block = 40 * 6       = 240

    i32 beforeBlocks = tracker.CurBlocks();
    i32 beforeBytes  = tracker.CurBytes();

    void* points = ObjectAllocPolysAndPoints(&entry);
    CHECK(points != nullptr);
    CHECK(entry.polys != nullptr);
    CHECK(entry.points == points);
    // Two real allocations carved with the exact byte counts.
    CHECK_EQ(tracker.CurBlocks() - beforeBlocks, 2);
    CHECK_EQ(tracker.CurBytes() - beforeBytes,
             (40 * 6) + (80 * (4 + 8)) + 2 * kGuardOverhead);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    // FreeSubMeshData frees polys (pointCount>0) and points (polyCount>0) via the
    // real FreeDebug, clearing the counts.
    int rc = ObjectFreeSubMeshData(&entry);
    CHECK_EQ(rc, 0);
    CHECK(entry.points == nullptr);
    CHECK(entry.polys == nullptr);
    CHECK_EQ(tracker.CurBlocks(), beforeBlocks);
    CHECK_EQ(tracker.CurBytes(), beforeBytes);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    ObjLife4ResetHooks();
    g_heap = nullptr; g_tracker = nullptr;
}
