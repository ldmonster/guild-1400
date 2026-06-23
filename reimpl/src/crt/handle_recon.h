#pragma once
// gilde.exe — CRT generic handle table (the os-handle table behind the
// low-level I/O layer). namespace guild::crt.
//
// The table is a dynamically grown array of 32-bit handle values:
//   dword_64ADBC  — base pointer to the int[] array        (table_)
//   dword_64ADC0  — element count (high-water capacity)    (count_)
// A second, distinct array of OS HANDLEs created by the table itself is tracked
// for cleanup:
//   dword_64ADC4  — created-handle array base              (owned_)
//   dword_64ADC8  — created-handle count                   (ownedCount_)
//
// Recovered 1:1 from:
//   VIBE_HandleTable_HasFreeSlot   @0x6042d0
//   VIBE_HandleTable_AddEntry      @0x604324  (__usercall eax=value)
//   VIBE_HandleTable_SetStdHandle  @0x6043a8  (__usercall eax=value edx=index)
//   VIBE_HandleTable_ClearEntry    @0x60447c  (slot index in edx)
//   VIBE_HandleTable_InitStdHandles@0x6044b0
//   VIBE_HandleTable_Cleanup       @0x604578
//
// Platform-coupled leaves (Win32 SetStdHandle / GetStdHandle / CloseHandle and
// the per-os-fd lock off_64A940/off_64A944) are modelled as injected, inert-by-
// default hooks so the pure table logic is reproduced exactly. VIBE_EventTable_
// CreateEvent @0x604510 (the "make a fresh OS handle" leaf) is likewise injected.
#include "guild/common/types.h"
#include <vector>

namespace guild::crt {

// Std-handle ids passed to the SetStdHandle leaf. The original maps table index
// 0/1/2 to STD_INPUT(-10)/STD_OUTPUT(-11)/STD_ERROR(-12).
constexpr i32 kStdInputHandle  = -10; // 0xFFFFFFF6
constexpr i32 kStdOutputHandle = -11; // 0xFFFFFFF5
constexpr i32 kStdErrorHandle  = -12; // 0xFFFFFFF4

class HandleTable {
public:
    // --- injected leaves (default inert) -----------------------------------
    // off_64A940 / off_64A944 — acquire / release the table lock. Inert.
    using LockFn = void (*)();
    void set_lock(LockFn acquire, LockFn release) { acquire_ = acquire; release_ = release; }

    // SetStdHandle(stdId, value) — Win32 std-handle install. Inert by default;
    // last call is recorded for inspection/tests.
    using SetStdFn = void (*)(i32 stdId, i32 value);
    void set_set_std(SetStdFn fn) { setStd_ = fn; }

    // GetStdHandle(stdId) — Win32 std-handle query. Returns 0 by default.
    using GetStdFn = i32 (*)(i32 stdId);
    void set_get_std(GetStdFn fn) { getStd_ = fn; }

    // VIBE_EventTable_CreateEvent @0x604510 — fabricate a fresh OS handle when no
    // inherited std handle exists. Returns 0 by default.
    using CreateEventFn = i32 (*)();
    void set_create_event(CreateEventFn fn) { createEvent_ = fn; }

    // CloseHandle(value) — Win32 handle close (Cleanup path). Inert by default.
    using CloseFn = void (*)(i32 value);
    void set_close(CloseFn fn) { close_ = fn; }

    // VIBE_HandleTable_HasFreeSlot @0x6042d0.
    //   if (count < dword_64AE10) return 0;          // below the reserve floor
    //   if (count <= 0) return 1;
    //   scan for a zero slot; 1 if found, else 0.
    // dword_64AE10 is the std-handle reserve floor (3 after InitStdHandles).
    int HasFreeSlot() const;

    // VIBE_HandleTable_AddEntry @0x604324 — return the slot index the value was
    // stored in. Reuses the first zero slot; otherwise grows the array by one.
    i32 AddEntry(i32 value);

    // VIBE_HandleTable_SetStdHandle @0x6043a8 — store `value` at table index
    // `index`, mirror it into the Win32 std handle for index 0/1/2, growing
    // (and zero-filling the gap) if index >= count. No-op for index < 0.
    void SetStdHandle(i32 value, i32 index);

    // VIBE_HandleTable_ClearEntry @0x60447c — zero the slot at `index` (only if
    // 0 < index < count; index 0 is the reserved std-in slot and is left alone).
    void ClearEntry(i32 index);

    // VIBE_HandleTable_InitStdHandles @0x6044b0 — seed slots 0/1/2 from the
    // inherited std handles (fabricating a fresh event when none), via AddEntry.
    // Returns the last AddEntry result (the index of the std-error slot).
    i32 InitStdHandles();

    // VIBE_HandleTable_Cleanup @0x604578 — free the table array and close every
    // OS handle the table created, then free the created-handle array.
    void Cleanup();

    // dword_64AE10 — reserve floor used by HasFreeSlot. Settable for tests.
    void set_reserve_floor(i32 n) { reserveFloor_ = n; }

    // --- inspectable state --------------------------------------------------
    i32  count() const { return count_; }
    i32  at(i32 i) const { return table_[static_cast<std::size_t>(i)]; }
    const std::vector<i32>& table() const { return table_; }

    // Register an OS handle as table-owned (so Cleanup closes it). The original's
    // owned array is populated by the event/file open paths; exposed for wiring.
    void track_owned(i32 value) { owned_.push_back(value); }

private:
    void lockAcquire() const { if (acquire_) acquire_(); }
    void lockRelease() const { if (release_) release_(); }

    std::vector<i32> table_;   // dword_64ADBC / dword_64ADC0
    std::vector<i32> owned_;   // dword_64ADC4 / dword_64ADC8
    i32 count_ = 0;            // dword_64ADC0 (kept == table_.size())
    i32 reserveFloor_ = 0;     // dword_64AE10

    LockFn       acquire_     = nullptr;
    LockFn       release_     = nullptr;
    SetStdFn     setStd_      = nullptr;
    GetStdFn     getStd_      = nullptr;
    CreateEventFn createEvent_ = nullptr;
    CloseFn      close_       = nullptr;
};

} // namespace guild::crt
