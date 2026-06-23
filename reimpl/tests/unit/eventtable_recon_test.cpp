// Golden tests for VIBE_EventTable_CreateEvent @0x604510 (guild::sim).
// Verifies the lock-guarded grow-and-append table bookkeeping, the dummy-handle
// success path, and the failure-counter path, against the gilde.exe decompile.
#include "tests/framework/test.h"

#include "sim/eventtable_recon.h"
#include "sim/object_lifecycle7.h"   // ObjLife7Hooks — backs CloneOrFreeData

#include <cstdlib>
#include <cstring>
#include <map>

using namespace guild;
using namespace guild::sim;

namespace {

// Malloc-backed allocator so the (faithful) CloneOrFreeData realloc-copy path
// the table-grow relies on actually allocates headlessly. A size map lets
// memBlockHeaderClear report each block's payload size (the bytes to preserve).
std::map<const void*, unsigned>& sizes() { static std::map<const void*, unsigned> m; return m; }
void* TAlloc(unsigned n) { void* p = std::malloc(n ? n : 1); sizes()[p] = n; return p; }
unsigned THeaderClear(const void* p) { auto it = sizes().find(p); return it == sizes().end() ? 0 : it->second; }
void* TShrink(const void* /*p*/) { return nullptr; }  // force the alloc+copy path
void TFree(const void* p) { sizes().erase(p); std::free(const_cast<void*>(p)); }

void InstallAllocator() {
    ObjLife7Hooks h{};
    h.memAllocFromFreeList = &TAlloc;
    h.memBlockHeaderClear  = &THeaderClear;
    h.memShrinkBlock       = &TShrink;
    h.memReturnToFreeList  = &TFree;
    ObjLife7SetHooks(h);
}

int g_enter = 0, g_leave = 0;
void CountEnter() { ++g_enter; }
void CountLeave() { ++g_leave; }

// A createEvent that fails (returns 0) -> exercises the ++failCount branch.
i32 FailCreate() { return 0; }

// A createEvent returning a fixed non-null handle.
i32 FixedCreate() { return 0xABCD; }

} // namespace

// Inert default: each call appends a distinct non-null dummy handle; count grows
// by one per call; no failures; lock balanced is implicit (no-op).
TEST(EventTableRecon, InertAppendsDistinctHandles) {
    EventTableReset();
    InstallAllocator();
    SetEventTableHooks(nullptr);

    EventTableCreateEvent();
    EventTableCreateEvent();
    EventTableCreateEvent();

    CHECK_EQ(EventTableCount(), 3);
    CHECK_EQ(EventTableFailCount(), 0);
    // Distinct, non-null, monotonic (1,2,3) per the inert dummy-handle source.
    CHECK_EQ(EventTableHandleAt(0), 1);
    CHECK_EQ(EventTableHandleAt(1), 2);
    CHECK_EQ(EventTableHandleAt(2), 3);
    CHECK(EventTableHandleAt(0) != 0);
    EventTableReset();
}

// The critical-section enter/leave fire exactly once per call, in balance.
TEST(EventTableRecon, LockEnterLeaveBalanced) {
    EventTableReset();
    InstallAllocator();
    g_enter = g_leave = 0;
    EventTableHooks h{};
    h.enterLock = &CountEnter;
    h.leaveLock = &CountLeave;
    h.createEvent = &FixedCreate;
    SetEventTableHooks(&h);

    EventTableCreateEvent();
    EventTableCreateEvent();

    CHECK_EQ(g_enter, 2);
    CHECK_EQ(g_leave, 2);
    CHECK_EQ(EventTableCount(), 2);
    CHECK_EQ(EventTableHandleAt(0), 0xABCD);
    CHECK_EQ(EventTableHandleAt(1), 0xABCD);
    SetEventTableHooks(nullptr);
    EventTableReset();
}

// CreateEventA failure -> handle NOT appended, failure counter bumped, lock still
// balanced (decompile: else { ++dword_64ADCC; }).
TEST(EventTableRecon, FailurePathBumpsCounterNoAppend) {
    EventTableReset();
    g_enter = g_leave = 0;
    EventTableHooks h{};
    h.enterLock = &CountEnter;
    h.leaveLock = &CountLeave;
    h.createEvent = &FailCreate;
    SetEventTableHooks(&h);

    EventTableCreateEvent();
    EventTableCreateEvent();

    CHECK_EQ(EventTableCount(), 0);        // nothing appended
    CHECK_EQ(EventTableFailCount(), 2);    // both failed
    CHECK_EQ(g_enter, 2);                  // lock still taken/released each call
    CHECK_EQ(g_leave, 2);
    SetEventTableHooks(nullptr);
    EventTableReset();
}

// Reset clears the table and all counters.
TEST(EventTableRecon, ResetClearsState) {
    EventTableReset();
    InstallAllocator();
    SetEventTableHooks(nullptr);
    EventTableCreateEvent();
    CHECK_EQ(EventTableCount(), 1);
    EventTableReset();
    CHECK_EQ(EventTableCount(), 0);
    CHECK_EQ(EventTableFailCount(), 0);
    CHECK_EQ(EventTableHandleAt(0), 0);    // out of range after reset
}
