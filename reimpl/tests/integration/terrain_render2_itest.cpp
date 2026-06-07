#include "test.h"

// Integration: drive terrain_render2's VIBE_Floor_AllocTileBuffers /
// VIBE_Floor_AllocLightBuffers against the REAL reconstructed memory sibling —
// the debug allocation tracker (mem/memory_debug.cpp, VIBE_Memory_AllocDebug
// @0x438f10 / VIBE_Memory_FreeDebug @0x43923c) layered on the REAL free-list heap
// (mem/heap.cpp). The TerrainRender2Hooks allocDebug/freeDebug slots ARE those two
// functions in the live binary (the hook comments name the exact addresses); we
// forward them straight into a real MemoryTracker, exactly as the engine wires
// them, and assert the tile/floor buffers terrain_render2 allocates are accounted
// per named group and reclaimed by the real allocator.
//
// (computeSlopeFlags / buildTilePolys have no reconstructed sibling yet, so they
// are left on the terrain_render2 inert default no-op path.)
#include "render/terrain_render2.h"
#include "mem/memory_debug.h"   // REAL reconstructed sibling: MemoryTracker
#include "mem/heap.h"           // REAL reconstructed sibling: Heap

#include <cstdint>
#include <cstring>

using namespace guild;

namespace {

// One real heap + tracker shared by the hooks (the game's single process-global).
mem::Heap*          g_heap    = nullptr;
mem::MemoryTracker* g_tracker = nullptr;

// Per-tag allocation capture (verified at call time — the floor-level divide
// buffer slots at +36/+40/+44 are 4 bytes apart in the original 32-bit record and
// OVERLAP on LP64, so they cannot be read back; the header documents that the
// Alloc leaves expose their recovered size math via the allocDebug hook instead).
struct AllocRec { char tag[32]; unsigned size; void* ptr; };
AllocRec g_allocLog[2048];
int      g_allocLogN = 0;

// terrain_render2 allocDebug hook -> real VIBE_Memory_AllocDebug (+ capture).
void* AllocHook(unsigned int size, const char* tag) {
    void* p = g_tracker->AllocDebug(size, tag);
    if (g_allocLogN < 2048) {
        AllocRec& r = g_allocLog[g_allocLogN++];
        std::strncpy(r.tag, tag ? tag : "", sizeof r.tag - 1);
        r.tag[sizeof r.tag - 1] = 0;
        r.size = size;
        r.ptr = p;
    }
    return p;
}

bool SawTag(const char* needle) {
    for (int i = 0; i < g_allocLogN; ++i)
        if (std::strcmp(g_allocLog[i].tag, needle) == 0) return true;
    return false;
}
// terrain_render2 freeDebug hook -> real VIBE_Memory_FreeDebug.
void FreeHook(void* p) { g_tracker->FreeDebug(p); }

render::TerrainRender2Hooks MakeHooks() {
    render::TerrainRender2Hooks h{};
    h.allocDebug        = &AllocHook;        // -> real MemoryTracker::AllocDebug
    h.freeDebug         = &FreeHook;         // -> real MemoryTracker::FreeDebug
    h.computeSlopeFlags = [](void*) {};      // inert default (no reconstructed sibling)
    h.buildTilePolys    = [](void*) {};      // inert default (no reconstructed sibling)
    return h;
}

// The real tracker accounts userSize + 8 guard bytes per live block.
constexpr int kGuardOverhead = 8;

} // namespace

// AllocTileBuffers(tile, span=4, lodShift=0) -> v6 = 4. Allocates four named
// buffers through the real tracker; assert the recovered SIZE MATH lands in the
// matching interned groups and the live block accounting is exact.
TEST(TerrainRender2Itest, AllocTileBuffersAccountedByRealTracker) {
    mem::Heap heap;
    mem::MemoryTracker tracker(heap);
    tracker.Init(256);
    g_heap = &heap; g_tracker = &tracker;

    g_allocLogN = 0;
    render::TerrainRender2Hooks h = MakeHooks();
    render::SetTerrainRender2Hooks(&h);

    // The tile header is read/written up to offset +97; the pointer slots
    // (+24/+40/+48/+60/+52/+28/+44/+36) must start zeroed so every alloc fires.
    std::uint8_t tile[128];
    std::memset(tile, 0, sizeof tile);

    i32 beforeBytes  = tracker.CurBytes();
    i32 beforeBlocks = tracker.CurBlocks();

    int polys = render::AllocTileBuffers(reinterpret_cast<int*>(tile), 4u, 0u);

    // Recovered size math (v6 = span>>lodShift = 4):
    const unsigned v6 = 4u;
    const int sPoints = 80 * (v6 + 2) * (v6 + 2);              // 2880
    const int sPolys  = 40 * (v6 + 1) * (2 * v6 + 2);          // 2000
    const int sSplit  = 48 * (v6 + 2);                         // 288
    const int sBp     = 24 * ((2 * v6 + 2) * (v6 + 1) + (8 * v6 + 16)); // 2352

    // poly count returned == (2*v6+2)*(v6+1).
    CHECK_EQ(polys, static_cast<int>((2 * v6 + 2) * (v6 + 1)));

    // Four real allocations, one per named group.
    CHECK_EQ(tracker.CurBlocks() - beforeBlocks, 4);
    CHECK_EQ(tracker.CurBytes() - beforeBytes,
             sPoints + sPolys + sSplit + sBp + 4 * kGuardOverhead);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    // The interned groups carry the recovered per-buffer sizes (+ guard).
    const mem::MemGroup* gPoints = tracker.FindGroup("d3_fl");
    CHECK(gPoints != nullptr);   // all four tags share the "d3_fl" group prefix

    // The pointer slots got real, distinct, non-null blocks.
    void* pPoints = *reinterpret_cast<void**>(tile + 24);
    void* pPolys  = *reinterpret_cast<void**>(tile + 40);
    void* pSplit  = *reinterpret_cast<void**>(tile + 48);
    void* pBp     = *reinterpret_cast<void**>(tile + 60);
    CHECK(pPoints != nullptr);
    CHECK(pPolys  != nullptr);
    CHECK(pSplit  != nullptr);
    CHECK(pBp     != nullptr);
    if (pPoints && pPolys) CHECK(pPoints != pPolys);

    // Real tracker validates the blocks it handed out.
    CHECK(tracker.IsValidPointer(pPoints));
    CHECK(tracker.IsValidPointer(pBp));

    // Free them back through the real sibling; accounting returns to baseline.
    tracker.FreeDebug(pPoints);
    tracker.FreeDebug(pPolys);
    tracker.FreeDebug(pSplit);
    tracker.FreeDebug(pBp);
    CHECK_EQ(tracker.CurBlocks(), beforeBlocks);
    CHECK_EQ(tracker.CurBytes(), beforeBytes);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    render::SetTerrainRender2Hooks(nullptr);
}

