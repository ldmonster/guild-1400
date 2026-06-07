#pragma once
#include "guild/common/types.h"
#include <cstddef>

// Region-based boundary-tag free-list allocator from gilde.exe — the bottom of
// the engine's allocator stack that the debug tracker (memory_debug.*) and the
// fixed-block pools (mempool.*) are built on.
//
// This is the "VIBE_Memory_*" free-list heap (NOT the separate 3-mode CRT
// small-block allocator behind dword_1465044; see the note at the end of this
// header). The allocator manages a doubly linked list of *regions* obtained from
// the OS page allocator. Each region holds a run of boundary-tagged *chunks*
// threaded onto a per-region circular free list. It is a Doug-Lea-style design:
//   - chunk size word at +0x00, low bit = IN-USE flag (size is always a multiple
//     of 8, so the low 3 bits are free for flags; only bit 0 is used here)
//   - free chunks additionally carry bk (+0x04) and fd (+0x08) free-list links
//   - the user pointer is chunk+4 (the size word precedes the payload)
//   - a -1 sentinel size word terminates each region's run
//
//   VIBE_Memory_AllocFromFreeList       @0x5dbe70  (rounds, scans, grows)
//   VIBE_Memory_ReturnToFreeList        @0x5dbf60  (locates owning region, frees)
//   VIBE_Memory_HeapAllocBlock          @0x5fcab0  (carve a chunk from a region)
//   VIBE_Memory_HeapFreeBlock           @0x5fcb60  (coalesce + relink a chunk)
//   VIBE_Memory_RegisterHeapRegion      @0x5fcc90  (insert region, init free list)
//   VIBE_Memory_GrowHeapWithVirtualAlloc@0x5fcd08  (VirtualAlloc a new region)
//   VIBE_Memory_ComputeGrowSize         @0x5fcdd8  (round request to a page run)
//   VIBE_Heap_FreeUnusedRegions         @0x606700  (release fully-free regions)
//   VIBE_Heap_UnlinkAndFreeRegion       @0x60679c
//   VIBE_Heap_VirtualFreeRegion         @0x60673c  (VirtualFree a region)
//
// PAGE SOURCE: the original calls VirtualAlloc(MEM_COMMIT, PAGE_READWRITE) and
// VirtualFree(MEM_RELEASE). The shim model (AGENT_GUIDE) lets us back this with a
// thin internal page provider; here it is std::aligned_alloc / std::free of
// 4 KiB-aligned page runs (PageAlloc/PageFree below). The block and free-list
// logic is faithful to the original; only the raw page source differs.
namespace guild::mem {

// 4 KiB pages, matching the original's page rounding (ComputeGrowSize masks to
// 0xF000 — i.e. rounds up to a multiple of 4096).
constexpr u32 kPageSize = 0x1000;

// Minimum bytes carved out by ComputeGrowSize when the heap grows; the original
// reads this from dword_64AE6C. We expose a default but allow override for tests.
struct HeapConfig {
    u32 minGrowBytes;   // dword_64AE6C — minimum region run size before page rounding
    u32 maxRegionBytes; // dword_64A960 == -2 disables growth; here a soft cap (0 = unlimited)
};

// One global heap instance, mirroring the original's process-global region list
// (dword_64A30C head, dword_64A310 / dword_64A314 search cursor, dword_1407BA0
// last-freed region cache). State is encapsulated so tests can spin up isolated
// heaps; the engine used a single static instance.
class Heap {
public:
    Heap();
    explicit Heap(const HeapConfig& cfg);
    ~Heap();

    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

    // VIBE_Memory_AllocFromFreeList @0x5dbe70 — (eax=size) -> eax=user pointer.
    // Returns nullptr on failure (size 0 or > 0xFFFFFFD4, or out of memory).
    void* AllocFromFreeList(u32 size);

    // VIBE_Memory_ReturnToFreeList @0x5dbf60 — (eax=user pointer). Null-safe.
    void ReturnToFreeList(void* userPtr);

    // VIBE_Heap_FreeUnusedRegions @0x606700 — release every region whose entire
    // run is one free chunk back to the OS.
    void FreeUnusedRegions();

    // Diagnostics (not in the original; for tests). Number of live regions.
    std::size_t RegionCount() const;
    // Bytes currently handed out to callers (sum of in-use chunk payloads,
    // excluding the per-chunk size word).
    std::size_t LiveBytes() const;

    // Strict structural validation (tests only): every region's chunks tile the
    // run exactly to the sentinel with sane sizes and a consistent free count.
    // Returns true if the heap is well-formed.
    bool Validate() const;

private:
    struct Region; // boundary-tag region, see heap.cpp for the +offset layout

    // VIBE_Memory_RegisterHeapRegion @0x5fcc90 — link `r` into the sorted region
    // list and initialise its circular free list. Returns the run base (r+0x2C).
    u8* RegisterHeapRegion(Region* r, u32 regionSize);

    // VIBE_Memory_HeapAllocBlock @0x5fcab0 — carve a chunk of `size` from region
    // `r`. Returns user pointer (chunk+4) or nullptr.
    void* HeapAllocBlock(u32 size, Region* r);

    // VIBE_Memory_HeapFreeBlock @0x5fcb60 — return `userPtr`'s chunk to region
    // `r`, coalescing with adjacent free chunks.
    void HeapFreeBlock(void* userPtr, Region* r);

    // VIBE_Memory_ComputeGrowSize @0x5fcdd8 — round a request up to a page run.
    bool ComputeGrowSize(u32* sizeInOut) const;

    // VIBE_Memory_GrowHeapWithVirtualAlloc @0x5fcd08 — obtain a new region.
    bool GrowHeap(u32 size);

    // VIBE_Heap_UnlinkAndFreeRegion @0x60679c / VirtualFreeRegion @0x60673c.
    void UnlinkAndFreeRegion(Region* r);

    Region* FindOwningRegion(void* userPtr) const;

    // Address of a region's circular free-list anchor node (the "a2 + 8"
    // terminator). Defined in the .cpp where Region is complete.
    static u8* AnchorOf(Region* r);

    // Byte offset from a region's base to its first run chunk.
    static u32 RunOffset();

    // Largest free chunk size in a region (walks the physical run).
    static u32 RecomputeMaxFree(Region* r, u8* runStart);

    Region* m_head = nullptr;     // dword_64A30C — sorted (by address) region list head
    Region* m_cursor = nullptr;   // dword_64A310 — rover; where the last scan stopped
    u32     m_cursorMax = 0;      // dword_64A314 — best free size seen past the rover
    Region* m_lastFreed = nullptr;// dword_1407BA0 — region of the most recent free
    HeapConfig m_cfg;
};

// Page provider — the VirtualAlloc substitute (see header note). Returns a
// 4 KiB-aligned, zero-filled run of `bytes` (rounded up to a page) or nullptr.
void* PageAlloc(std::size_t bytes);
void  PageFree(void* p);

} // namespace guild::mem
