#include "test.h"
#include "mem/heap.h"
#include "mem/memory_debug.h"
#include "mem/mempool.h"

#include <cstring>
#include <vector>

// Deterministic PRNG for the churn workload (independent of the engine RNG so
// this test is self-contained).
namespace {
struct Lcg {
    guild::u32 s;
    explicit Lcg(guild::u32 seed) : s(seed) {}
    guild::u32 next() {
        s = s * 1103515245u + 12345u;
        return (s >> 16) & 0x7FFFu;
    }
    guild::u32 range(guild::u32 lo, guild::u32 hi) { return lo + next() % (hi - lo + 1); }
};
} // namespace

using namespace guild;
using namespace guild::mem;

// Mixed alloc/realloc/free churn across the whole allocator stack, verifying no
// corruption and correct accounting at the end.
TEST(MemE2E, ChurnWorkloadNoCorruption) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(4096);

    struct Live {
        void* p;
        u32 size;
        u8 fill;
    };
    std::vector<Live> live;
    Lcg rng(0xC0FFEEu);

    const int kIters = 20000;
    for (int i = 0; i < kIters; ++i) {
        u32 op = rng.range(0, 9);
        bool doFree = (op < 4 && !live.empty());
        bool doRealloc = (op >= 4 && op < 6 && !live.empty());

        if (doFree) {
            // Free a random live block; verify its contents survived intact.
            u32 idx = rng.range(0, static_cast<u32>(live.size() - 1));
            Live l = live[idx];
            u8* b = static_cast<u8*>(l.p);
            for (u32 j = 0; j < l.size; ++j)
                CHECK_EQ(b[j], l.fill);
            tr.FreeDebug(l.p);
            live[idx] = live.back();
            live.pop_back();
        } else if (doRealloc) {
            // Emulate realloc: alloc new, copy min(old,new), free old, preserve.
            u32 idx = rng.range(0, static_cast<u32>(live.size() - 1));
            Live old = live[idx];
            u32 nsize = rng.range(1, 512);
            void* np = tr.AllocDebug(nsize, "churn:rea");
            CHECK(np != nullptr);
            u32 copy = nsize < old.size ? nsize : old.size;
            std::memcpy(np, old.p, copy);
            // Verify the copied prefix matches the old fill.
            u8* nb = static_cast<u8*>(np);
            for (u32 j = 0; j < copy; ++j)
                CHECK_EQ(nb[j], old.fill);
            tr.FreeDebug(old.p);
            // Re-fill the whole new block with a fresh pattern.
            u8 fill = static_cast<u8>(rng.range(1, 254));
            std::memset(np, fill, nsize);
            live[idx] = Live{np, nsize, fill};
        } else {
            // Allocate a random-sized block, fill it with a unique pattern.
            u32 size = rng.range(1, 512);
            u8 fill = static_cast<u8>(rng.range(1, 254));
            void* p = tr.AllocDebug(size, "churn:new");
            CHECK(p != nullptr);
            std::memset(p, fill, size);
            live.push_back(Live{p, size, fill});
        }
    }

    // Final integrity sweep: every live block still holds its pattern, then free.
    for (const Live& l : live) {
        u8* b = static_cast<u8*>(l.p);
        for (u32 j = 0; j < l.size; ++j)
            CHECK_EQ(b[j], l.fill);
    }
    int liveCount = static_cast<int>(live.size());
    CHECK_EQ(tr.CurBlocks(), liveCount);
    for (const Live& l : live)
        tr.FreeDebug(l.p);

    // Everything freed -> zero accounting, no corruption observed.
    CHECK_EQ(tr.CurBlocks(), 0);
    CHECK_EQ(tr.CurBytes(), 0);
    CHECK_EQ(tr.CorruptionCount(), 0);
    // Peaks are non-trivial (the workload did real work).
    CHECK(tr.MaxBlocks() > 0);
    CHECK(tr.MaxBytes() > 0);
}

// End-to-end pool flow layered on the tracker, with interleaved direct allocs.
TEST(MemE2E, PoolAndTrackerInterleaved) {
    Heap heap;
    MemoryTracker tr(heap);
    tr.Init(2048);
    MemPool pool;
    Lcg rng(0x1234u);

    std::vector<void*> poolPtrs;
    std::vector<void*> rawPtrs;

    for (int i = 0; i < 2000; ++i) {
        u32 op = rng.range(0, 9);
        if (op < 5) {
            void* p = MemPoolAlloc(&pool, tr, 48, 16);
            CHECK(p != nullptr);
            std::memset(p, 0x7E, 48);
            poolPtrs.push_back(p);
        } else if (op < 7 && !poolPtrs.empty()) {
            u32 idx = rng.range(0, static_cast<u32>(poolPtrs.size() - 1));
            MemPoolFree(&pool, tr, poolPtrs[idx]);
            poolPtrs[idx] = poolPtrs.back();
            poolPtrs.pop_back();
        } else if (op < 9) {
            void* p = tr.AllocDebug(rng.range(8, 256), "raw:misc");
            CHECK(p != nullptr);
            rawPtrs.push_back(p);
        } else if (!rawPtrs.empty()) {
            u32 idx = rng.range(0, static_cast<u32>(rawPtrs.size() - 1));
            tr.FreeDebug(rawPtrs[idx]);
            rawPtrs[idx] = rawPtrs.back();
            rawPtrs.pop_back();
        }
    }

    for (void* p : rawPtrs)
        tr.FreeDebug(p);
    MemPoolFreeAll(&pool, tr);

    CHECK_EQ(tr.CurBlocks(), 0);
    CHECK_EQ(tr.CurBytes(), 0);
    CHECK_EQ(tr.CorruptionCount(), 0);
}
