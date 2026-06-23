#pragma once
// gilde.exe 0x5384d0 — VIBE_GesetzTable_FindByKey  (__usercall, al = key, eax = ret)
//
// The "Gesetz table" name is the auto-generated placeholder; this routine is in
// fact the find-by-VALUE sibling of the shared 24-byte descriptor table that the
// mission / event / crime-type systems index. That table is already reconstructed
// in src/world/event.{h,cpp} as:
//
//     EventDesc g_eventTable[kEventTableCapacity];   // gilde.exe dword_63CD48 @0x63CD48
//     i32       g_eventTableCount;                    // gilde.exe dword_5383F0  @0x5383F0
//
// The original walks `byte_63CD4C` (== dword_63CD48 + 4, i.e. the EventDesc.value
// field at record offset +0x04) with a stride of 24 bytes, comparing each entry's
// value byte against the key `al`, and returns a pointer to the matching value byte
// (or NULL). We reproduce that exact behavior over the existing g_eventTable: there
// is NO new table here — only the missing accessor.
//
//   sibling accessors already present (do not redefine):
//     0x538550 VIBE_GesetzTable_CountByType  -> EventTableCountByCategory  (matches +5)
//     0x5385b0 VIBE_Mission_FindByType       -> EventTableFindByCategory   (matches +5)
//     0x538680 VIBE_Mission_PickRandomByType -> EventPickRandomByCategory  (returns +4)
//
// FindByKey is distinct: it matches the +4 VALUE byte (not the +5 category byte)
// and returns a pointer to that byte inside the live record.

#include "guild/common/types.h"

namespace guild::world {

struct EventDesc;  // defined in event.h (24-byte descriptor record)

// gilde.exe 0x5384d0 — VIBE_GesetzTable_FindByKey.
//
//   char *__usercall FindByKey@<eax>(char a1@<al>)
//   {
//     if ( dword_5383F0 <= 0 ) return 0;
//     v1 = 0;
//     while ( byte_63CD4C[v1] != a1 )
//     {
//       v1 += 24;
//       if ( v1 >= 24 * dword_5383F0 ) return 0;
//     }
//     return &byte_63CD4C[v1];
//   }
//
// Returns a pointer to the matching record's `value` byte (+0x04), or nullptr when
// the table is empty or no entry's value byte equals `key`. `key` is taken as the
// low 8 bits of the al register operand, matching the original char compare.
u8* GesetzTableFindByKey(u8 key);

// Convenience: returns the table index of the matching record, or -1. This is the
// index form `(returned_ptr - &g_eventTable[0].value) / 24` and is provided for
// callers/tests that prefer an index over a raw pointer; it does not exist in the
// binary but is a lossless reformulation of the same scan.
int GesetzTableFindIndexByKey(u8 key);

} // namespace guild::world
