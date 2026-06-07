#pragma once
// History / Chronicle — the chronicle "cart" group-slot TABLE and its lifecycle.
//
// The chronicle commandline parser (VIBE_History_ParseCommandline*) addresses a
// table of 4 GROUPS (gilde.exe dword_122DAE0 / dword_122DADC), each 68 bytes
// (17 dwords) wide:
//
//   group[g]:  +0x00  active flag (dword, 1 == group in use)
//              +0x04  slot 0 id   (dword, -1 == free)   +0x08 slot 0 kind (byte)
//              +0x0C  slot 1 id   (dword)               +0x10 slot 1 kind (byte)
//              ... 8 slots of {id@+4 step 8 (dword), kind@+8 step 8 (byte)} ...
//
// dword_122DADC is dword_122DAE0 minus one dword, so the same byte address that
// holds an id in DADC's frame holds the kind low-byte in DAE0's frame — the
// original aliases both onto one 4-group x 17-dword array. This is the same table
// the save-game writer (src/io/save_tables.h "cart" records, stride 68) persists.
//
// This module recovers, byte-for-byte:
//   * VIBE_History_ResetGroupSlot      0x4fd140 — reset one group (flag=1, slots free)
//   * VIBE_History_ResetChronicleState 0x4fd090 — reset ALL 4 groups (the table-init
//                                                  portion; file (re)load is engine)
//   * VIBE_History_FreeChronicleFiles  0x4fd194 — reset ALL 4 groups (the table-init
//                                                  portion; file free is engine)
//   * VIBE_History_ParseCommandlineFirstPass 0x4fd6ac — the group-index lookup +
//                                                  (optional) per-group reset, the
//                                                  recoverable core of the 1st pass.
//
// The label string table (dword_8C36B0), VIBE_Util_ParseInt, the "_SET"/"_USE"
// prefix keys and the .esc dispatch are engine leaves; here we recover the table
// shape, the reset rules and the group-index gate.
#include <cstdint>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// The chronicle cart group-slot table.
// ===========================================================================
constexpr int kCartGroupCount = 4;   // dword_122DAE0 has 4 groups (loop i<4)
constexpr int kCartSlotCount  = 8;   // 8 slots per group (the do/while v3 8..32)
constexpr int kCartGroupDwords = 17; // 68-byte stride == 17 dwords

// One {id, kind} slot. Free slots are id == -1, kind == 0xFF (the reset writes
// -1 to the dword and 0xFF to the low byte).
struct CartSlot {
    std::int32_t id;   // +0x04+8k  (-1 == free)
    std::uint8_t kind; // +0x08+8k  (0xFF == free)  (only the low byte is written)
};

// One group: an active flag plus 8 slots. Laid out to mirror the 68-byte stride.
struct CartGroup {
    std::int32_t active;             // +0x00  (1 == in use after a reset)
    CartSlot     slots[kCartSlotCount];
};

// The whole table (4 groups). A plain value type so callers/tests own an instance
// without touching the engine's static array.
struct CartTable {
    CartGroup groups[kCartGroupCount];
};

// ===========================================================================
// VIBE_History_ResetGroupSlot 0x4fd140 (__usercall, eax = (a1@ax)).
// ===========================================================================
// Resets ONE group: active flag -> 1, every slot id -> -1 and kind low byte ->
// 0xFF. The original gates on a1 < 4 (returns 0 == false for an out-of-range
// group, 1 == true on success). `group` selects which group to reset.
bool HistoryResetGroupSlot(CartTable& table, unsigned group);

// ===========================================================================
// VIBE_History_ResetChronicleState 0x4fd090  /
// VIBE_History_FreeChronicleFiles  0x4fd194 (table-reset portion).
// ===========================================================================
// Both functions begin with the same 4-group init loop: for each of the 4 groups,
// set the active flag to 1 and free the FIRST 4 slots (id -1, kind 0xFF). NOTE the
// loop bound differs from HistoryResetGroupSlot: the original's byte counter runs
// v3 = 8,16,24,32 (4 iterations), so only slots 0..3 are cleared here, whereas
// ResetGroupSlot clears all 8. This asymmetry is faithful to the binary. The
// remainder of each original (text-file (re)load / free) is engine I/O, left out.
constexpr int kResetAllGroupsSlots = 4; // ChronicleState/FreeFiles clear 4 slots/grp
void HistoryResetAllGroups(CartTable& table);

// ===========================================================================
// VIBE_History_ParseCommandlineFirstPass 0x4fd6ac (group-index core).
// ===========================================================================
// The recoverable decision of the 1st pass: a label carries a leading keyword
// ("_SET" or "_USE") followed by a group digit. The pass parses the digit, gates
// it on < 4, and — for a "_SET" label (resetFlag set) — resets that group before
// returning a handle to it. We model the parsed inputs directly:
//   `groupDigit` : the integer the original gets from VIBE_Util_ParseInt
//   `isSet`      : true for a "_SET" label (the original's v18 == 1 reset flag),
//                  false for a "_USE" label
// Returns the index of the addressed group (0..3) on success, or -1 when the
// group digit is out of range (the original returns 0 == null pointer). On a
// "_SET" label the addressed group is reset (HistoryResetGroupSlot) first.
int HistoryParseCommandlineGroupIndex(CartTable& table, int groupDigit, bool isSet);

} // namespace guild::world
