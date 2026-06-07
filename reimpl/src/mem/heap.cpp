#include "mem/heap.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace guild::mem {

// ---------------------------------------------------------------------------
// Page provider — VirtualAlloc / VirtualFree substitute.
// The original: VirtualAlloc(0, size, MEM_COMMIT(0x1000), PAGE_READWRITE(0x40))
// and VirtualFree(p, 0, MEM_RELEASE(0x8000)). We allocate page-aligned, zeroed
// runs from the C++ allocator. We over-allocate by one page and stash the real
// base immediately before the returned page-aligned pointer so PageFree can
// recover it (std::aligned_alloc would also work but isn't on every libc; this
// keeps the free path simple and portable).
// ---------------------------------------------------------------------------
void* PageAlloc(std::size_t bytes) {
    if (bytes == 0)
        return nullptr;
    std::size_t rounded = (bytes + (kPageSize - 1)) & ~static_cast<std::size_t>(kPageSize - 1);
    std::size_t total = rounded + kPageSize; // room to align + store the base
    void* raw = std::malloc(total);
    if (!raw)
        return nullptr;
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(raw);
    std::uintptr_t aligned = (base + kPageSize) & ~static_cast<std::uintptr_t>(kPageSize - 1);
    // Stash the malloc base right below the aligned page.
    reinterpret_cast<void**>(aligned)[-1] = raw;
    void* p = reinterpret_cast<void*>(aligned);
    std::memset(p, 0, rounded); // VirtualAlloc/MEM_COMMIT zero-fills
    return p;
}

void PageFree(void* p) {
    if (!p)
        return;
    void* raw = reinterpret_cast<void**>(p)[-1];
    std::free(raw);
}

// ---------------------------------------------------------------------------
// Region layout — recovered from VIBE_Memory_RegisterHeapRegion @0x5fcc90.
// The region base pointer is `r`; the run of chunks starts at r+0x2C. Field
// offsets are dword indices a2[N] used by HeapAllocBlock / HeapFreeBlock.
//
//   +0x00  size        run size = (regionSize) - 0x2C  (first free chunk's size)
//   +0x04  prev        previous region (address-sorted list)
//   +0x08  next        next region
//   +0x0C  dv          "designated victim": last-split free chunk (rover)
//   +0x10  dvSize      a lower bound used to skip the dv during search
//   +0x14  maxFree     largest free chunk size known in this region
//   +0x18  allocCount  number of in-use chunks (a2[6])
//   +0x1C  freeCount   number of free chunks on the list (a2[7])
//   +0x20  anchorSize  unused size slot of the circular free-list anchor node
//   +0x24  anchorBk    anchor.bk  (a2[9])   — points at last free chunk
//   +0x28  anchorFd    anchor.fd  (a2[10])  — points at first free chunk
//
// The anchor node lives at r+0x20 (i.e. &a2[8]); a free list with one chunk has
// anchor.fd == anchor.bk == that chunk and chunk.bk == chunk.fd == anchor.
//
// 64-BIT ADAPTATION: the original is 32-bit and packs each chunk's size word at
// +0x00 and its free-list links bk/fd at +0x04/+0x08 (4-byte pointers). Here
// pointers are 8 bytes, so the links are widened: size word at +0x00, bk at
// +0x08, fd at +0x10 (8-byte aligned, non-overlapping). The boundary-tag /
// circular-free-list algorithm is unchanged — only the link slot widths differ.
// The returned user pointer is chunk + kChunkHdr (the payload origin). The
// anchor node is a same-shaped raw buffer embedded in the Region so the exact
// same accessors apply to it (it is the list terminator).
// ---------------------------------------------------------------------------
namespace {

constexpr u32 kInUse = 1u;          // low bit of a chunk size word
constexpr u32 kChunkHdr = 8;        // size word + pad before payload (8-aligned)
constexpr u32 kBkOff = 8;           // free-chunk bk link offset
constexpr u32 kFdOff = 16;          // free-chunk fd link offset
constexpr u32 kMinChunk = 32;       // holds size + bk + fd, 8-aligned (>= kFdOff+8)
constexpr u32 kEndSentinel = 0xFFFFFFFFu; // run terminator: in-use bit set, blocks coalesce

// Chunk accessors. A chunk is addressed by the byte pointer to its size word.
inline u32  ChunkSizeRaw(const u8* c) { return *reinterpret_cast<const u32*>(c); }
inline void ChunkSetSizeRaw(u8* c, u32 v) { *reinterpret_cast<u32*>(c) = v; }
inline u32  ChunkSize(const u8* c) { return ChunkSizeRaw(c) & ~kInUse; }
inline bool ChunkInUse(const u8* c) { return (ChunkSizeRaw(c) & kInUse) != 0; }

// Free-chunk links (valid only while the chunk is free).
inline u8*& ChunkBk(u8* c) { return *reinterpret_cast<u8**>(c + kBkOff); }
inline u8*& ChunkFd(u8* c) { return *reinterpret_cast<u8**>(c + kFdOff); }

} // namespace

