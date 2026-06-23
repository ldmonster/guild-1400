#pragma once
// Event / mission descriptor table — the shared 24-byte descriptor records that
// both the mission system and the crime/Straftat-type system index by id and by
// category (gilde.exe dword_63CD48 @0x63CD48, stride 24, count dword_5383F0).
//
// Recovered 1:1 from the accessors that walk the table:
//   VIBE_GesetzTable_CountByType   0x538550  (count entries of a category)
//   VIBE_Mission_FindByType        0x5385b0  (id->record / by category)
//   VIBE_Mission_PickRandomByType  0x538680  (LCG-random pick of a category)
//
// All three read the category from the dword at (record+2) shifted right 24,
// i.e. the byte at record+5, and PickRandomByType returns the byte at record+4.
// We model the table as a flat array of EventDesc and reproduce that exact
// byte-addressing so the lookups stay faithful.
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Descriptor record  (gilde.exe dword_63CD48, stride 24 bytes)
// ===========================================================================
// Field map recovered from the accessors and from the static table image
// (get_bytes @0x63CD48). The category accessor reads *(int*)(rec+2) >> 24, i.e.
// the high byte of the dword spanning rec[2..5] == the byte at +5. The random
// picker returns byte_63CD4C[24*i], i.e. the byte at +4. The remaining dwords
// are descriptor parameters (subtype, amounts, a -1-terminated extra slot).
GUILD_PACKED_BEGIN
struct EventDesc {
    i32 word0;     // +0x00  leading dword (id/source ordinal; FindBySource matches
                   //         the slot owner, not this field)
    u8  value;     // +0x04  PickRandomByType return (byte_63CD4C[24*i]); subtype id
    u8  category;  // +0x05  category byte the accessors read as *(int*)(rec+2)>>24.
                   //         In the shipped static image this is 0xFF for the free
                   //         sentinel (row 0) and the 0..5 book grouping for the
                   //         populated rows (4x0,8x1,8x2,8x3,12x4,7x5) — the value
                   //         the picker/counter match. (The 0x17 that recurs at +9
                   //         is paramA's low byte, not the category.)
    u8  pad6[2];   // +0x06  remainder of the +4 dword
    i32 paramA;    // +0x08  descriptor parameter A
    i32 paramB;    // +0x0C  descriptor parameter B
    i32 paramC;    // +0x10  descriptor parameter C (often a money amount)
    i32 paramD;    // +0x14  descriptor parameter D (-1 == none)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(EventDesc) == 24, "EventDesc stride must be 24 bytes");
static_assert(offsetof(EventDesc, value)    == 4, "value @+4");
static_assert(offsetof(EventDesc, category) == 5, "category @+5");

constexpr int kEventTableCapacity = 64;   // backing capacity (image holds 48)
constexpr int kEventMaxCategory   = 5;     // CountByType rejects category > 5

// Flat descriptor table + active count (mirrors dword_63CD48 / dword_5383F0).
extern EventDesc g_eventTable[kEventTableCapacity];
extern i32       g_eventTableCount;

// LCG state used by the random picker (gilde.exe dword_122F49C). Distinct from
// the CRT generator; advanced as state = 1103515245*state + 12345.
extern u32 g_missionLcgState;

// Clears the table to all-free (category 0xFF) and zeroes the count + LCG state.
void EventTableReset();

// Loads the recovered static descriptor image (the 48-entry default table from
// gilde.exe @0x63CD48). Sets g_eventTableCount to 48.
void EventTableLoadDefault();

// gilde.exe 0x538550 — VIBE_GesetzTable_CountByType  (al=category, edx=unused).
// Returns the number of active entries whose category byte (+5) == `category`,
// or -1 when category > 5.
int EventTableCountByCategory(u8 category);

// gilde.exe 0x5385b0 — VIBE_Mission_FindByType  (al=category, edx=cursor).
// With cursor==0: returns the first record (by pointer into the byte image, here
// a table index) whose category == `category`, or -1 if none. The original's
// pointer-cursor variant is reduced to the id-based form the game actually uses.
// Returns the table index of the first matching active entry, or -1.
int EventTableFindByCategory(u8 category);

// gilde.exe 0x538680 — VIBE_Mission_PickRandomByType  (al=category, edx=unused).
// Counts entries of `category`; if none, returns -1. Otherwise advances the LCG
//   g_missionLcgState = 1103515245*state + 12345
// and picks pick = (HIWORD(state) % 0x7FFF) % count, then walks the table
// counting category matches until the (pick+1)-th, returning that entry's +4
// value byte. Returns -1 if the walk falls off the end.
int EventPickRandomByCategory(u8 category);

} // namespace guild::world
