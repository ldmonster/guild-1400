#include "mem/memory_debug.h"

#include <cstring>

namespace guild::mem {

namespace {

// Extract the group name (up to and including ':') from an "name:detail" string.
// Mirrors AllocDebug's scan: walk to the first ':'; the kept length is the
// number of chars up to and including it. Returns false if there is no ':'.
// `out` receives the name including the ':' and a NUL terminator.
bool ExtractGroupName(const char* info, char out[16], bool* tooLong) {
    *tooLong = false;
    if (!info)
        return false;
    const char* p = info;
    while (*p != ':') {
        if (*p == '\0')
            return false; // no ':' -> no group (v23 = 0 path)
        ++p;
    }
    int len = static_cast<int>(p - info) + 1; // include the ':'
    if (len >= 16)
        *tooLong = true;                      // original logs a warning
    int copy = len < 15 ? len : 15;           // StrNCopyPad bounds + NUL below
    std::memcpy(out, info, static_cast<std::size_t>(copy));
    out[copy] = '\0';                         // *((BYTE*)v33 + v25) = 0
    return true;
}

} // namespace

MemoryTracker::MemoryTracker(Heap& heap) : m_heap(heap) {}

MemoryTracker::~MemoryTracker() {
    Shutdown();
}

// VIBE_Memory_InitTracker @0x43937c
void MemoryTracker::Init(u32 capacity) {
    m_capacity = static_cast<i32>(capacity);
    m_curBytes = 0;
    m_maxBytes = 0;
    m_curBlocks = 0;
    m_maxBlocks = 0;
    // One MemTrackEntry per slot, zero-filled (the original memsets it). The
    // original entry is 16 bytes (4 dwords); on 64-bit our pointers widen the
    // entry, so size by sizeof(MemTrackEntry) rather than the literal 16.
    std::size_t bytes = sizeof(MemTrackEntry) * static_cast<std::size_t>(capacity);
    m_table = static_cast<MemTrackEntry*>(
        m_heap.AllocFromFreeList(static_cast<u32>(bytes)));
    if (m_table)
        std::memset(m_table, 0, bytes);
    // Intern the primordial "_main_:" group (unk_764840 head).
    m_groupHead = nullptr;
    InternGroup("_main_:");
}

// Group interning: find by name in the linked list, else allocate (0x28 bytes)
// from the heap and push. Mirrors AllocDebug's intern path and InitTracker's
// "_main_:" seeding.
MemGroup* MemoryTracker::InternGroup(const char* info) {
    char name[16];
    bool tooLong = false;
    if (!ExtractGroupName(info, name, &tooLong)) {
        // No ':' in the string: the original keeps the current head group
        // (v2 = &unk_764840) without interning a new one.
        return m_groupHead;
    }
    for (MemGroup* g = m_groupHead; g; g = g->next) {
        if (std::strcmp(g->name, name) == 0)
            return g;
    }
    MemGroup* g = static_cast<MemGroup*>(m_heap.AllocFromFreeList(sizeof(MemGroup)));
    if (!g)
        return m_groupHead;
    std::memset(g, 0, sizeof(MemGroup));
    std::strncpy(g->name, name, sizeof(g->name) - 1);
    g->name[sizeof(g->name) - 1] = '\0';
    g->next = m_groupHead;
    m_groupHead = g;
    return g;
}

const MemGroup* MemoryTracker::FindGroup(const char* name) const {
    for (MemGroup* g = m_groupHead; g; g = g->next) {
        // Compare against the stored name with the ':' stripped, like the user
        // would query it.
        char bare[16];
        std::strncpy(bare, g->name, sizeof(bare));
        bare[sizeof(bare) - 1] = '\0';
        char* colon = std::strchr(bare, ':');
        if (colon)
            *colon = '\0';
        if (std::strcmp(bare, name) == 0)
            return g;
    }
    return nullptr;
}

// VIBE_Memory_AllocDebug @0x438f10
void* MemoryTracker::AllocDebug(u32 userSize, const char* info) {
    if (!m_table)
        return nullptr;       // tracker not initialised (dword_62D9F4 == 0)

    // Intern / locate the accounting group for `info`.
    MemGroup* group = InternGroup(info);
    if (!group)
        group = m_groupHead;  // falls back to the seed group like the original

    ++group->callCount;       // ++v2[8]

    // Request payload + two guard words.
    u32 total = userSize + 8; // v4 = v3 + 8
    void* block = nullptr;
    if (total != 0)
        block = m_heap.AllocFromFreeList(total);
    if (!block && total != 0) {
        // Out-of-memory: original logs and returns 0.
        return nullptr;
    }
    if (total != 0)
        std::memset(block, 0, total); // zero-fill the whole guarded block

    // Record the allocation if there is a free table slot.
    if (m_curBlocks < m_capacity && block) {
        // Find a free entry (block field == 0).
        MemTrackEntry* e = m_table;
        i32 i = 0;
        for (; i < m_capacity; ++e) {
            if (e->block == nullptr)
                break;
            ++i;
        }
        // (i < m_capacity guaranteed because curBlocks < capacity)
        e->group = group;
        e->info = info;
        e->block = block;
        e->userSize = userSize;

        // Global byte total + peak.
        m_curBytes += static_cast<i32>(total); // v4 added to dword_62D9E0
        if (m_curBytes > m_maxBytes)
            m_maxBytes = m_curBytes;

        // Per-group byte total + peak.
        group->curBytes += static_cast<i32>(total);
        if (group->curBytes > group->maxBytes)
            group->maxBytes = group->curBytes;

        // Global block count + peak.
        ++m_curBlocks;
        if (m_curBlocks > m_maxBlocks)
            m_maxBlocks = m_curBlocks;

        // Per-group block count + peak.
        ++group->curBlocks;
        if (group->curBlocks > group->maxBlocks)
            group->maxBlocks = group->curBlocks;

        // Write the guard words: block[0] and block[userSize+4].
        u8* b = static_cast<u8*>(block);
        *reinterpret_cast<u32*>(b) = kGuardWord;
        *reinterpret_cast<u32*>(b + userSize + 4) = kGuardWord;
    }

    if (block)
        return static_cast<u8*>(block) + 4; // user pointer is block + 4
    return nullptr;
}

// VIBE_Memory_FreeDebug @0x43923c
void MemoryTracker::FreeDebug(void* userPtr) {
    if (!userPtr)
        return;
    u8* block = static_cast<u8*>(userPtr) - 4; // v4 = result - 4

    // Locate the tracking entry by block pointer.
    MemTrackEntry* e = m_table;
    i32 i = 0;
    for (; i < m_capacity; ++e) {
        if (e->block == block)
            break;
        ++i;
    }
    if (i == m_capacity)
        return; // not tracked (untracked free is a no-op here)

    u32 userSize = e->userSize;

    // Leading guard check.
    if (*reinterpret_cast<u32*>(block) != kGuardWord) {
        // "Memory overwritten (start):\n<group info>"
        ++m_corruptions;
    }
    // Trailing guard check at block + userSize + 4.
    if (*reinterpret_cast<u32*>(block + userSize + 4) != kGuardWord) {
        // "Memory overwritten (end):\n<group info>"
        ++m_corruptions;
    }

    u32 total = userSize + 8; // v22 = userSize; v22 += 8

    e->block = nullptr;       // v5[2] = 0 (free the slot)
    m_heap.ReturnToFreeList(block);

    // Per-group accounting decrement (the original updates the group via the
    // entry's group pointer: --group->curBlocks, group->curBytes -= total).
    MemGroup* group = e->group;
    if (group) {
        --group->curBlocks;
        group->curBytes -= static_cast<i32>(total);
    }
    m_curBytes -= static_cast<i32>(total);
    --m_curBlocks;
}

// VIBE_Memory_IsValidPointer @0x4391f0
bool MemoryTracker::IsValidPointer(void* userPtr) const {
    if (!userPtr)
        return true;
    u8* block = static_cast<u8*>(userPtr) - 4;
    MemTrackEntry* e = m_table;
    i32 i = 0;
    for (; i < m_capacity; ++e) {
        if (e->block == block)
            break;
        ++i;
    }
    return i != m_capacity;
}

// VIBE_Memory_ShutdownTracker @0x439640
void MemoryTracker::Shutdown() {
    if (!m_table)
        return;
    // Reclaim any still-live tracked blocks ("-> Pointer not freed").
    MemTrackEntry* e = m_table;
    for (i32 i = 0; i < m_capacity; ++i, ++e) {
        if (e->block) {
            m_heap.ReturnToFreeList(e->block);
            e->block = nullptr;
        }
    }
    m_maxBlocks = 0;
    m_curBlocks = 0;
    m_maxBytes = 0;
    m_curBytes = 0;
    m_capacity = 0;

    // Free the interned group list.
    MemGroup* g = m_groupHead;
    while (g) {
        MemGroup* nxt = g->next;
        m_heap.ReturnToFreeList(g);
        g = nxt;
    }
    m_groupHead = nullptr;

    // Free the entry table.
    m_heap.ReturnToFreeList(m_table);
    m_table = nullptr;
}

} // namespace guild::mem