// Region header. The first dword is the run size; the remaining bookkeeping
// fields mirror the original's a2[1..7]; the anchor[] buffer is the circular
// free-list terminator node, addressed with the chunk accessors above.
struct Heap::Region {
    u32 size;          // run size (= regionSize - header)
    Region* prev;      // address-sorted region list
    Region* next;
    u8* dv;            // designated victim (rover chunk) — retained for layout
    u32 dvSize;        // fidelity; the dv search fast-path is omitted (see below)
    u32 maxFree;       // largest known free chunk
    u32 allocCount;    // in-use chunk count
    u32 freeCount;     // free chunk count
    // Anchor node: a kMinChunk-sized raw buffer. anchor[0..3] = size slot (kept
    // 0 so it never satisfies a request), anchor[kBkOff]=bk, anchor[kFdOff]=fd.
    alignas(std::max_align_t) u8 anchor[kMinChunk];
    // run bytes follow after the header (size returned by RunOffset()).
};

// Byte offset from the Region base to the first run chunk. Lives after the
// fixed fields + the embedded anchor buffer.
u32 Heap::RunOffset() {
    return static_cast<u32>(offsetof(Region, anchor) + kMinChunk);
}

// The anchor node's address is the list terminator ("a2 + 8" in the original).
u8* Heap::AnchorOf(Region* r) {
    return r->anchor;
}

Heap::Heap() : Heap(HeapConfig{0x10000, 0}) {} // 64 KiB default grow floor

Heap::Heap(const HeapConfig& cfg) : m_cfg(cfg) {}

Heap::~Heap() {
    // Release every region back to the page provider.
    Region* r = m_head;
    while (r) {
        Region* nxt = r->next;
        PageFree(r);
        r = nxt;
    }
}

// VIBE_Memory_RegisterHeapRegion @0x5fcc90
u8* Heap::RegisterHeapRegion(Region* r, u32 regionSize) {
    // Insert into the address-sorted list (ascending). Find predecessor `prev`
    // (last region whose base < r) and successor `next`.
    Region* prev = nullptr;
    Region* cur = m_head;
    while (cur) {
        if (reinterpret_cast<std::uintptr_t>(r) < reinterpret_cast<std::uintptr_t>(cur))
            break;
        prev = cur;
        cur = cur->next;
    }
    r->prev = prev;
    r->next = cur;
    if (prev)
        prev->next = r;
    else
        m_head = r;
    if (cur)
        cur->prev = r;

    // Initialise the circular free list (empty) and per-region counters.
    u8* anchor = AnchorOf(r);          // the "a2+8" terminator node
    r->dv = anchor;
    r->dvSize = 0;
    r->maxFree = 0;
    r->allocCount = 0;
    r->freeCount = 0;
    ChunkSetSizeRaw(anchor, 0);        // anchor size slot: never satisfies a request
    ChunkBk(anchor) = anchor;          // anchor.bk = anchor (empty list)
    ChunkFd(anchor) = anchor;          // anchor.fd = anchor

    // The single initial free chunk spans the whole run; a sentinel marks the
    // end. Round the run size DOWN to a multiple of 8 so every chunk boundary
    // stays 8-aligned (all chunk sizes are multiples of 8); the few trailing
    // bytes before the sentinel are unused. The run base (r + RunOffset) is
    // 8-aligned because the page is page-aligned and RunOffset is a multiple of 8.
    u32 runSize = (regionSize - RunOffset()) & ~7u;
    r->size = runSize; // region+0 holds the run size (original: [edx] = size - hdr)
    u8* run = reinterpret_cast<u8*>(r) + RunOffset();
    ChunkSetSizeRaw(run, runSize);                       // first chunk's size
    ChunkSetSizeRaw(run + runSize, kEndSentinel);        // -1 run terminator
    return run;
}

