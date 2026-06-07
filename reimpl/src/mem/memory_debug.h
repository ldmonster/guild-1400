#pragma once
#include "guild/common/types.h"
#include "mem/heap.h"
#include <cstddef>

// Game-side "Memory" debug allocation tracker from gilde.exe. Wraps the
// free-list heap (heap.*) with 0xDEADBEEF guard words and named-group byte/block
// accounting so the engine can detect overwrites and report per-subsystem usage.
//
//   VIBE_Memory_AllocDebug      @0x438f10  (eax=size, edx=group string)
//   VIBE_Memory_FreeDebug       @0x43923c  (eax=user pointer)
//   VIBE_Memory_IsValidPointer  @0x4391f0
//   VIBE_Memory_InitTracker     @0x43937c  (eax=entry capacity)
//   VIBE_Memory_ShutdownTracker @0x439640
//   VIBE_Memory_LogGroupStats   @0x43941c
//
// GUARD WORDS: AllocDebug requests (userSize + 8) bytes from the heap, writes
//   block[0]            = 0xDEADBEEF   (leading guard)
//   block[userSize+4]   = 0xDEADBEEF   (trailing guard, just past the payload)
// and returns block+4 to the caller. FreeDebug checks both guards; a mismatch
// means the caller wrote out of bounds and is reported ("Memory overwritten").
// The constant is -559038737 == 0xDEADBEEF in the binary.
//
// GROUP STRING: the edx argument is "name:detail". The substring up to and
// including the ':' (max 15 chars) names an accounting *group*; groups are
// interned in a linked list and accumulate cur/max bytes and cur/max block
// counts plus a call counter.
namespace guild::mem {

constexpr u32 kGuardWord = 0xDEADBEEFu; // -559038737
constexpr u32 kMaxGroupName = 16;       // includes the ':' and NUL; >=16 warns

// Group accounting record. Original layout (0x28 bytes, dword-indexed a2[N]):
//   +0x00 name[16]   (interned, copied up to and including ':')
//   +0x10 curBytes   (a2[4])
//   +0x14 curBlocks  (a2[5])
//   +0x18 maxBytes   (a2[6])
//   +0x1C maxBlocks  (a2[7])
//   +0x20 callCount  (a2[8])
//   +0x24 next       (a2[9])  linked-list pointer
struct MemGroup {
    char  name[16]; // +0x00
    i32   curBytes; // +0x10
    i32   curBlocks;// +0x14
    i32   maxBytes; // +0x18
    i32   maxBlocks;// +0x1C
    i32   callCount;// +0x20
    MemGroup* next; // +0x24
};

// Tracker entry. Original array element is 16 bytes (4 dwords), indexed
// dword_62D9F4[i*4 + k]:
//   +0x00 group   (MemGroup*)        k=0
//   +0x04 info    (group string)     k=1
//   +0x08 block   (guarded block)    k=2  (0 = slot free)
//   +0x0C userSize                   k=3
struct MemTrackEntry {
    MemGroup* group;  // +0x00
    const char* info; // +0x04
    void* block;      // +0x08  (block start = user pointer - 4)
    u32 userSize;     // +0x0C
};

// The tracker. Mirrors the process globals dword_62D9DC/E0/E4/E8/EC/F4 and the
// interned group list head unk_764840. Encapsulated so tests are isolated.
class MemoryTracker {
public:
    explicit MemoryTracker(Heap& heap);
    ~MemoryTracker();

    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    // VIBE_Memory_InitTracker @0x43937c — allocate the entry table (capacity
    // entries) and reset all counters. The interned group "_main_:" is created.
    void Init(u32 capacity);

    // VIBE_Memory_AllocDebug @0x438f10 — allocate `userSize` bytes tagged with
    // group string `info` ("name:detail"). Returns a guarded user pointer, or
    // nullptr on failure or if the tracker is uninitialised.
    void* AllocDebug(u32 userSize, const char* info);

    // VIBE_Memory_FreeDebug @0x43923c — free a pointer from AllocDebug, checking
    // both guard words and updating accounting. Detects overwrites via
    // GuardCorrupted() below. Null-safe.
    void FreeDebug(void* userPtr);

    // VIBE_Memory_IsValidPointer @0x4391f0 — true if `userPtr` is null or a live
    // tracked allocation.
    bool IsValidPointer(void* userPtr) const;

    // VIBE_Memory_ShutdownTracker @0x439640 — free the table and group list.
    void Shutdown();

    // --- diagnostics (the original logs via sprintf; we expose the values) ---
    i32 CurBytes()  const { return m_curBytes; }   // dword_62D9E0
    i32 CurBlocks() const { return m_curBlocks; }  // dword_62D9E4
    i32 MaxBytes()  const { return m_maxBytes; }   // dword_62D9E8
    i32 MaxBlocks() const { return m_maxBlocks; }  // dword_62D9EC

    // Look up an interned group by name (the part before ':'), or nullptr.
    const MemGroup* FindGroup(const char* name) const;

    // Number of guard-corruption events observed on free (test hook for the
    // original's "Memory overwritten" reports).
    int CorruptionCount() const { return m_corruptions; }

private:
    MemGroup* InternGroup(const char* info);

    Heap& m_heap;
    MemTrackEntry* m_table = nullptr; // dword_62D9F4
    i32 m_capacity = 0;               // dword_62D9DC
    i32 m_curBytes = 0;               // dword_62D9E0
    i32 m_curBlocks = 0;              // dword_62D9E4
    i32 m_maxBytes = 0;               // dword_62D9E8
    i32 m_maxBlocks = 0;              // dword_62D9EC
    MemGroup* m_groupHead = nullptr;  // unk_764840
    int m_corruptions = 0;            // not in original; counts overwrite reports
};

} // namespace guild::mem