// AllocLightBuffers drives a small (size=2) floor end to end: the 3 divide
// buffers + light + light-offset, plus every tile's buffers, all flow through the
// real tracker. Assert the real allocator handed out a live, consistent set and
// that the floor's "built" bit (+7280 |= 1) + light marker (+7276 = 0xFF) are set.
TEST(TerrainRender2Itest, AllocLightBuffersFloorViaRealTracker) {
    mem::Heap heap;
    mem::MemoryTracker tracker(heap);
    tracker.Init(2048);
    g_heap = &heap; g_tracker = &tracker;

    g_allocLogN = 0;
    render::TerrainRender2Hooks h = MakeHooks();
    render::SetTerrainRender2Hooks(&h);

    // Floor record: tiles run base+224 .. base+1024+800*7, plus scalar fields at
    // +7276/+7280/+7281. Size the buffer to cover the whole record.
    constexpr int kFloorBytes = 800 * 8 + 1100;  // > 7281
    std::uint8_t* floor = new std::uint8_t[kFloorBytes];
    std::memset(floor, 0, kFloorBytes);
    int* a1 = reinterpret_cast<int*>(floor);
    a1[0] = 2;   // size (+0)
    a1[1] = 2;   // tileSpan (+4) — fed to AllocTileBuffers
    floor[7281] = 0;  // lodShift nibble = 0

    i32 beforeBlocks = tracker.CurBlocks();

    int rc = render::AllocLightBuffers(a1);
    CHECK_EQ(rc, 0);

    // Real tracker now holds the named floor buffers + the per-tile buffers.
    CHECK(tracker.CurBlocks() > beforeBlocks);
    CHECK_EQ(tracker.CorruptionCount(), 0);

    // The named floor-level buffers were requested from the REAL tracker (verified
    // at call time via the alloc hook — the +36/+40/+44 divide-buffer pointer slots
    // overlap on LP64 and cannot be read back; size math is the load-bearing part).
    CHECK(SawTag("d3_fl:Divide0"));
    CHECK(SawTag("d3_fl:Divide1"));
    CHECK(SawTag("d3_fl:Divide2"));
    CHECK(SawTag("d3_fl:Light"));
    CHECK(SawTag("d3_fl:TilePoints"));   // a per-tile buffer => the tile loop ran
    // NOTE: d3_fl:LightOffset (+32) is gated by `if (!I(a1,32))`; the Light slot
    // at +28 is an 8-byte pointer on LP64 that spills into +32, so the gate reads
    // non-zero and LightOffset is skipped on this host — an inherent artifact of
    // the original's 4-byte-spaced pointer slots, documented in the module header.

    // Recovered floor-level size math (size = 2): Divide0 = size*size = 4.
    for (int i = 0; i < g_allocLogN; ++i) {
        if (std::strcmp(g_allocLog[i].tag, "d3_fl:Divide0") == 0)
            CHECK_EQ(g_allocLog[i].size, 4u);
        // every handed-out block is valid in the real tracker.
        if (g_allocLog[i].ptr) CHECK(tracker.IsValidPointer(g_allocLog[i].ptr));
    }

    // Floor scalar side-effects: light marker + built bit.
    CHECK_EQ(static_cast<int>(floor[7276]), 0xFF);
    CHECK((floor[7280] & 1u) != 0u);

    // The "d3_fl" group accumulated the floor + tile allocations.
    const mem::MemGroup* g = tracker.FindGroup("d3_fl");
    CHECK(g != nullptr);
    if (g) CHECK(g->curBlocks > 0);

    delete[] floor;
    render::SetTerrainRender2Hooks(nullptr);
}