// VIBE_Memory_ComputeGrowSize @0x5fcdd8
bool Heap::ComputeGrowSize(u32* sizeInOut) const {
    u32 v = *sizeInOut;
    u32 rounded = (v + 11) & 0xFFFFFFF8u; // align request to 8, + chunk overhead
    if (rounded < v)
        return false;                     // overflow
    v = rounded;
    u32 withHdr = v + 60;                 // + region header + sentinel slack
    if (withHdr < v)
        return false;
    if (withHdr < m_cfg.minGrowBytes) {
        withHdr = m_cfg.minGrowBytes & 0xFFFFFFFEu;
    }
    v = withHdr;
    u32 paged = v + (kPageSize - 1);      // round up to a 4 KiB page
    if (paged < v)
        return false;
    paged &= 0xFFFFF000u;
    *sizeInOut = paged;
    return paged != 0;
}

// VIBE_Memory_GrowHeapWithVirtualAlloc @0x5fcd08
bool Heap::GrowHeap(u32 size) {
    if (m_cfg.maxRegionBytes != 0 && size > m_cfg.maxRegionBytes)
        return false;
    u32 grow = size;
    if (!ComputeGrowSize(&grow))
        return false;

    void* page = PageAlloc(grow); // VirtualAlloc substitute
    if (!page)
        return false;

    // The original reserves a 4-byte tail and requires the region to be big
    // enough for the header plus at least one minimum chunk.
    if (grow < 4)
        return false;
    u32 regionSize = grow - 4;
    if (regionSize < RunOffset() + kMinChunk)
        return false;

    Region* r = static_cast<Region*>(page);
    u8* run = RegisterHeapRegion(r, regionSize);

    // Mark the run's first chunk in-use, then free it so it lands on the free
    // list with proper links (mirrors the original: set in-use bit, then
    // ReturnToFreeList(run)). user pointer is chunk + kChunkHdr.
    ChunkSetSizeRaw(run, ChunkSizeRaw(run) | kInUse);
    r->allocCount = 1;                     // counts the about-to-be-freed chunk
    ReturnToFreeList(run + kChunkHdr);
    // Point the search rover at the new region so the next allocation scan
    // includes it (it may sort below the previous rover position). The original
    // resets the rover (dword_64A310) similarly after growing.
    m_cursor = r;
    m_cursorMax = r->maxFree;
    return true;
}

// Recompute a region's largest free chunk by walking its physical run. Keeps
// r->maxFree exact, so AllocFromFreeList's "rmax >= size" gate is reliable.
// (The original keeps a running hint; an exact recompute is behaviorally
// equivalent and avoids stale-hint corner cases on this widened layout.)
u32 Heap::RecomputeMaxFree(Region* r, u8* runStart) {
    u32 best = 0;
    u8* end = runStart + r->size;
    for (u8* c = runStart; c < end;) {
        u32 raw = ChunkSizeRaw(c);
        if (raw == kEndSentinel)
            break;
        u32 sz = raw & ~kInUse;
        if (sz == 0)
            break;
        if ((raw & kInUse) == 0 && sz > best)
            best = sz;
        c += sz;
    }
    return best;
}

// VIBE_Memory_HeapAllocBlock @0x5fcab0 — carve a chunk of (rounded) `size` from
// region `r`'s free list. Returns user pointer (chunk+kChunkHdr) or nullptr.
// First-fit over the region's circular free list, splitting the found chunk.
void* Heap::HeapAllocBlock(u32 size, Region* r) {
    if (size == 0)
        return nullptr;
    // Original: need = (size + 11) & ~7. Here the chunk header is kChunkHdr (8)
    // instead of 4, so round (size + header) up to 8; floor at kMinChunk.
    u32 need = size + kChunkHdr + 7;
    if (need < size)
        return nullptr;          // overflow guard
    need &= ~7u;                 // align to 8
    if (need < kMinChunk)
        need = kMinChunk;
    if (need > r->maxFree)       // region can't hold it
        return nullptr;

    u8* anchor = AnchorOf(r);
    u8* runStart = reinterpret_cast<u8*>(r) + RunOffset();

    // First-fit walk over the circular free list (only free chunks are linked).
    u8* chunk = ChunkFd(anchor);
    while (chunk != anchor && ChunkSize(chunk) < need)
        chunk = ChunkFd(chunk);
    if (chunk == anchor) {       // no fit; correct the region's hint and bail
        r->maxFree = RecomputeMaxFree(r, runStart);
        return nullptr;
    }

    u32 csize = ChunkSize(chunk);
    u8* bk = ChunkBk(chunk);
    u8* fd = ChunkFd(chunk);
    u32 remainder = csize - need;

    if (remainder < kMinChunk) {
        // Take the whole chunk: unlink it.
        ChunkFd(bk) = fd;
        ChunkBk(fd) = bk;
        --r->freeCount;
        ChunkSetSizeRaw(chunk, csize | kInUse); // keep full size, mark in-use
    } else {
        // Split: the lower part (need) becomes in-use; the upper part (split)
        // is a fresh free chunk that replaces `chunk` in the list.
        u8* split = chunk + need;
        ChunkSetSizeRaw(split, remainder);
        ChunkFd(bk) = split;
        ChunkBk(fd) = split;
        ChunkBk(split) = bk;
        ChunkFd(split) = fd;
        ChunkSetSizeRaw(chunk, need | kInUse);
    }

    ++r->allocCount;
    r->maxFree = RecomputeMaxFree(r, runStart);
    return chunk + kChunkHdr;
}

