#pragma once
#include "guild/common/types.h"
#include "mem/memory_debug.h"
#include <cstddef>

// Fixed-block memory pool from gilde.exe ("m_pool"). A pool is a singly linked
// list of *chunks*; each chunk holds a fixed number of equal-sized slots carved
// from one debug-tracked allocation. Slots are recycled in place; when a chunk
// becomes empty it is unlinked and freed. Used for many short-lived same-size
// records (the original tags allocations "tp_m_pool").
//
//   VIBE_MemPool_Alloc        @0x439778  (eax=&head, edx=blockSize, ebx=count)
//   VIBE_MemPool_Free         @0x439880  (eax=&head, edx=user pointer)
//   VIBE_MemPool_GetFirstUsed @0x43998c  / GetNextUsed @0x4398f8 (iterate live)
//   VIBE_MemPool_FreeAll      @0x4399b4  (free every chunk)
//   VIBE_MemPool_ResetBlocks  @0x4399dc  (mark all slots free, keep chunks)
//
// CHUNK header (16 bytes), then `count` slots of `stride` bytes each:
//   +0x00 usedCount   number of in-use slots in this chunk
//   +0x04 count       slots per chunk (the requested `count`)
//   +0x08 stride      per-slot bytes = round_up(blockSize, 4) + 4
//   +0x0C next        next chunk pointer
// SLOT layout: +0x00 owner chunk back-pointer (0 == free), +0x04.. user data
// (stride-4 usable bytes). The user pointer returned is slot+4.
namespace guild::mem {

// Chunk header. Layout matches the original (see header note).
struct PoolChunk {
    u32 usedCount;     // +0x00
    u32 count;         // +0x04
    u32 stride;        // +0x08
    PoolChunk* next;   // +0x0C
    // slots follow at +0x10
};

// A pool is just its head-chunk pointer (the original passes &head by register).
struct MemPool {
    PoolChunk* head = nullptr;
};

// VIBE_MemPool_Alloc @0x439778 — allocate one slot of `blockSize` user bytes
// from `pool`; chunks hold `count` slots each. Allocates a new chunk via the
// debug tracker when all existing chunks are full. Returns a zeroed user pointer
// or nullptr (bad args / OOM / chunk full but capacity miscount).
void* MemPoolAlloc(MemPool* pool, MemoryTracker& tracker, u32 blockSize, int count);

// VIBE_MemPool_Free @0x439880 — return a slot obtained from MemPoolAlloc. When
// its chunk empties, the chunk is unlinked and freed via the tracker. Validates
// that the pointer lies within its owning chunk's slot range.
void MemPoolFree(MemPool* pool, MemoryTracker& tracker, void* userPtr);

// VIBE_MemPool_FreeAll @0x4399b4 — free every chunk and clear the pool.
void MemPoolFreeAll(MemPool* pool, MemoryTracker& tracker);

// VIBE_MemPool_ResetBlocks @0x4399dc — mark all slots free (usedCount=0, slot
// owner words cleared) without freeing chunks. Returns total slots reset.
int MemPoolResetBlocks(MemPool* pool);

// Iteration over live slots, mirroring GetFirstUsed/GetNextUsed. The cursor is
// opaque state seeded by Begin(); each Next() yields a live user pointer or
// nullptr when exhausted.
struct PoolCursor {
    PoolChunk* chunk; // [0] current chunk
    int slotIndex;    // [1] next slot index within chunk
    int remaining;    // [2] used slots remaining in current chunk
};

// VIBE_MemPool_GetFirstUsed @0x43998c
void* MemPoolBegin(MemPool* pool, PoolCursor* cur);
// VIBE_MemPool_GetNextUsed @0x4398f8
void* MemPoolNext(PoolCursor* cur);

} // namespace guild::mem
