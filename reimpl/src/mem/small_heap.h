#pragma once
#include "guild/common/types.h"
#include <cstddef>

// The deferred 3-mode CRT small-block allocator from gilde.exe — the MSVC
// "__sbh_*" small-block heap that sits behind the global heap-mode selector
// dword_1465044. This is DISTINCT from the VIBE_Memory_* boundary-tag free-list
// heap in heap.{h,cpp}: that one is the game-side region allocator; this one is
// the CRT's own three-way malloc backend.
//
// THE 3 MODES (dword_1465044, chosen once at startup by VIBE_Heap_SelectGlobalMode
// @0x142473e from __MSVCRT_HEAP_SELECT / OS version):
//   mode 1  default      -> straight HeapAlloc/HeapFree (the OS heap)
//   mode 2  segment heap  -> the 4 MiB-segment / 4 KiB-page "page heap"
//                           (VIBE_Heap_CreateSegment & friends; deferred here)
//   mode 3  small-block   -> THIS module: 1 MiB regions, 32 groups/region,
//                           64 size-class buckets/group, boundary-tagged chunks
//                           on per-bucket free lists, with two-level commit
//                           bitmaps. The classic Win9x __sbh_alloc_block design.
//
//   VIBE_Mem_HeapAlloc          @0x1421b91  (3-mode alloc dispatch)
//   VIBE_Mem_Free               @0x1421d8b  (3-mode free dispatch)
//   VIBE_Memory_Realloc         @0x142a0f8  (3-mode realloc dispatch; SB path)
//   VIBE_Heap_InitSmallBlock    @0x14248e3  (init the region table)
//   VIBE_Heap_AllocSmallBlock   @0x1424c7f  (__sbh_alloc_block)
//   VIBE_Heap_FreeSmallBlock    @0x1424956  (__sbh_free_block)
//   VIBE_Heap_ResizeSmallBlock  @0x1425134  (__sbh_resize_block)
//   VIBE_Heap_CreateRegion      @0x1424f88  (reserve a 1 MiB region + header)
//   VIBE_Heap_CommitGroup       @0x1425039  (commit one 32 KiB group, build runs)
//   VIBE_Heap_FindRegion        @0x142492b  (which region owns a pointer)
//
// PAGE / OS SOURCE (documented substitute, same model as heap.cpp): the original
// uses VirtualAlloc(MEM_RESERVE) for the 1 MiB region (dword_146779C),
// VirtualAlloc(MEM_COMMIT) per 32 KiB group, and HeapAlloc for the region's
// 16836-byte bookkeeping header (dword_14677D4). Here the reserve+commit is one
// page-aligned PageAlloc (reused from heap.cpp) and the header is a plain struct;
// see small_heap.cpp for the 1:1 mapping.
//
// 64-BIT ADAPTATION (same precedent as heap.cpp / mempool.cpp): the original
// packs 4-byte chunk links and embeds raw 32-bit pointers inside the committed
// run pages (its free-list nodes ARE addresses within the 1 MiB region). On a
// 64-bit host an 8-byte pointer will not fit the 32-bit slot, so the per-chunk
// links are widened to native pointers and the bucket list heads are kept in the
// header rather than as self-referential addresses. The region/group/bucket
// architecture, the two-level bitmaps, the size-class bucketing, the boundary
// tags and the commit/decommit accounting are all faithful; only the link slot
// width differs. RECOVERED RAW LAYOUTS (byte offsets from the originals) are
// documented on every struct field in small_heap.cpp and asserted in the tests.
namespace guild::mem {

// ---- Size-class table (recovered from AllocSmallBlock @0x1424ca5) -----------
// A request of `n` user bytes is rounded to a 16-byte-granular chunk:
//     chunk = (n + 23) & ~15        // +0x00 size word (+1 in-use) + 8 hdr, then 16-align
// and the size class is:
//     class = (chunk >> 4) - 1      // 0..63 ; clamped to 63 for the free path
// So there are 64 size classes spaced 16 bytes apart: class k holds chunks of
// (k+1)*16 bytes (payload (k+1)*16 - 8). The largest serviced small block is
// dword_146503C (== 1016 in this build; InitSmallBlock's argument), i.e. classes
// 0..62; class 63 is the catch-all "big free chunk" tail used by coalescing.
constexpr u32 kSizeClasses   = 64;       // 64 buckets per group
constexpr u32 kSizeGranule   = 16;       // chunk size granularity
constexpr u32 kSmallBlockMax = 1016;     // dword_146503C — max user bytes via SB

// ---- Region / group / run geometry (recovered) ------------------------------
constexpr u32 kRegionBytes   = 0x100000; // 1 MiB reserved per region (VirtualAlloc 0x100000)
constexpr u32 kGroupsPerRegion = 32;     // 32 groups (one bit each in the region bitmap)
constexpr u32 kGroupBytes    = 0x8000;   // 32 KiB committed per group (v3<<15 stride)
constexpr u32 kGroupRunBytes = 0x7000;   // 28 KiB usable run inside a group (v6+28672 bound)

// Round a user request to its chunk size and 16-align. The original is
// (n + 23) & ~15 = roundup(n + 4-byte-head + 4-byte-foot, 16); here the head and
// foot are widened to 8 bytes each (see small_heap.cpp), so it is
// roundup(n + 16, 16) = (n + 31) & ~15. Returns 0 if n exceeds kSmallBlockMax.
u32 SmallChunkSize(u32 userBytes);
// Size class for a chunk of `chunkBytes`: (chunkBytes >> 4) - 1, clamped to 63.
u32 SmallSizeClass(u32 chunkBytes);

// The small-block heap. Encapsulated (the original is a set of process globals:
// dword_1465038 region table base, dword_146502C cursor, dword_1465034 count,
// dword_1465024 capacity, dword_1465030/dword_1465028 the last-emptied group
// cache) so tests can spin up isolated instances.
class SmallHeap {
public:
    SmallHeap();
    ~SmallHeap();

