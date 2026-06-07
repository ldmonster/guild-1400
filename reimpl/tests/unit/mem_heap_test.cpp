#include "test.h"
#include "mem/heap.h"
#include "mem/mempool.h"
#include "mem/memory_debug.h"

#include <cstring>
#include <set>
#include <vector>

using namespace guild;
using namespace guild::mem;

// --------------------------------------------------------------------------
// Heap: alloc/free correctness
// --------------------------------------------------------------------------
TEST(MemHeap, AllocReturnsWritableDistinctAligned) {
    Heap heap;
    std::vector<void*> ptrs;
    std::set<void*> seen;
    for (int i = 0; i < 200; ++i) {
        void* p = heap.AllocFromFreeList(40);
        CHECK(p != nullptr);
        // distinct
        CHECK(seen.find(p) == seen.end());
        seen.insert(p);
        // writable across the whole payload
        std::memset(p, 0xAB, 40);
        ptrs.push_back(p);
    }
    // Confirm earlier writes were not clobbered by later allocations.
    for (void* p : ptrs) {
        u8* b = static_cast<u8*>(p);
        for (int j = 0; j < 40; ++j)
            CHECK_EQ(b[j], 0xAB);
    }
    for (void* p : ptrs)
        heap.ReturnToFreeList(p);
}

TEST(MemHeap, FreeThenAllocReuse) {
    Heap heap;
    void* a = heap.AllocFromFreeList(64);
    CHECK(a != nullptr);
    heap.ReturnToFreeList(a);
    // A same-size request right after should reuse the freed chunk.
    void* b = heap.AllocFromFreeList(64);
    CHECK(b != nullptr);
    CHECK_EQ(a, b);
    heap.ReturnToFreeList(b);
}

TEST(MemHeap, VariedSizesAllSatisfiable) {
    Heap heap;
    std::vector<void*> ptrs;
    for (u32 sz = 1; sz <= 4096; sz += 37) {
        void* p = heap.AllocFromFreeList(sz);
        CHECK(p != nullptr);
        std::memset(p, 0x5A, sz); // writable to the requested size
        ptrs.push_back(p);
    }
    for (void* p : ptrs)
        heap.ReturnToFreeList(p);
}

TEST(MemHeap, NullAndOversizeRejected) {
    Heap heap;
    CHECK(heap.AllocFromFreeList(0) == nullptr);
    CHECK(heap.AllocFromFreeList(0xFFFFFFFFu) == nullptr);
    heap.ReturnToFreeList(nullptr); // null-safe
}

// --------------------------------------------------------------------------
// Memory debug tracker: guard words + group accounting
// --------------------------------------------------------------------------
TEST(MemDebug, GuardWordsWrittenAndPreserved) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    void* p = tr.AllocDebug(32, "tex:font");
    CHECK(p != nullptr);
    // Leading and trailing guard words are present around the payload.
    u8* block = static_cast<u8*>(p) - 4;
    CHECK_EQ(*reinterpret_cast<u32*>(block), kGuardWord);
    CHECK_EQ(*reinterpret_cast<u32*>(block + 32 + 4), kGuardWord);
    // Writing within bounds keeps guards intact.
    std::memset(p, 0x11, 32);
    CHECK_EQ(*reinterpret_cast<u32*>(block), kGuardWord);
    CHECK_EQ(*reinterpret_cast<u32*>(block + 32 + 4), kGuardWord);
    CHECK_EQ(tr.CorruptionCount(), 0);
    tr.FreeDebug(p);
    CHECK_EQ(tr.CorruptionCount(), 0);
}

TEST(MemDebug, DetectsTrailingOverwrite) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    void* p = tr.AllocDebug(16, "buf:net");
    CHECK(p != nullptr);
    // Write one byte past the payload, into the trailing guard word.
    static_cast<u8*>(p)[16] = 0xFF;
    tr.FreeDebug(p);
    CHECK_EQ(tr.CorruptionCount(), 1);
}

TEST(MemDebug, DetectsLeadingOverwrite) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    void* p = tr.AllocDebug(16, "buf:net");
    CHECK(p != nullptr);
    // Underwrite into the leading guard word.
    static_cast<u8*>(p)[-1] = 0x00;
    tr.FreeDebug(p);
    CHECK_EQ(tr.CorruptionCount(), 1);
}

