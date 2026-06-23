#pragma once
// gilde.exe — guild::world  (MODULE: fixed-stride id lookup table)
//
// 1:1 reconstruction of VIBE_Table_FindEntrySlotById @0x4bad28.
//
// The original scans a global fixed array of 64 records (dword_11B73A8), stride 67
// dwords (268 bytes / 0x10C). Each record's FIRST dword is an id key. The routine
// linearly scans for the record whose key == `id`; on a match (or trivially the
// first slot) it CLEARS that key dword to 0 (freeing/claiming the slot) and returns
// the BYTE offset of the matched dword (slotDwordIndex * 4). If no slot matches, it
// returns the terminal byte offset (4288 dwords * 4 = 17152), i.e. one past the end.
//
// Quirk preserved verbatim (recovered from the disassembly): the first comparison is
// against slot 0 with the loop counter still 0, and the success path
// unconditionally zeroes dword[result] — so calling with `id == key[0]` clears slot
// 0 even though the search "found it immediately". This matches the binary exactly.
//
// Recovered constants:
//   stride  = 67 dwords (268 bytes, 0x10C)
//   limit   = 4288 dwords (loop terminates when the running dword index >= 4288)
//   entries = 4288 / 67 = 64
#include "guild/common/types.h"

namespace guild::world {

constexpr int kLut_StrideDwords = 67;   // add eax,10Ch  (== 67 dwords)
constexpr int kLut_LimitDwords  = 4288; // cmp eax,4300h  (== 4288 dwords)
constexpr int kLut_EntryCount   = kLut_LimitDwords / kLut_StrideDwords; // 64

// gilde.exe 0x4bad28 — VIBE_Table_FindEntrySlotById. `table` is the base of the
// dword array (dword_11B73A8). Returns the BYTE offset of the matched key dword
// (the original returns result*4 where result is the dword index). On a match the
// key dword is zeroed in place. Returns kLut_LimitDwords*4 (17152) when no slot
// matches.
guild::u32 TableFindEntrySlotById(guild::u32* table, guild::u32 id);

} // namespace guild::world
