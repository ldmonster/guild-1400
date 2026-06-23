// gilde.exe 0x604510 — VIBE_EventTable_CreateEvent
//
// Registers a fresh OS event handle into a process-wide, lock-guarded growable
// table. The original:
//   EnterCriticalSection(off_64A940);
//   h = CreateEventA(0,0,0,0);
//   if (h) { table = CloneOrFreeData(table, 4*(count+1)); table[count++] = h; }
//   else   { ++failCount; }
//   LeaveCriticalSection(off_64A944);
//
// RULE 6 decision (user, 2026-06-10): CreateEventA is a Win32 thread-sync
// primitive and is NOT a pre-approved swap — it is left as an INERT STUB that
// hands back a dummy (non-null) handle, so the genuine table-append bookkeeping
// runs 1:1 but no real cross-thread event object is created (the engine is
// single-threaded here). The critical-section enter/leave are inert no-ops for
// the same reason. The handle table holds 4-byte slots in the original; with
// integer dummy handles we model it as an i32 array (LP64-safe — no real
// pointer is ever truncated into a dword slot).
#pragma once

#include "guild/common/types.h"

namespace guild::sim {

// Injected leaves (inert defaults). createEvent must return a NON-NULL dummy
// handle to exercise the success path; lock enter/leave are no-ops.
struct EventTableHooks {
    void (*enterLock)() = nullptr;   // off_64A940 — EnterCriticalSection
    void (*leaveLock)() = nullptr;   // off_64A944 — LeaveCriticalSection
    i32  (*createEvent)() = nullptr; // CreateEventA(0,0,0,0) -> dummy handle
};

// Install hooks (nullptr => restore inert defaults). Returns the previous table.
void SetEventTableHooks(const EventTableHooks* hooks);

// gilde.exe 0x604510 — append a fresh event handle to the global table.
// (The original returns an uninitialised ecx that callers ignore; void here.)
void EventTableCreateEvent();

// --- Test / introspection accessors (the original globals) ------------------
i32  EventTableCount();          // dword_64ADC8
i32  EventTableFailCount();      // dword_64ADCC
i32  EventTableHandleAt(i32 i);  // table[i] (0 if out of range)
void EventTableReset();          // free table + zero counters (test fixture)

} // namespace guild::sim