TEST(MemDebug, GroupAccountingTotals) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(128);

    void* a = tr.AllocDebug(100, "snd:wav");
    void* b = tr.AllocDebug(200, "snd:wav");
    void* c = tr.AllocDebug(50, "gfx:bmp");
    CHECK(a && b && c);

    const MemGroup* snd = tr.FindGroup("snd");
    const MemGroup* gfx = tr.FindGroup("gfx");
    CHECK(snd != nullptr);
    CHECK(gfx != nullptr);

    // Group byte totals include the 8 guard bytes per block.
    CHECK_EQ(snd->curBytes, (100 + 8) + (200 + 8));
    CHECK_EQ(snd->curBlocks, 2);
    CHECK_EQ(gfx->curBytes, 50 + 8);
    CHECK_EQ(gfx->curBlocks, 1);

    // Global totals.
    CHECK_EQ(tr.CurBlocks(), 3);
    CHECK_EQ(tr.CurBytes(), (100 + 8) + (200 + 8) + (50 + 8));

    // Peaks tracked.
    CHECK_EQ(tr.MaxBlocks(), 3);

    tr.FreeDebug(b);
    CHECK_EQ(snd->curBlocks, 1);
    CHECK_EQ(snd->curBytes, 100 + 8);
    CHECK_EQ(tr.CurBlocks(), 2);
    // Peak unchanged after free.
    CHECK_EQ(tr.MaxBlocks(), 3);

    tr.FreeDebug(a);
    tr.FreeDebug(c);
    CHECK_EQ(tr.CurBlocks(), 0);
    CHECK_EQ(tr.CurBytes(), 0);
    CHECK_EQ(snd->curBlocks, 0);
    CHECK_EQ(snd->curBytes, 0);
}

TEST(MemDebug, IsValidPointer) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(32);
    void* p = tr.AllocDebug(24, "x:y");
    CHECK(tr.IsValidPointer(p));
    CHECK(tr.IsValidPointer(nullptr)); // null is "valid" per the original
    int stack;
    CHECK(!tr.IsValidPointer(&stack));
    tr.FreeDebug(p);
    CHECK(!tr.IsValidPointer(p));
}

TEST(MemDebug, GroupInterningReusesSameGroup) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(32);
    void* a = tr.AllocDebug(10, "pool:a");
    void* b = tr.AllocDebug(10, "pool:b");
    CHECK(a && b);
    // Same group name ("pool") regardless of detail.
    const MemGroup* g = tr.FindGroup("pool");
    CHECK(g != nullptr);
    CHECK_EQ(g->curBlocks, 2);
    tr.FreeDebug(a);
    tr.FreeDebug(b);
}

// --------------------------------------------------------------------------
// MemPool: fixed-block exhaustion / recycle
// --------------------------------------------------------------------------
TEST(MemPool, ExhaustionGrowsNewChunk) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(256);
    MemPool pool;

    const int kPer = 4;
    std::vector<void*> ptrs;
    std::set<void*> seen;
    // Allocate more than one chunk's worth; pool must grow.
    for (int i = 0; i < kPer * 3; ++i) {
        void* p = MemPoolAlloc(&pool, tr, 24, kPer);
        CHECK(p != nullptr);
        CHECK(seen.insert(p).second); // distinct
        std::memset(p, 0xCD, 24);     // writable
        ptrs.push_back(p);
    }
    for (void* p : ptrs)
        MemPoolFree(&pool, tr, p);
    MemPoolFreeAll(&pool, tr);
}

TEST(MemPool, RecycleSlots) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    MemPool pool;

    void* a = MemPoolAlloc(&pool, tr, 32, 8);
    void* b = MemPoolAlloc(&pool, tr, 32, 8);
    CHECK(a && b);
    CHECK(a != b);
    MemPoolFree(&pool, tr, a);
    // Freed slot recycled on the next same-size allocation.
    void* c = MemPoolAlloc(&pool, tr, 32, 8);
    CHECK_EQ(a, c);
    MemPoolFreeAll(&pool, tr);
}

TEST(MemPool, ResetBlocks) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    MemPool pool;
    for (int i = 0; i < 5; ++i)
        CHECK(MemPoolAlloc(&pool, tr, 16, 8) != nullptr);
    int reset = MemPoolResetBlocks(&pool);
    CHECK_EQ(reset, 5);
    // After reset all slots are free again.
    PoolCursor cur;
    CHECK(MemPoolBegin(&pool, &cur) == nullptr);
    MemPoolFreeAll(&pool, tr);
}

TEST(MemPool, IterateLiveSlots) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(64);
    MemPool pool;
    std::set<void*> live;
    for (int i = 0; i < 6; ++i)
        live.insert(MemPoolAlloc(&pool, tr, 16, 4));
    // Iterate and confirm every live slot is visited exactly once.
    std::set<void*> visited;
    PoolCursor cur;
    for (void* p = MemPoolBegin(&pool, &cur); p; p = MemPoolNext(&cur)) {
        CHECK(live.count(p) == 1);
        CHECK(visited.insert(p).second);
    }
    CHECK_EQ(visited.size(), live.size());
    MemPoolFreeAll(&pool, tr);
}
