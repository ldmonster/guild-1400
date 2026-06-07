#include "mem/small_heap.h"
#include "mem/heap.h"   // PageAlloc / PageFree / kPageSize — REUSED (ODR), see note

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace guild::mem {

// ===========================================================================
// RECOVERED RAW LAYOUTS (gilde.exe, 32-bit). These are the byte-exact structures
// the original packs into its region table, header block and committed runs. We
// reproduce them as named C++ structs (with the documented 64-bit link widening,
// see the header note); the offset comments below are the original ones.
//
// REGION descriptor — 20 bytes (5 dwords), array based at dword_1465038, indexed
// `dword_1465038 + 20*i`. Recovered from AllocSmallBlock / CreateRegion:
//   +0x00  bitmap0   i[0]  — "group has a class<32 free chunk" bitmap, 1 bit/group
//   +0x04  bitmap1   i[1]  — "group has a class>=32 free chunk" bitmap, 1 bit/group
//   +0x08  uncommit  i[2]  — "group not yet committed" bitmap (1=free slot)
//   +0x0C  reserve   i[3]  — VirtualAlloc(MEM_RESERVE,0x100000) base of the region
//   +0x10  header    i[4]  — HeapAlloc'd 16836-byte bookkeeping block (see below)
//
// HEADER block — 16836 bytes (HeapAlloc dword_14677D4(heap,8,16836)). Recovered
// from CommitGroup / AllocSmallBlock / FreeSmallBlock dword indices on i[4]==v8:
//   +0x00  curGroup    *v8        — group index of the most-recently-touched group
//   +0x43  commitCnt   v1+67      — number of committed groups (byte)
//   +0x44  gbm0[32]    v1+4*g+68  — per-group class<32  "bucket non-empty" bitmaps
//   +0xC4  gbm1[32]    v1+4*g+196 — per-group class>=32 "bucket non-empty" bitmaps
//   +0x144 groups[32]  v1+516*g+324 — 32 group blocks, 516 bytes each:
//        each group block: 64 bucket list-heads (8 bytes each = 512 bytes) + a
//        64-element byte array of per-bucket chunk counts (v8 + class + 4) folded
//        into the same header (the original overlays the count bytes at v8+class+4
//        and the list heads at 516*g+324 + 8*class). The run anchors live at
//        516*g + v1 + 828/832/836.
//
// We keep the SAME logical fields but as typed members; the run chunks carry
// native-width links (64-bit adaptation).
// ===========================================================================

namespace {

// Chunk in a committed run. Boundary-tagged: a size word precedes the payload and
// an identical word follows it, so neighbours can be located. The original stores
// (size|1) in-use and (size) free, with the size being the 16-granular chunk size;
// the low bit is the in-use flag. We keep that exact convention.
constexpr u32 kInUse = 1u;          // low bit of a chunk size word (original)
// Smallest physically usable chunk. A FREE chunk must hold its head size word
// (+0), the two native-width free-list links (bk@+8, fd@+16..24) and the 8-byte
// trailing footer (+size-8); that needs >= 32 bytes once the links are widened to
// 64-bit (head8 | bk8@8 | fd8@16 | foot8@24 == 32). The original packed 4-byte
// links into a 16-byte class-0 chunk. The size-class TABLE stays exact
// (16-granular, see header); only the allocation floor rises to 32, so the
// smallest serviced chunk is class 1 (32 B) not class 0 (16 B). Documented 64-bit
// consequence, same precedent as heap.cpp's kMinChunk=32.
constexpr u32 kMinChunk = 32;

inline u32  CSizeRaw(const u8* c)        { return *reinterpret_cast<const u32*>(c); }
inline void CSetSizeRaw(u8* c, u32 v)    { *reinterpret_cast<u32*>(c) = v; }
inline u32  CSize(const u8* c)           { return CSizeRaw(c) & ~kInUse; }
inline bool CInUse(const u8* c)          { return (CSizeRaw(c) & kInUse) != 0; }

// Boundary-tag overhead. The original packs a 4-byte head size word at +0 and a
// 4-byte footer (copy of the size word, in-use bit included) at +size-4, so the
// previous chunk's in-use state can be read from `*(chunk-4)` during a backward
// coalesce (overhead 8, user payload = size-8, user pointer = chunk+4). Here the
// head and footer are BOTH widened to 8 bytes so the native-width free-list links
// (bk@+8, fd@+16) never collide with the footer in a free chunk, and the user
// payload region [+8 .. size-8) never overlaps either tag. User pointer = chunk+8,
// usable payload = size - 16. (Documented 64-bit widening; the size-class table
// and (size>>4)-1 bucketing are unchanged — only the per-chunk overhead grows.)
constexpr u32 kFrontHdr = 8;   // head size word + pad (user pointer = chunk+8)
constexpr u32 kFootTag  = 8;   // trailing size-word copy at chunk+size-8
constexpr u32 kChunkOverhead = kFrontHdr + kFootTag; // 16

// Write the trailing footer = exact copy of the head size word (in-use bit
// included), at chunk + size - kFootTag.
inline void CSyncTag(u8* c) {
    u32 raw = CSizeRaw(c);
    u32 sz = raw & ~kInUse;
    *reinterpret_cast<u32*>(c + sz - kFootTag) = raw;
}
// Read the footer of the physically previous chunk (the word at chunk-kFootTag).
inline u32 CPrevFooter(const u8* c) { return *reinterpret_cast<const u32*>(c - kFootTag); }

// Free-list links (widened to native pointers, see header note): bk@+8, fd@+16.
// The same accessors apply to real chunks and to the anchor terminator node.
inline u8*& LinkBk(u8* c) { return *reinterpret_cast<u8**>(c + 8); }
inline u8*& LinkFd(u8* c) { return *reinterpret_cast<u8**>(c + 16); }

} // namespace

// One committed 32 KiB group: a run of boundary-tagged chunks threaded onto 64
// per-size-class circular free lists, with a non-empty bitmap and per-class counts.
struct SmallHeap::Group {
    // Per-class circular free list. The anchor is a chunk-SHAPED terminator node
    // so the SAME +8(bk)/+16(fd) link accessors apply to it and to real chunks
    // (mirrors the original, whose list heads at 516*g+324+8*class are addressed
    // exactly like in-run chunks). `slot[0..3]` is an unused size word kept 0 so
    // the anchor can never satisfy a search; slot[8]=bk, slot[16]=fd. The free
    // count is held alongside (original: per-class byte at v8+class+4).
    struct Bucket {
        alignas(std::max_align_t) u8 node[24]; // size(+0) | bk(+8) | fd(+16)
        u32 count;                              // free-chunk count in this bucket
    };
    Bucket buckets[kSizeClasses];   // +0x144+516*g region of the header (recovered)
    // Two-level non-empty bitmaps, faithful to the original's pair of dwords
    // (gbm0 = classes 0..31 at v8[g+17], gbm1 = classes 32..63 at v8[g+49]). Bit
    // for class c: gbm0 uses (0x80000000 >> c); gbm1 uses (0x80000000 >> (c-32)).
    u32    gbm0;                    // classes 0..31  non-empty bitmap
    u32    gbm1;                    // classes 32..63 non-empty bitmap
    u32    liveChunks;              // in-use chunks in this group (decommit when 0)
    Region* region;                 // owning region (back-pointer; not in original)
    u8*    run;                     // base of the committed run (original v6+12)
    u32    runBytes;                // usable run length (== kGroupRunBytes)
    u8*    page;                    // page-aligned backing alloc (for PageFree)
    bool   committed;               // false until CommitGroup runs

    u8* anchor(u32 cls) { return buckets[cls].node; }
};

// One 1 MiB region: a fixed array of kGroupsPerRegion group slots plus the two
// region-level bitmaps. Mirrors the 20-byte region descriptor + 16836 header.
struct SmallHeap::Region {
    u32     bitmap0;                // +0x00 — group has a free chunk (any class)
    u32     uncommitted;           // +0x08 — bit g set => group g not committed
    Region* next;                  // singly linked region list
    Group   groups[kGroupsPerRegion]; // the header's 32 group blocks
};

// --- size-class helpers (recovered, see header) ---
u32 SmallChunkSize(u32 userBytes) {
    // roundup(n + 16, 16) = (n + 31) & ~15 (widened from the original's (n+23)&~15;
    // see header). The original only enters the SB path for n <= 1016. Reject
    // oversize so (chunk>>4)-1 stays a valid class.
    if (userBytes > kSmallBlockMax)
        return 0;
    u32 chunk = (userBytes + kChunkOverhead + (kSizeGranule - 1)) &
                ~static_cast<u32>(kSizeGranule - 1);
    return chunk;
}

u32 SmallSizeClass(u32 chunkBytes) {
    u32 cls = (chunkBytes >> 4) - 1;
    if (cls > kSizeClasses - 1)
        cls = kSizeClasses - 1;   // clamp to 63 (original: `if (v>0x3F) v=63`)
    return cls;
}

SmallHeap::SmallHeap() = default;

SmallHeap::~SmallHeap() {
    Region* r = m_regions;
    while (r) {
        Region* nxt = r->next;
        // Free every committed group's run, then the region header itself.
        for (u32 g = 0; g < kGroupsPerRegion; ++g)
            if (r->groups[g].committed && r->groups[g].page)
                PageFree(r->groups[g].page);
        std::free(r);
        r = nxt;
    }
}

// VIBE_Heap_CreateRegion @0x1424f88 — reserve a new 1 MiB region and its header.
// Original: VirtualAlloc(MEM_RESERVE,0x100000) for i[3], HeapAlloc(16836) for the
// header i[4]; mark all 32 groups uncommitted (i[2] = full), clear the bitmaps.
// Substitute: one zeroed struct (the reserve is lazily committed per group).
SmallHeap::Region* SmallHeap::CreateRegion() {
    Region* r = static_cast<Region*>(std::malloc(sizeof(Region)));
    if (!r)
        return nullptr;
    std::memset(r, 0, sizeof(Region));
    r->bitmap0 = 0;
    r->uncommitted = 0xFFFFFFFFu;     // i[2] = -1: all 32 groups free/uncommitted
    r->next = m_regions;
    m_regions = r;
    ++m_regionCount;
    for (u32 g = 0; g < kGroupsPerRegion; ++g)
        r->groups[g].region = r;
    return r;
}

// VIBE_Heap_CommitGroup @0x1425039 — commit one uncommitted group of `r`, lay down
// its run as a single free chunk spanning kGroupRunBytes, init the 64 empty bucket
// anchors, and clear the group's uncommitted bit. Returns the group or nullptr.
// Original: picks the highest uncommitted bit (v3 = bsr(i[2])), VirtualAlloc-commits
// (v3<<15)+reserve for 0x8000 bytes, writes the 4080-byte run-chunk template into
// every 4 KiB page, then links the run into bucket-class boundary list. Here the
// run is one PageAlloc'd block carved as a boundary-tagged free chunk.
SmallHeap::Group* SmallHeap::CommitGroup(Region* r) {
    // Find the highest-index uncommitted group (original scans i[2] from the top
    // bit; the exact index only matters for the region bitmap bit position).
    if (r->uncommitted == 0)
        return nullptr;             // region full
    u32 g = 0;
    {
        u32 bits = r->uncommitted;
        // bsr: index of the most-significant set bit (original: shl until sign).
        u32 idx = 0;
        for (u32 b = 0; b < 32; ++b)
            if (bits & (0x80000000u >> b)) { idx = b; break; }
        g = idx;
    }
    Group* grp = &r->groups[g];

    // Commit one group: a single page-aligned run of kGroupRunBytes usable bytes
    // plus a trailing sentinel word. (VirtualAlloc MEM_COMMIT substitute.)
    u8* page = static_cast<u8*>(PageAlloc(kGroupBytes));
    if (!page)
        return nullptr;
    grp->page = page;
    grp->run = page;
    grp->runBytes = kGroupRunBytes & ~static_cast<u32>(kSizeGranule - 1);
    grp->committed = true;
    grp->liveChunks = 0;
    grp->gbm0 = 0;
    grp->gbm1 = 0;

    // Empty all 64 bucket anchors (anchor.fd = anchor.bk = anchor; count 0). The
    // anchor's size slot is kept 0 so it can never satisfy a search.
    for (u32 c = 0; c < kSizeClasses; ++c) {
        u8* a = grp->anchor(c);
        CSetSizeRaw(a, 0);
        LinkBk(a) = a;
        LinkFd(a) = a;
        grp->buckets[c].count = 0;
    }

    // The whole run starts as one free chunk; a -1 sentinel terminates it (reads
    // as in-use so coalescing stops at the run end, like the original's run tail).
    u8* run = grp->run;
    u32 runSize = grp->runBytes;
    CSetSizeRaw(run, runSize);
    CSyncTag(run);
    CSetSizeRaw(run + runSize, 0xFFFFFFFFu);  // sentinel
    // Link it into its size class (clamped to 63, the catch-all big-chunk bucket).
    u32 cls = SmallSizeClass(runSize);
    BucketLink(grp, cls, run);

    r->uncommitted &= ~(0x80000000u >> g);    // a1[2] &= ~(0x80000000 >> v3)
    r->bitmap0 |= (0x80000000u >> g);         // group now has free space
    return grp;
}

// Two-level non-empty bitmap accessors for a group, faithful to the original's
// pair of dwords (classes 0..31 / 32..63). `cls` is 0..63.
void SmallHeap::SetNonEmpty(Group* g, u32 cls) {
    if (cls < 32) g->gbm0 |= (0x80000000u >> cls);
    else          g->gbm1 |= (0x80000000u >> (cls - 32));
}
void SmallHeap::ClearNonEmpty(Group* g, u32 cls) {
    if (cls < 32) g->gbm0 &= ~(0x80000000u >> cls);
    else          g->gbm1 &= ~(0x80000000u >> (cls - 32));
}
bool SmallHeap::TestNonEmpty(const Group* g, u32 cls) {
    if (cls < 32) return (g->gbm0 & (0x80000000u >> cls)) != 0;
    return (g->gbm1 & (0x80000000u >> (cls - 32))) != 0;
}

// Unlink free chunk `c` from its bucket; update bitmap/count when it empties.
// The anchor node and chunks share the same +8(bk)/+16(fd) link layout, so a
// single splice handles both (the original's circular-list unlink: bk.fd = fd;
// fd.bk = bk).
void SmallHeap::BucketUnlink(Group* g, u32 cls, u8* c) {
    u8* bk = LinkBk(c);
    u8* fd = LinkFd(c);
    LinkFd(bk) = fd;
    LinkBk(fd) = bk;
    if (--g->buckets[cls].count == 0)
        ClearNonEmpty(g, cls);
}

// Link free chunk `c` of `cls` at the head of its bucket (original: insert after
// the anchor). bitmap/count maintained.
void SmallHeap::BucketLink(Group* g, u32 cls, u8* c) {
    u8* a = g->anchor(cls);
    u8* fd = LinkFd(a);     // current first free chunk (or anchor when empty)
    LinkFd(c) = fd;
    LinkBk(c) = a;
    LinkBk(fd) = c;
    LinkFd(a) = c;
    if (g->buckets[cls].count++ == 0)
        SetNonEmpty(g, cls);
}

// VIBE_Heap_AllocSmallBlock @0x1424c7f — first-fit over the cursor region's groups,
// committing a fresh group (or region) when none fit, then carving/splitting the
// best chunk and recording it in-use. Faithful to the original control flow:
//   - round request to a chunk (need); compute the class mask (v2/v30 in the orig)
//   - scan groups for one whose non-empty bitmap intersects the mask
//   - within the group, find the lowest sufficient class, take its first chunk,
//     split off the remainder into the remainder's class, mark the chunk in-use.
void* SmallHeap::Alloc(u32 userBytes) {
    u32 need = SmallChunkSize(userBytes);
    if (need == 0)
        return nullptr;
    if (need < kMinChunk)
        need = kMinChunk;
    u32 wantClass = SmallSizeClass(need);

    // Find a group with a free chunk of class >= wantClass. The original encodes
    // "class >= wantClass" as a two-word bitmap mask (0xFFFFFFFF >> wantClass over
    // gbm0/gbm1, AllocSmallBlock's v2/v30) tested against each group's non-empty
    // bitmap; we scan classes directly (behaviour-identical: a set non-empty bit
    // at any class c >= wantClass means a sufficient free chunk exists there).
    for (int pass = 0; pass < 3; ++pass) {
        // Sweep every committed group, starting at the cursor region (rover).
        Region* start = m_cursor ? m_cursor : m_regions;
        for (Region* r = start; r; r = r->next) {
            for (u32 gi = 0; gi < kGroupsPerRegion; ++gi) {
                Group* g = &r->groups[gi];
                if (!g->committed)
                    continue;
                // Lowest class >= wantClass that is non-empty in this group. For
                // a class < 63 every chunk in it is >= need, so the first one fits.
                // Class 63 is the catch-all "big" bucket: its chunks are >= 1024
                // but NOT size-homogeneous, so we first-fit within it for a chunk
                // of csize >= need (the original likewise scans the big bucket).
                u32 cls = wantClass;
                while (cls < kSizeClasses && !TestNonEmpty(g, cls))
                    ++cls;
                if (cls >= kSizeClasses)
                    continue;       // no sufficient chunk here

                u8* chunk = nullptr;
                u32 csize = 0;
                if (cls < kSizeClasses - 1) {
                    chunk = LinkFd(g->anchor(cls));   // first chunk; guaranteed >= need
                    csize = CSize(chunk);
                } else {
                    // Catch-all bucket: first-fit for csize >= need.
                    u8* a = g->anchor(cls);
                    for (u8* it = LinkFd(a); it != a; it = LinkFd(it)) {
                        if (CSize(it) >= need) { chunk = it; csize = CSize(it); break; }
                    }
                    if (!chunk)
                        continue;     // no big-enough chunk in this group
                }
                BucketUnlink(g, cls, chunk);

                u32 remainder = csize - need;
                if (remainder >= kMinChunk) {
                    // Split: lower `need` becomes in-use, upper becomes free.
                    u8* split = chunk + need;
                    CSetSizeRaw(split, remainder);
                    CSyncTag(split);
                    u32 rcls = SmallSizeClass(remainder);
                    BucketLink(g, rcls, split);
                    CSetSizeRaw(chunk, need | kInUse);
                    CSyncTag(chunk);
                } else {
                    // Take whole chunk (no usable remainder).
                    CSetSizeRaw(chunk, csize | kInUse);
                    CSyncTag(chunk);
                }

                ++g->liveChunks;
                m_cursor = r;
                // If we just consumed the empty-group decommit cache, clear it.
                if (m_emptyCacheGroup == g)
                    m_emptyCacheGroup = nullptr;
                return chunk + kFrontHdr;
            }
        }
        // No fit: commit another group (in an existing region or a new one).
        bool committed = false;
        for (Region* r = m_regions; r && !committed; r = r->next) {
            if (r->uncommitted != 0) {
                if (CommitGroup(r)) { m_cursor = r; committed = true; }
            }
        }
        if (!committed) {
            Region* r = CreateRegion();
            if (!r || !CommitGroup(r))
                return nullptr;
            m_cursor = r;
        }
    }
    return nullptr;
}

// VIBE_Heap_FindRegion @0x142492b — region whose 1 MiB reserve owns `userPtr`.
// Original: linear scan of the region table, testing (p - i[3]) < 0x100000. Here
// we test the committed-group runs directly (the reserve is per-group in the
// substitute), which is the same ownership predicate.
SmallHeap::Region* SmallHeap::FindRegion(void* userPtr) const {
    u8* p = static_cast<u8*>(userPtr);
    for (Region* r = m_regions; r; r = r->next)
        for (u32 g = 0; g < kGroupsPerRegion; ++g) {
            const Group& grp = r->groups[g];
            if (grp.committed && p > grp.run && p < grp.run + grp.runBytes)
                return r;
        }
    return nullptr;
}

// VIBE_Heap_FreeSmallBlock @0x1424956 — boundary-tag free: clear the in-use bit,
// coalesce with the physically next and previous chunks if free, relink the
// merged chunk into its size class, and if the group becomes wholly free push it
// to the decommit cache (and decommit the previously cached group). Faithful.
void SmallHeap::Free(void* userPtr) {
    if (!userPtr)
        return;
    Region* r = FindRegion(userPtr);
    if (!r)
        return;
    u8* p = static_cast<u8*>(userPtr);
    Group* g = SmallHeap::GroupOf(r, p);
    if (!g)
        return;

    u8* chunk = p - kFrontHdr;
    if (!CInUse(chunk))
        return;                          // double free
    u32 size = CSize(chunk);

    u8* runEnd = g->run + g->runBytes;

    // Forward-coalesce with the physically adjacent higher chunk if free.
    u8* next = chunk + size;
    if (next < runEnd && !CInUse(next)) {
        u32 nsz = CSize(next);
        BucketUnlink(g, SmallSizeClass(nsz), next);
        size += nsz;
    }
    // Backward-coalesce with the physically adjacent lower chunk if free. Its
    // trailing boundary tag sits at chunk-4 (original v22 = *(a2-8)).
    if (chunk > g->run) {
        u32 prevFooter = CPrevFooter(chunk);     // *(chunk - kFootTag)
        if ((prevFooter & kInUse) == 0) {
            u32 prevSize = prevFooter;           // footer of a free chunk = its size
            u8* prev = chunk - prevSize;
            BucketUnlink(g, SmallSizeClass(prevSize), prev);
            size += prevSize;
            chunk = prev;
        }
    }

    // Write the merged free chunk and relink it.
    CSetSizeRaw(chunk, size);            // in-use bit cleared
    CSyncTag(chunk);
    BucketLink(g, SmallSizeClass(size), chunk);
    --g->liveChunks;

    // If the group is now wholly free (one chunk spanning the whole run), recycle
    // it: decommit the previously cached empty group, cache this one. (Original
    // dword_1465030/dword_1465028 deferred-decommit + VirtualFree(MEM_DECOMMIT).)
    if (g->liveChunks == 0) {
        if (m_emptyCacheGroup && m_emptyCacheGroup != g) {
            Group* old = m_emptyCacheGroup;
            Region* orr = old->region;
            // Decommit: release the run and mark the group uncommitted again.
            for (u32 i = 0; i < kGroupsPerRegion; ++i) {
                if (&orr->groups[i] == old) {
                    orr->uncommitted |= (0x80000000u >> i);
                    orr->bitmap0 &= ~(0x80000000u >> i);
                    break;
                }
            }
            if (old->page) PageFree(old->page);
            old->page = nullptr;
            old->run = nullptr;
            old->committed = false;
            old->gbm0 = 0;
            old->gbm1 = 0;
            if (m_cursor == orr && !orr->uncommitted) { /* keep cursor */ }
        }
        m_emptyCacheGroup = g;
    }
}

SmallHeap::Group* SmallHeap::GroupOf(Region* r, u8* p) {
    for (u32 i = 0; i < kGroupsPerRegion; ++i) {
        Group* g = &r->groups[i];
        if (g->committed && p >= g->run && p < g->run + g->runBytes)
            return g;
    }
    return nullptr;
}

// VIBE_Heap_ResizeSmallBlock @0x1425134 — grow/shrink in place. Shrink always
// succeeds (split off the tail as a free chunk, coalescing with the next). Grow
// succeeds only if the physically next chunk is free and big enough to absorb the
// delta; otherwise return false so the caller alloc+copies. Faithful to the
// original's two branches (v4<=v6 shrink path, v4>v6 grow path).
bool SmallHeap::Resize(void* userPtr, u32 newUserBytes) {
    if (!userPtr)
        return false;
    Region* r = FindRegion(userPtr);
    if (!r)
        return false;
    u8* p = static_cast<u8*>(userPtr);
    Group* g = GroupOf(r, p);
    if (!g)
        return false;

    u32 need = SmallChunkSize(newUserBytes);
    if (need == 0)
        return false;
    if (need < kMinChunk)
        need = kMinChunk;

    u8* chunk = p - kFrontHdr;
    if (!CInUse(chunk))
        return false;
    u32 cur = CSize(chunk);
    u8* runEnd = g->run + g->runBytes;

    if (need <= cur) {
        // Shrink. Split off the tail (>= kMinChunk) as a free chunk; if the next
        // chunk is free, coalesce the freed tail into it.
        u32 rem = cur - need;
        if (rem < kMinChunk)
            return true;             // nothing worth splitting; keep as-is
        CSetSizeRaw(chunk, need | kInUse);
        CSyncTag(chunk);
        u8* tail = chunk + need;
        u32 tailSize = rem;
        u8* next = chunk + cur;
        if (next < runEnd && !CInUse(next)) {
            u32 nsz = CSize(next);
            BucketUnlink(g, SmallSizeClass(nsz), next);
            tailSize += nsz;
        }
        CSetSizeRaw(tail, tailSize);
        CSyncTag(tail);
        BucketLink(g, SmallSizeClass(tailSize), tail);
        return true;
    }

    // Grow. Need the physically next chunk to be free and span the deficit.
    u8* next = chunk + cur;
    if (next >= runEnd || CInUse(next))
        return false;
    u32 nsz = CSize(next);
    if (cur + nsz < need)
        return false;
    BucketUnlink(g, SmallSizeClass(nsz), next);
    u32 total = cur + nsz;
    u32 rem = total - need;
    if (rem >= kMinChunk) {
        CSetSizeRaw(chunk, need | kInUse);
        CSyncTag(chunk);
        u8* tail = chunk + need;
        CSetSizeRaw(tail, rem);
        CSyncTag(tail);
        BucketLink(g, SmallSizeClass(rem), tail);
    } else {
        CSetSizeRaw(chunk, total | kInUse);
        CSyncTag(chunk);
    }
    return true;
}

// Mirrors VIBE_Memory_Realloc's small-block path (resize in place; else alloc,
// copy the min(old,new) payload, free the old).
void* SmallHeap::Realloc(void* userPtr, u32 newUserBytes) {
    if (!userPtr)
        return Alloc(newUserBytes);
    if (newUserBytes == 0) {
        Free(userPtr);
        return nullptr;
    }
    if (Resize(userPtr, newUserBytes))
        return userPtr;
    void* np = Alloc(newUserBytes);
    if (!np)
        return nullptr;
    // Copy the overlap (original: min(oldPayload, new)).
    u8* oc = static_cast<u8*>(userPtr) - kFrontHdr;
    u32 oldPayload = CSize(oc) - kChunkOverhead;
    u32 copy = oldPayload < newUserBytes ? oldPayload : newUserBytes;
    std::memcpy(np, userPtr, copy);
    Free(userPtr);
    return np;
}

// ----------------------------- diagnostics ---------------------------------
std::size_t SmallHeap::RegionCount() const { return m_regionCount; }

std::size_t SmallHeap::GroupCount() const {
    std::size_t n = 0;
    for (Region* r = m_regions; r; r = r->next)
        for (u32 g = 0; g < kGroupsPerRegion; ++g)
            if (r->groups[g].committed)
                ++n;
    return n;
}

std::size_t SmallHeap::LiveBlocks() const {
    std::size_t n = 0;
    for (Region* r = m_regions; r; r = r->next)
        for (u32 g = 0; g < kGroupsPerRegion; ++g)
            if (r->groups[g].committed)
                n += r->groups[g].liveChunks;
    return n;
}

std::size_t SmallHeap::LiveBytes() const {
    std::size_t total = 0;
    for (Region* r = m_regions; r; r = r->next)
        for (u32 gi = 0; gi < kGroupsPerRegion; ++gi) {
            const Group& g = r->groups[gi];
            if (!g.committed)
                continue;
            u8* end = g.run + g.runBytes;
            for (u8* c = g.run; c < end;) {
                u32 raw = CSizeRaw(c);
                if (raw == 0xFFFFFFFFu)
                    break;
                u32 sz = raw & ~kInUse;
                if (sz < kMinChunk)
                    break;
                if (raw & kInUse)
                    total += sz - kChunkOverhead;
                c += sz;
            }
        }
    return total;
}

bool SmallHeap::Validate() const {
    for (Region* r = m_regions; r; r = r->next) {
        for (u32 gi = 0; gi < kGroupsPerRegion; ++gi) {
            const Group& g = r->groups[gi];
            if (!g.committed)
                continue;
            u8* end = g.run + g.runBytes;
            u32 freeSeen[kSizeClasses] = {0};
            u32 live = 0;
            u8* c = g.run;
            int guard = 0;
            while (c < end) {
                if (++guard > 4000000) return false;
                u32 raw = CSizeRaw(c);
                u32 sz = raw & ~kInUse;
                if (sz < kMinChunk || (sz & (kSizeGranule - 1)) != 0)
                    return false;
                if (sz > static_cast<u32>(end - c))
                    return false;
                // trailing footer must mirror the head size word EXACTLY, in-use
                // bit included (CSyncTag copies the raw word to chunk+size-kFootTag).
                if (*reinterpret_cast<u32*>(c + sz - kFootTag) != raw)
                    return false;
                if (raw & kInUse) {
                    ++live;
                } else {
                    ++freeSeen[SmallSizeClass(sz)];
                }
                c += sz;
            }
            if (c != end)
                return false;
            if (CSizeRaw(end) != 0xFFFFFFFFu)
                return false;
            if (live != g.liveChunks)
                return false;
            // Each bucket's linked count must match the physical free count, and
            // the non-empty bitmap must agree.
            for (u32 cls = 0; cls < kSizeClasses; ++cls) {
                u32 listLen = 0;
                u8* a = const_cast<Group&>(g).anchor(cls);
                for (u8* it = LinkFd(a); it != a; it = LinkFd(it)) {
                    if (it < g.run || it >= end) return false;
                    if (CInUse(it)) return false;
                    if (++listLen > freeSeen[cls] + 1) return false;
                }
                if (listLen != g.buckets[cls].count) return false;
                if (listLen != freeSeen[cls]) return false;
                bool bit = TestNonEmpty(&g, cls);
                if (bit != (listLen != 0)) return false;
            }
        }
    }
    return true;
}

} // namespace guild::mem
