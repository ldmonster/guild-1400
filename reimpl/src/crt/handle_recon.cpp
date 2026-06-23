#include "crt/handle_recon.h"

// gilde.exe — CRT generic handle table. See handle_recon.h for the recovered
// global layout (dword_64ADBC..dword_64ADC8) and provenance. Each function is a
// straight translation of the Hex-Rays pseudocode; the dynamic int[] grown by
// VIBE_Object_CloneOrFreeData (realloc) / freed by VIBE_Memory_ReturnToFreeList
// (free) is modelled by std::vector<i32>.

namespace guild::crt {

// gilde.exe 0x6042d0 — VIBE_HandleTable_HasFreeSlot.
int HandleTable::HasFreeSlot() const {
    if (count_ < reserveFloor_)            // dword_64ADC0 < dword_64AE10
        return 0;
    if (count_ <= 0)                       // dword_64ADC0 <= 0
        return 1;
    int v1 = 0;                            // byte offset into the array
    while (table_[static_cast<std::size_t>(v1 / 4)] != 0) {
        v1 += 4;
        if (v1 >= 4 * count_)
            return 1;
    }
    return 0;
}

// gilde.exe 0x604324 — VIBE_HandleTable_AddEntry(value@eax). Returns the slot
// index. The original returns `v4` (uninitialised edx) on the reuse path; edx
// holds the index counter `v2`, which has been incremented in lockstep with the
// scan, so the faithful value is that running index. We reproduce it exactly.
i32 HandleTable::AddEntry(i32 a1) {
    lockAcquire();                         // off_64A940()
    i32 v2 = 0;                            // edx — running slot index
    if (count_ > 0) {
        i32 v3 = 0;                        // byte offset
        while (table_[static_cast<std::size_t>(v3 / 4)] != 0) {
            v3 += 4;
            ++v2;
            if (v3 >= 4 * count_)
                goto grow;                 // LABEL_6
        }
        table_[static_cast<std::size_t>(v3 / 4)] = a1;
        lockRelease();                     // off_64A944()
        return v2;                         // return v4 (== running index v2)
    }
grow:
    // dword_64ADBC = realloc(..., 4*(count+1)); store at [count]; ++count.
    table_.push_back(a1);
    count_ = static_cast<i32>(table_.size());
    lockRelease();                         // off_64A944()
    return count_ - 1;
}

// gilde.exe 0x6043a8 — VIBE_HandleTable_SetStdHandle(value@eax, index@edx).
void HandleTable::SetStdHandle(i32 a1, i32 a2) {
    if (a2 < 0)
        return;
    lockAcquire();                         // off_64A940()
    // The original mirrors the value into the Win32 std handle keyed by the
    // index (v3): 0 -> STD_INPUT, 1 -> STD_OUTPUT, 2 -> STD_ERROR.
    if (a2) {
        if (a2 <= 1) {
            if (setStd_) setStd_(kStdOutputHandle, a1); // 0xFFFFFFF5
        } else if (a2 == 2) {
            if (setStd_) setStd_(kStdErrorHandle, a1);  // 0xFFFFFFF4
        }
    } else {
        if (setStd_) setStd_(kStdInputHandle, a1);      // 0xFFFFFFF6
    }
    if (a2 >= count_) {
        // grow to a2+1 entries, zero-filling the [count_, a2) gap.
        table_.resize(static_cast<std::size_t>(a2) + 1, 0);
        count_ = a2 + 1;
        table_[static_cast<std::size_t>(a2)] = a1;
    } else {
        table_[static_cast<std::size_t>(a2)] = a1;
    }
    lockRelease();                         // off_64A944()
}

// gilde.exe 0x60447c — VIBE_HandleTable_ClearEntry(index@edx).
void HandleTable::ClearEntry(i32 v0) {
    lockAcquire();                         // off_64A940()
    if (v0 > 0 && v0 < count_)
        table_[static_cast<std::size_t>(v0)] = 0;
    lockRelease();                         // off_64A944()
}

// gilde.exe 0x6044b0 — VIBE_HandleTable_InitStdHandles.
i32 HandleTable::InitStdHandles() {
    i32 h;
    h = getStd_ ? getStd_(kStdInputHandle) : 0;        // GetStdHandle(STD_INPUT)
    if (!h || h == -1)
        h = createEvent_ ? createEvent_() : 0;
    AddEntry(h);

    h = getStd_ ? getStd_(kStdOutputHandle) : 0;       // GetStdHandle(STD_OUTPUT)
    if (!h || h == -1)
        h = createEvent_ ? createEvent_() : 0;
    AddEntry(h);

    h = getStd_ ? getStd_(kStdErrorHandle) : 0;        // GetStdHandle(STD_ERROR)
    if (!h || h == -1)
        h = createEvent_ ? createEvent_() : 0;
    return AddEntry(h);
}

// gilde.exe 0x604578 — VIBE_HandleTable_Cleanup.
void HandleTable::Cleanup() {
    if (!table_.empty()) {                 // if (dword_64ADBC)
        table_.clear();                    // VIBE_Memory_ReturnToFreeList; =0
        count_ = 0;
    }
    if (!owned_.empty()) {                 // if (dword_64ADC4)
        for (std::size_t i = 0; i < owned_.size(); ++i)
            if (close_) close_(owned_[i]); // CloseHandle(owned[i])
        owned_.clear();                    // VIBE_Memory_ReturnToFreeList; =0
    }
}

} // namespace guild::crt