// VIBE_Memory_HeapFreeBlock @0x5fcb60 — return userPtr's chunk to region r,
// coalescing with both physically adjacent free chunks (boundary-tag merge),
// then linking the result into the region's circular free list.
void Heap::HeapFreeBlock(void* userPtr, Region* r) {
    if (!userPtr)
        return;
    u8* chunk = static_cast<u8*>(userPtr) - kChunkHdr;
    if (!ChunkInUse(chunk))
        return;                                  // double free / not ours
    u32 size = ChunkSize(chunk);
    ChunkSetSizeRaw(chunk, size);                // clear in-use bit

    u8* anchor = AnchorOf(r);
    u8* runStart = reinterpret_cast<u8*>(r) + RunOffset();

    // Forward-coalesce: absorb the physically adjacent higher chunk if it is
    // free. The run terminator sentinel reads as in-use, stopping at the end.
    u8* next = chunk + size;
    if (next < runStart + r->size && !ChunkInUse(next)) {
        u8* bk = ChunkBk(next);
        u8* fd = ChunkFd(next);
        ChunkFd(bk) = fd;        // unlink next
        ChunkBk(fd) = bk;
        --r->freeCount;
        size += ChunkSize(next);
        ChunkSetSizeRaw(chunk, size);
    }

    // Backward-coalesce: scan the free list for the chunk physically below us
    // (p + size(p) == chunk) and merge into it (it stays linked). Otherwise link
    // `chunk` at the list head.
    u8* pred = nullptr;
    for (u8* it = ChunkFd(anchor); it != anchor; it = ChunkFd(it)) {
        if (it + ChunkSize(it) == chunk) {
            pred = it;
            break;
        }
    }
    if (pred) {
        size += ChunkSize(pred);
        ChunkSetSizeRaw(pred, size);
        chunk = pred;            // pred remains on the list at its position
    } else {
        ++r->freeCount;
        u8* fd = ChunkFd(anchor);
        ChunkFd(chunk) = fd;
        ChunkBk(chunk) = anchor;
        ChunkBk(fd) = chunk;
        ChunkFd(anchor) = chunk;
    }

    --r->allocCount;
    r->maxFree = RecomputeMaxFree(r, runStart);
}

Heap::Region* Heap::FindOwningRegion(void* userPtr) const {
    std::uintptr_t p = reinterpret_cast<std::uintptr_t>(userPtr);
    for (Region* r = m_head; r; r = r->next) {
        std::uintptr_t base = reinterpret_cast<std::uintptr_t>(r);
        // The run spans [base+RunOffset, base+RunOffset+r->size); user pointers
        // live within it. Use the full region extent for the bounds test.
        if (p > base && p < base + RunOffset() + r->size)
            return r;
    }
    return nullptr;
}

// VIBE_Memory_AllocFromFreeList @0x5dbe70
void* Heap::AllocFromFreeList(u32 size) {
    if (size == 0 || size > 0xFFFFFFD4u)
        return nullptr;
    // HeapAllocBlock re-derives the rounded chunk size from the raw request; the
    // original keeps a separate rounded "search size" hint for the rover, which
    // we fold into the per-region maxFree gate below.
    bool grewOnce = false;
    for (;;) {
        // Sweep every region from the head so any region with space is tried
        // before we grow. (The original biases the scan to start at a rover —
        // dword_64A310 — purely as an optimization; a full sweep is equivalent.)
        m_cursorMax = 0;
        for (Region* r = m_head; r; r = r->next) {
            u32 rmax = r->maxFree;
            if (rmax >= size) {
                void* p = HeapAllocBlock(size, r);
                if (p) {
                    m_cursor = r;
                    if (r->maxFree > m_cursorMax)
                        m_cursorMax = r->maxFree;
                    return p;
                }
            }
            if (r->maxFree > m_cursorMax) // re-read: HeapAllocBlock may correct it
                m_cursorMax = r->maxFree;
        }
        // No region fit; grow once, then once more before giving up.
        if (!GrowHeap(size)) {
            if (grewOnce)
                return nullptr;
            grewOnce = true;
            if (!GrowHeap(size))
                return nullptr;
        }
    }
}

