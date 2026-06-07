#include "ai/meister_events.h"

#include <cstring>

// MeisterAi event-slot managers (gilde.exe 0x4c6f68..0x4c7164, 0x4c932c).
// See meister_events.h for the pool layouts and field offsets. The originals
// operate over flat global arrays via dword-strided indices; here the same
// traversal is expressed over the named slot structs, preserving the exact
// search order, overflow behaviour and (quirky) in-place ageing control flow.

namespace guild::ai {

namespace {

// gilde.exe 0x5d9360 — VIBE_Util_StrNCopyPad: copy up to `n` chars from src,
// stopping at its NUL, then zero-fill the remainder of the n-byte field. (A
// module-private clone; the shared util symbol lives in other modules but is
// not exported here. Behaviour is byte-identical.)
void StrNCopyPad(char* dst, const char* src, int n) {
    int remaining = n;
    while (remaining && *src) {
        *dst++ = *src++;
        --remaining;
    }
    while (remaining) {
        *dst++ = 0;
        --remaining;
    }
}

} // namespace

// gilde.exe 0x4c703c — he_RegisterApEvent.
int RegisterApEvent(ApEventPool& pool, i32 owner, i32 secondary, i32 primary,
                    const char* label, ErrorLogFn log) {
    // Scan for the first slot that is entirely empty. The original tests
    // dword_11C6568 (primary, +0x08), dword_11C656C (secondary, +0x0C) and
    // byte_11C6570 (label[0], +0x10) == 0 — note it checks the label's first
    // byte, not the ttl marker.
    int index = 0;
    for (; index < kApEventSlotCount; ++index) {
        const ApEventSlot& s = pool.slots[index];
        if (s.primary == 0 && s.secondary == 0 && s.label[0] == 0)
            break;
    }

    if (index < kApEventSlotCount) {
        ApEventSlot& s = pool.slots[index];
        s.ttl = 2;            // *v10 = 2
        s.ownerKey = owner;   // *((_DWORD*)v10 + 1) = a1
        s.primary = primary;  // *((_DWORD*)v10 + 2) = a3
        s.secondary = secondary; // *((_DWORD*)v10 + 3) = a2
        StrNCopyPad(s.label, label ? label : "", 63);
        return index;
    }

    // Overflow: report and return failure. Original returns 0; we keep the
    // diagnostic and return -1 to signal "no slot" to callers.
    if (log)
        log("he_RegisterApEvent(): Too many ApEvents...");
    return -1;
}

// gilde.exe 0x4c70c0 — ExpireApEventSlots.
void ExpireApEventSlots(ApEventPool& pool) {
    // Faithful translation of the original's interleaved ageing/free walk: it
    // ages each slot's ttl exactly once; a slot whose ttl reaches <= 0 has its
    // primary/secondary/label[0] cleared. The inner loop carries the cursor
    // forward so the outer `for` resumes from the freed slot.
    for (int result = 0; result != kApEventSlotCount; ++result) {
        while (true) {
            i8 v1 = static_cast<i8>(pool.slots[result].ttl) - 1;
            pool.slots[result].ttl = static_cast<u8>(v1);
            if (v1 <= 0)
                break;
            ++result;
            if (result == kApEventSlotCount)
                return;
        }
        ApEventSlot& s = pool.slots[result];
        s.primary = 0;      // dword_11C6568[result] = 0
        s.secondary = 0;    // dword_11C656C[result] = 0
        s.label[0] = 0;     // byte_11C6570[result*4] = 0
    }
}

// gilde.exe 0x4c7104 — SumApEventsByOwner.
int SumApEventsByOwner(const ApEventPool& pool, i32 owner) {
    int total = 0;
    for (int i = 0; i != kApEventSlotCount; ++i) {
        i32 v = pool.slots[i].primary;
        if (v && owner == pool.slots[i].ownerKey)
            total += v;
    }
    return total;
}

// gilde.exe 0x4c7134 — SumApEventsBySecondary.
int SumApEventsBySecondary(const ApEventPool& pool, i32 owner) {
    int total = 0;
    for (int i = 0; i != kApEventSlotCount; ++i) {
        i32 v = pool.slots[i].secondary;
        if (v && owner == pool.slots[i].ownerKey)
            total += v;
    }
    return total;
}

// gilde.exe 0x4c6f68 — RegisterEventSlot.
int RegisterEventSlot(EventPool& pool, i32 id, i32 payloadA, i32 payloadB,
                      const char* label, const void* payload,
                      std::size_t payloadSize) {
    // Scan for the first free slot (id == 0). The original guards the search
    // with `if (dword_11CB620[0])` and advances while id != 0; an all-free pool
    // therefore stops at index 0, exactly as this loop does.
    int index = 0;
    for (; index < kEventSlotCount; ++index) {
        if (pool.slots[index].id == 0)
            break;
    }

    if (index < kEventSlotCount) {
        EventSlot& s = pool.slots[index];
        s.kind = 1;            // v11[2] = 1
        s.ttl = 2;             // *((_BYTE*)v11 + 4) = 2
        s.id = id;             // *v11 = a1
        s.payloadA = payloadA; // v11[3] = a2
        s.payloadB = payloadB; // v11[4] = a3
        StrNCopyPad(s.label, label ? label : "", 127);
        std::size_t n = payloadSize;
        if (n > sizeof(s.payload))
            n = sizeof(s.payload);
        if (payload && n)
            std::memcpy(s.payload, payload, n); // qmemcpy(v12, a5, v13)
        return index;
    }
    return -1;
}

// gilde.exe 0x4c7004 — ExpireEventSlots.
void ExpireEventSlots(EventPool& pool) {
    // Same interleaved ageing/free walk as ExpireApEventSlots, over the ttl
    // byte; a slot whose ttl reaches 0 has its id cleared (freed).
    for (int result = 0; result != kEventSlotCount; ++result) {
        while (true) {
            i8 v1 = static_cast<i8>(pool.slots[result].ttl) - 1;
            pool.slots[result].ttl = static_cast<u8>(v1);
            if (!v1)
                break;
            ++result;
            if (result == kEventSlotCount)
                return;
        }
        pool.slots[result].id = 0;   // dword_11CB620[result] = 0
    }
}

// gilde.exe 0x4c932c — NullTick.
void NullTick() {
    // Intentionally empty (the original is a single `retn`).
}

} // namespace guild::ai
