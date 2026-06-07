#include "mem/small_heap.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <vector>

using namespace guild;
using namespace guild::mem;

// --- size-class table (recovered + documented 64-bit overhead widening) ---
// The original rounds chunk = (n + 23) & ~15 (4-byte head + 4-byte footer). On
// 64-bit the head and footer are widened to 8 bytes each, so the overhead is 16
// and chunk = (n + 31) & ~15 (see small_heap.cpp). The (chunk>>4)-1 bucketing and
// the 16-byte granularity are unchanged.
TEST(MemSmallHeap, SizeClassTable) {
    CHECK_EQ(SmallChunkSize(1), 32u);     // (1+31)=32 -> 32
    CHECK_EQ(SmallChunkSize(8), 32u);     // (8+31)=39 -> 32
    CHECK_EQ(SmallChunkSize(16), 32u);    // (16+31)=47 -> 32
    CHECK_EQ(SmallChunkSize(17), 48u);    // (17+31)=48 -> 48
    CHECK_EQ(SmallChunkSize(24), 48u);    // (24+31)=55 -> 48
    CHECK_EQ(SmallChunkSize(1016), 1040u);// largest serviced (payload 1024 >= 1016)
    CHECK_EQ(SmallChunkSize(1017), 0u);   // beyond kSmallBlockMax

    // SmallSizeClass is the exact recovered table: (chunk>>4)-1, clamped to 63.
    CHECK_EQ(SmallSizeClass(16), 0u);
    CHECK_EQ(SmallSizeClass(32), 1u);
    CHECK_EQ(SmallSizeClass(48), 2u);
    CHECK_EQ(SmallSizeClass(1024), 63u);  // (1024>>4)-1 = 63
    CHECK_EQ(SmallSizeClass(4096), 63u);  // clamped
}

// --- recovered geometry constants ---
TEST(MemSmallHeap, GeometryConstants) {
    CHECK_EQ(kSizeClasses, 64u);
    CHECK_EQ(kSizeGranule, 16u);
    CHECK_EQ(kSmallBlockMax, 1016u);
    CHECK_EQ(kRegionBytes, 0x100000u);
    CHECK_EQ(kGroupsPerRegion, 32u);
    CHECK_EQ(kGroupBytes, 0x8000u);
    CHECK_EQ(kGroupRunBytes, 0x7000u);
}

// --- basic alloc/free: writable, distinct, 16-aligned ---
TEST(MemSmallHeap, AllocWritableDistinctAligned) {
    SmallHeap h;
    std::vector<void*> ptrs;
    for (int i = 0; i < 200; ++i) {
        void* p = h.Alloc(40);
        CHECK(p != nullptr);
        // Chunk bases are 16-granular; payload sits past the 8-byte header, so
        // user pointers are 8-aligned (the original returns chunk+4 = 4-aligned;
        // the 64-bit-widened header yields 8-alignment here).
        CHECK((reinterpret_cast<std::uintptr_t>(p) & 7u) == 0);
        std::memset(p, 0xAB, 40);                                // writable
        ptrs.push_back(p);
    }
    std::set<void*> uniq(ptrs.begin(), ptrs.end());
    CHECK_EQ(uniq.size(), ptrs.size());          // all distinct
    CHECK_EQ(h.LiveBlocks(), ptrs.size());
    CHECK(h.Validate());
    for (void* p : ptrs)
        h.Free(p);
    CHECK_EQ(h.LiveBlocks(), 0u);
    CHECK(h.Validate());
}

// --- distinct allocations don't overlap (write a tag, read it back) ---
TEST(MemSmallHeap, NoOverlapAcrossSizes) {
    SmallHeap h;
    struct B { void* p; u32 n; u8 tag; };
    std::vector<B> bs;
    u8 tag = 1;
    for (u32 n = 8; n <= 1000; n += 37) {
        void* p = h.Alloc(n);
        CHECK(p != nullptr);
        std::memset(p, tag, n);
        bs.push_back({p, n, tag});
        ++tag;
    }
    // verify none clobbered another
    for (const B& b : bs) {
        u8* q = static_cast<u8*>(b.p);
        bool ok = true;
        for (u32 i = 0; i < b.n; ++i)
            if (q[i] != b.tag) { ok = false; break; }
        CHECK(ok);
    }
    CHECK(h.Validate());
    for (const B& b : bs)
        h.Free(b.p);
    CHECK(h.Validate());
}