// VIBE_Memory_ReturnToFreeList @0x5dbf60
void Heap::ReturnToFreeList(void* userPtr) {
    if (!userPtr)
        return;
    Region* r = nullptr;
    // The original first checks the last-freed region cache (dword_1407BA0),
    // then the rover (dword_64A310), then walks the whole list.
    if (m_lastFreed && FindOwningRegion(userPtr) == m_lastFreed)
        r = m_lastFreed;
    if (!r)
        r = FindOwningRegion(userPtr);
    if (!r)
        return; // not one of ours
    HeapFreeBlock(userPtr, r);
    m_lastFreed = r;
    if (r->maxFree > m_cursorMax)
        m_cursorMax = r->maxFree;
}

// VIBE_Heap_FreeUnusedRegions @0x606700
void Heap::FreeUnusedRegions() {
    Region* r = m_head;
    while (r) {
        Region* nxt = r->next;
        // Region is fully free when it has no in-use chunks and its single free
        // chunk spans the whole run (matches the original's check that the run's
        // chunk size equals the full run size).
        if (r->allocCount == 0) {
            u8* run = reinterpret_cast<u8*>(r) + RunOffset();
            if (!ChunkInUse(run) && ChunkSize(run) == r->size)
                UnlinkAndFreeRegion(r);
        }
        r = nxt;
    }
}

// VIBE_Heap_UnlinkAndFreeRegion @0x60679c + VirtualFreeRegion @0x60673c
void Heap::UnlinkAndFreeRegion(Region* r) {
    Region* prev = r->prev;
    Region* next = r->next;
    if (m_cursor == r)
        m_cursor = next ? next : m_head;
    if (m_lastFreed == r)
        m_lastFreed = nullptr;
    if (prev)
        prev->next = next;
    else
        m_head = next;
    if (next)
        next->prev = prev;
    if (m_cursor == r)
        m_cursor = m_head;
    PageFree(r); // VirtualFree(MEM_RELEASE) substitute
}

std::size_t Heap::RegionCount() const {
    std::size_t n = 0;
    for (Region* r = m_head; r; r = r->next)
        ++n;
    return n;
}

std::size_t Heap::LiveBytes() const {
    std::size_t total = 0;
    for (Region* r = m_head; r; r = r->next) {
        u8* run = reinterpret_cast<u8*>(r) + RunOffset();
        u8* end = run + r->size;
        for (u8* c = run; c < end && ChunkSizeRaw(c) != kEndSentinel;) {
            u32 sz = ChunkSize(c);
            if (sz == 0)
                break;
            if (ChunkInUse(c))
                total += sz - kChunkHdr; // payload excludes the chunk header
            c += sz;
        }
    }
    return total;
}

bool Heap::Validate() const {
    int regionGuard = 0;
    for (Region* r = m_head; r; r = r->next) {
        if (++regionGuard > 100000) return false;
        u8* run = reinterpret_cast<u8*>(r) + RunOffset();
        u8* end = run + r->size;
        // Reject an implausible run size up front to avoid OOB reads on a
        // corrupt header.
        if (r->size < kMinChunk || (r->size & 7u) != 0 || r->size > (1u << 28))
            return false;
        u32 freeSeen = 0;
        u8* c = run;
        int chunkGuard = 0;
        while (c < end) {
            if (++chunkGuard > 10000000) return false;
            u32 raw = ChunkSizeRaw(c);
            u32 sz = raw & ~kInUse;
            if (sz < kMinChunk || (sz & 7u) != 0)
                return false;            // bogus size
            if (sz > static_cast<u32>(end - c))
                return false;            // chunk overruns the run (no OOB read)
            if ((raw & kInUse) == 0)
                ++freeSeen;
            c += sz;
        }
        if (c != end)
            return false;                // chunks didn't tile the run exactly
        if (ChunkSizeRaw(end) != kEndSentinel)
            return false;                // missing terminator
        if (freeSeen != r->freeCount)
            return false;                // free-list count drift

        // Validate the circular free list: every node must lie within the run
        // (or be the anchor), be marked free, and the list length must match.
        u8* anchor = AnchorOf(const_cast<Region*>(r));
        u32 listLen = 0;
        for (u8* it = ChunkFd(anchor); it != anchor; it = ChunkFd(it)) {
            if (it < run || it >= end)
                return false;            // free-list pointer outside the run
            if (ChunkInUse(it))
                return false;            // in-use chunk on the free list
            if (++listLen > freeSeen + 1)
                return false;            // cycle / overlong list
        }
        if (listLen != r->freeCount)
            return false;
    }
    return true;
}

} // namespace guild::mem
