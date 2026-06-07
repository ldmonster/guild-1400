#include "mem/mempool.h"

#include <cstring>

namespace guild::mem {

namespace {

// 64-BIT ADAPTATION: the original chunk header is 16 bytes (4 dwords) and each
// slot is prefixed by a 4-byte owner back-pointer (user = slot + 4). Here the
// back-pointer is a native 8-byte pointer, so the per-slot prefix and the chunk
// header widen accordingly. The original arithmetic (round_up + prefix, header
// + stride*count) is preserved; only the prefix/header widths change.
constexpr u32 kPoolHdr = sizeof(PoolChunk);                  // chunk header bytes
constexpr u32 kSlotOwner = static_cast<u32>(sizeof(void*));  // owner back-ptr bytes
constexpr u32 kSlotAlign = static_cast<u32>(sizeof(void*));  // payload alignment

// Per-slot stride = round_up(blockSize, ptr) + ptr. The leading owner-pointer
// word precedes each slot's user data (original: round_up(blockSize,4) + 4).
inline u32 SlotStride(u32 blockSize) {
    u32 stride = blockSize + kSlotOwner;
    u32 rem = blockSize & (kSlotAlign - 1);
    if (rem)
        stride += kSlotAlign - rem;
    return stride;
}

// First slot of a chunk (just past the header).
inline u8* ChunkSlots(PoolChunk* c) {
    return reinterpret_cast<u8*>(c) + kPoolHdr;
}

} // namespace

// VIBE_MemPool_Alloc @0x439778
void* MemPoolAlloc(MemPool* pool, MemoryTracker& tracker, u32 blockSize, int count) {
    if (!pool || blockSize == 0 || count <= 0)
        return nullptr;

    u32 stride = SlotStride(blockSize);

    PoolChunk* chunk = nullptr;
    if (pool->head) {
        // Find a non-full chunk with a matching stride.
        for (PoolChunk* c = pool->head; c; c = c->next) {
            if (c->usedCount < c->count && stride == c->stride) {
                chunk = c;
                break;
            }
        }
        if (!chunk) {
            // Allocate a fresh chunk and push to the front.
            u32 bytes = stride * static_cast<u32>(count) + kPoolHdr;
            chunk = static_cast<PoolChunk*>(
                tracker.AllocDebug(bytes, "m:pool_alloc(tp_m_pool)"));
            if (!chunk)
                return nullptr;
            chunk->usedCount = 0;
            chunk->count = static_cast<u32>(count);
            chunk->stride = stride;
            chunk->next = pool->head;
            pool->head = chunk;
        }
    } else {
        // First chunk for an empty pool.
        u32 bytes = stride * static_cast<u32>(count) + kPoolHdr;
        chunk = static_cast<PoolChunk*>(
            tracker.AllocDebug(bytes, "m:pool_get_free(tp_m_pool)"));
        if (!chunk)
            return nullptr;
        pool->head = chunk;
        chunk->usedCount = 0;
        chunk->next = nullptr;
        chunk->count = static_cast<u32>(count);
        chunk->stride = stride;
    }

    // Scan for a free slot (owner word == 0).
    if (static_cast<int>(chunk->count) <= 0)
        return nullptr;
    u8* slot = ChunkSlots(chunk);
    int idx = 0;
    while (*reinterpret_cast<PoolChunk**>(slot) != nullptr) {
        ++idx;
        slot += stride;
        if (idx >= static_cast<int>(chunk->count))
            return nullptr; // chunk reported non-full but no free slot
    }

    void* user = slot + kSlotOwner;                     // payload past owner word
    *reinterpret_cast<PoolChunk**>(slot) = chunk;       // owner back-pointer
    ++chunk->usedCount;                                 // ++*(DWORD*)v6
    std::memset(user, 0, stride - kSlotOwner);          // zero the user data
    return user;
}

// VIBE_MemPool_Free @0x439880
void MemPoolFree(MemPool* pool, MemoryTracker& tracker, void* userPtr) {
    if (!userPtr)
        return;
    // a2 == userPtr; the original treats a2 == 4 (null+4) as a no-op.
    u8* slot = static_cast<u8*>(userPtr) - kSlotOwner;  // v4 = a2 - owner word
    PoolChunk* chunk = *reinterpret_cast<PoolChunk**>(slot);
    if (!chunk)
        return;                                          // already free / invalid

    // Bounds-check: userPtr must lie within this chunk's slot range
    //   chunk + header (first user data) .. chunk + header + stride*count
    u8* lo = reinterpret_cast<u8*>(chunk) + kPoolHdr;
    u8* hi = reinterpret_cast<u8*>(chunk) + chunk->count * chunk->stride + kPoolHdr;
    u8* up = static_cast<u8*>(userPtr);
    if (!(up >= lo && up < hi))
        return;

    *reinterpret_cast<PoolChunk**>(slot) = nullptr;     // mark slot free
    bool nowEmpty = (chunk->usedCount == 1);
    --chunk->usedCount;
    if (!nowEmpty)
        return;

    // Chunk emptied: unlink from the pool list and free it.
    if (chunk == pool->head) {
        pool->head = chunk->next;
        tracker.FreeDebug(chunk);
        return;
    }
    PoolChunk* prev = pool->head;
    while (prev->next != chunk)
        prev = prev->next;
    prev->next = chunk->next;
    tracker.FreeDebug(chunk);
}

// VIBE_MemPool_FreeAll @0x4399b4
void MemPoolFreeAll(MemPool* pool, MemoryTracker& tracker) {
    if (!pool)
        return;
    PoolChunk* c = pool->head;
    while (c) {
        PoolChunk* nxt = c->next;
        tracker.FreeDebug(c);
        c = nxt;
    }
    pool->head = nullptr;
}

// VIBE_MemPool_ResetBlocks @0x4399dc
int MemPoolResetBlocks(MemPool* pool) {
    int total = 0;
    if (!pool)
        return 0;
    for (PoolChunk* c = pool->head; c; c = c->next) {
        total += static_cast<int>(c->usedCount);
        c->usedCount = 0;
        u8* slot = ChunkSlots(c);
        for (u32 i = 0; i < c->count; ++i) {
            *reinterpret_cast<PoolChunk**>(slot) = nullptr; // clear owner word
            slot += c->stride;
        }
    }
    return total;
}

// VIBE_MemPool_GetNextUsed @0x4398f8
void* MemPoolNext(PoolCursor* cur) {
    if (!cur)
        return nullptr;
    while (cur->chunk) {
        PoolChunk* c = cur->chunk;
        u32 stride = c->stride;
        if (cur->remaining == 0)
            cur->remaining = static_cast<int>(c->usedCount); // seed on entry
        int idx = cur->slotIndex;
        u8* slot = reinterpret_cast<u8*>(c) + stride * static_cast<u32>(idx) + kPoolHdr;
        if (cur->remaining > 0) {
            // Skip free slots (bounded by the chunk's slot count). With
            // remaining > 0 a used slot is guaranteed to exist ahead.
            while (idx < static_cast<int>(c->count) &&
                   *reinterpret_cast<PoolChunk**>(slot) == nullptr) {
                ++idx;
                slot += stride;
            }
            if (idx >= static_cast<int>(c->count)) {
                cur->chunk = c->next;
                cur->slotIndex = 0;
                cur->remaining = 0;
                continue;
            }
            --cur->remaining;
            if (cur->remaining <= 0) {
                // Last used slot in this chunk: advance chunk for next call.
                cur->chunk = c->next;
                cur->slotIndex = 0;
                cur->remaining = 0;
            } else {
                cur->slotIndex = idx + 1;
            }
            return slot + kSlotOwner;
        }
        cur->chunk = c->next;
        cur->slotIndex = 0;
        cur->remaining = 0;
    }
    return nullptr;
}

// VIBE_MemPool_GetFirstUsed @0x43998c
void* MemPoolBegin(MemPool* pool, PoolCursor* cur) {
    if (!pool || !pool->head || !cur)
        return nullptr;
    cur->slotIndex = 0;
    cur->chunk = pool->head;
    cur->remaining = 0;
    return MemPoolNext(cur);
}

} // namespace guild::mem
