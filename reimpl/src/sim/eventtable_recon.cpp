// gilde.exe 0x604510 — VIBE_EventTable_CreateEvent. See eventtable_recon.h.
#include "sim/eventtable_recon.h"

namespace guild::sim {

// VIBE_Object_CloneOrFreeData @0x5f1d00 — clone/grow/free a heap block
// (a1==0 => alloc size; size==0 => free+null; else realloc-copy). Reused, not
// redefined (defined in object_lifecycle7.cpp).
void* ObjectCloneOrFreeData(const void* block, unsigned size);

namespace {

// --- inert default leaves ---------------------------------------------------
void InertEnterLock() {}
void InertLeaveLock() {}
// Monotonic dummy-handle source: starts at 1 so every handle is non-null and
// distinct (mirrors CreateEventA always succeeding). Never returns 0.
i32 g_nextDummyHandle = 1;
i32 InertCreateEvent() { return g_nextDummyHandle++; }

const EventTableHooks kInert{ InertEnterLock, InertLeaveLock, InertCreateEvent };
EventTableHooks g_hooks = kInert;

// The original globals.
i32* g_table     = nullptr;  // dword_64ADC4 (array of 4-byte handle slots)
i32  g_count     = 0;        // dword_64ADC8
i32  g_failCount = 0;        // dword_64ADCC

} // namespace

void SetEventTableHooks(const EventTableHooks* hooks) {
    if (!hooks) { g_hooks = kInert; return; }
    g_hooks = *hooks;
    if (!g_hooks.enterLock)   g_hooks.enterLock   = InertEnterLock;
    if (!g_hooks.leaveLock)   g_hooks.leaveLock   = InertLeaveLock;
    if (!g_hooks.createEvent) g_hooks.createEvent = InertCreateEvent;
}

void EventTableCreateEvent() {
    g_hooks.enterLock();                                   // off_64A940()
    const i32 h = g_hooks.createEvent();                   // CreateEventA(0,0,0,0)
    if (h) {                                               // if (EventA)
        // table = CloneOrFreeData(table, 4*(count+1)); table[count] = h; ++count;
        g_table = static_cast<i32*>(
            ObjectCloneOrFreeData(g_table,
                                  static_cast<unsigned>(4 * (g_count + 1))));
        g_table[g_count] = h;                              // *(v1 + 4*v2 - 4) = EventA
        ++g_count;                                         // dword_64ADC8 = v2
    } else {
        ++g_failCount;                                     // ++dword_64ADCC
    }
    g_hooks.leaveLock();                                   // off_64A944()
}

i32 EventTableCount()     { return g_count; }
i32 EventTableFailCount() { return g_failCount; }
i32 EventTableHandleAt(i32 i) {
    if (!g_table || i < 0 || i >= g_count) return 0;
    return g_table[i];
}

void EventTableReset() {
    if (g_table) ObjectCloneOrFreeData(g_table, 0);  // size==0 => free + null
    g_table = nullptr;
    g_count = 0;
    g_failCount = 0;
    g_nextDummyHandle = 1;
}

} // namespace guild::sim