    SmallHeap(const SmallHeap&) = delete;
    SmallHeap& operator=(const SmallHeap&) = delete;

    // VIBE_Heap_AllocSmallBlock @0x1424c7f — allocate `userBytes` (<= kSmallBlockMax).
    // Returns a 16-aligned user pointer or nullptr (too large / out of memory).
    void* Alloc(u32 userBytes);

    // VIBE_Heap_FreeSmallBlock @0x1424956 — free a pointer from Alloc, coalescing
    // with adjacent free chunks and decommitting a group that becomes wholly free.
    // Null-safe; ignores pointers it does not own.
    void Free(void* userPtr);

    // VIBE_Heap_ResizeSmallBlock @0x1425134 — try to grow/shrink in place; returns
    // true on success (pointer unchanged), false if the caller must alloc+copy.
    bool Resize(void* userPtr, u32 newUserBytes);

    // Convenience realloc mirroring VIBE_Memory_Realloc's small-block path: resize
    // in place, else alloc a new block, copy the overlap and free the old one.
    // Returns the (possibly new) user pointer or nullptr.
    void* Realloc(void* userPtr, u32 newUserBytes);

    // --- diagnostics (not in the original; for tests) ---
    std::size_t RegionCount() const;     // committed regions
    std::size_t GroupCount() const;      // committed groups across all regions
    std::size_t LiveBlocks() const;      // outstanding allocations
    std::size_t LiveBytes() const;       // sum of live payloads
    bool Validate() const;               // structural well-formedness

private:
    struct Group;
    struct Region;

    // VIBE_Heap_CreateRegion @0x1424f88 — reserve a new 1 MiB region + header.
    Region* CreateRegion();
    // VIBE_Heap_CommitGroup @0x1425039 — commit one group in `r`, build its run.
    Group*  CommitGroup(Region* r);
    // VIBE_Heap_FindRegion @0x142492b — region owning `userPtr`, or nullptr.
    Region* FindRegion(void* userPtr) const;
    // Group within `r` whose run contains `p`, or nullptr.
    static Group* GroupOf(Region* r, u8* p);
    // Per-bucket free-list maintenance (size-class circular lists + bitmap/count).
    static void BucketUnlink(Group* g, u32 cls, u8* c);
    static void BucketLink(Group* g, u32 cls, u8* c);
    static void SetNonEmpty(Group* g, u32 cls);
    static void ClearNonEmpty(Group* g, u32 cls);
    static bool TestNonEmpty(const Group* g, u32 cls);

    Region* m_regions = nullptr;   // dword_1465038 — singly linked region list
    Region* m_cursor  = nullptr;   // dword_146502C — alloc scan cursor (rover)
    u32     m_regionCount = 0;     // dword_1465034
    u32     m_maxUserBytes = kSmallBlockMax; // dword_146503C
    Group*  m_emptyCacheGroup = nullptr;     // dword_1465030 — last wholly-freed group
};

} // namespace guild::mem