// --- free reuses space (alloc/free/alloc returns within same group) ---
TEST(MemSmallHeap, FreeReuses) {
    SmallHeap h;
    void* a = h.Alloc(64);
    CHECK(a != nullptr);
    std::size_t groups1 = h.GroupCount();
    h.Free(a);
    void* b = h.Alloc(64);
    CHECK(b != nullptr);
    CHECK_EQ(h.GroupCount(), groups1);   // no new group needed
    CHECK_EQ(b, a);                      // same coalesced slot recycled
    h.Free(b);
    CHECK(h.Validate());
}

// --- region fill / recycle: fill a group, free everything, refill ---
TEST(MemSmallHeap, RegionFillRecycle) {
    SmallHeap h;
    std::vector<void*> ptrs;
    // Fill enough to commit several groups.
    for (int i = 0; i < 2000; ++i) {
        void* p = h.Alloc(200);
        CHECK(p != nullptr);
        ptrs.push_back(p);
    }
    CHECK(h.GroupCount() >= 1u);
    CHECK(h.Validate());
    for (void* p : ptrs)
        h.Free(p);
    CHECK_EQ(h.LiveBlocks(), 0u);
    CHECK(h.Validate());
    // Refill: should reuse recycled/cached groups, not grow unbounded.
    std::size_t regionsAfterFree = h.RegionCount();
    for (int i = 0; i < 2000; ++i) {
        void* p = h.Alloc(200);
        CHECK(p != nullptr);
        ptrs[i] = p;
    }
    CHECK(h.RegionCount() <= regionsAfterFree + 1);
    CHECK(h.Validate());
    for (void* p : ptrs)
        h.Free(p);
    CHECK(h.Validate());
}

// --- realloc preserves data (grow + shrink) ---
TEST(MemSmallHeap, ReallocPreservesData) {
    SmallHeap h;
    void* p = h.Alloc(32);
    CHECK(p != nullptr);
    for (int i = 0; i < 32; ++i)
        static_cast<u8*>(p)[i] = static_cast<u8>(i + 1);

    // grow
    void* g = h.Realloc(p, 300);
    CHECK(g != nullptr);
    bool ok = true;
    for (int i = 0; i < 32; ++i)
        if (static_cast<u8*>(g)[i] != static_cast<u8>(i + 1)) { ok = false; break; }
    CHECK(ok);
    // extend the buffer, then shrink
    std::memset(static_cast<u8*>(g) + 32, 0x5A, 300 - 32);
    void* s = h.Realloc(g, 20);
    CHECK(s != nullptr);
    ok = true;
    for (int i = 0; i < 20; ++i)
        if (static_cast<u8*>(s)[i] != static_cast<u8>(i + 1)) { ok = false; break; }
    CHECK(ok);
    CHECK(h.Validate());

    // realloc(null) == alloc, realloc(p,0) == free
    void* z = h.Realloc(nullptr, 50);
    CHECK(z != nullptr);
    CHECK_EQ(h.Realloc(z, 0), nullptr);
    h.Free(s);
    CHECK(h.Validate());
}

// --- resize in place: shrink always succeeds, grow only if next is free ---
TEST(MemSmallHeap, ResizeInPlace) {
    SmallHeap h;
    void* a = h.Alloc(64);
    void* b = h.Alloc(64);          // pins a's neighbour
    CHECK(a != nullptr && b != nullptr);
    // grow a into b's space must fail (b is in use)
    CHECK(!h.Resize(a, 200));
    // shrink a always works in place
    CHECK(h.Resize(a, 16));
    CHECK(h.Validate());
    // now b is the last block before the big free tail; growing it should work
    CHECK(h.Resize(b, 300));
    CHECK(h.Validate());
    h.Free(a);
    h.Free(b);
    CHECK(h.Validate());
}

// --- boundary tags / coalescing keep the run well-formed under interleaving ---
TEST(MemSmallHeap, CoalesceInterleaved) {
    SmallHeap h;
    std::vector<void*> ptrs;
    for (int i = 0; i < 64; ++i)
        ptrs.push_back(h.Alloc(48));
    CHECK(h.Validate());
    // free every other -> isolated free chunks
    for (size_t i = 0; i < ptrs.size(); i += 2) {
        h.Free(ptrs[i]);
        ptrs[i] = nullptr;
    }
    CHECK(h.Validate());
    // free the rest -> everything coalesces
    for (void* p : ptrs)
        if (p) h.Free(p);
    CHECK_EQ(h.LiveBlocks(), 0u);
    CHECK(h.Validate());
}
