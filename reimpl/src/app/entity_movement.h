#pragma once
// gilde.exe — entity record sub-refcount step (guild::app).
//
// Faithful 1:1 reconstruction of:
//   0x412f18  VIBE_GameLogic_Movement  ("d2_SubRefcount")
//
// This is the per-frame "release a use of an entity record" helper. The engine's
// entity records live in a flat 84-byte-stride array (gilde.exe dword_62D204, the
// base). Each record has:
//   +0x30 (+48)  a "redirect-suppress" word  (when type is 5/8 and this is 0 the
//                refcount is applied to the LINKED record instead of this one)
//   +0x3C (+60)  a type/kind dword            (5 and 8 are the "linked pair" kinds)
//   +0x40 (+64)  a refcount dword             (decremented here)
//   +0x4C (+76)  a linked record INDEX        (the partner of a 5/8 pair)
//
// The function picks the record to decrement (this one, or — for a 5/8 record
// whose +48 suppress word is 0 — its +76 partner), then decrements that record's
// +64 refcount, reporting "d2_SubRefcount: invalid refcount!" if it was already
// <= 0 (an underflow guard that does NOT decrement).
//
// Reconstructed against a caller-supplied record base + an error callback so it is
// exercisable in isolation (the engine's dword_62D204 array and ErrorLog sink are
// owned elsewhere; passing them in avoids re-defining those globals here — ODR).
#include "guild/common/types.h"

#include <functional>

namespace guild::app {

// One entity record as the refcount step reads it (84-byte stride). Only the
// fields the step touches are named; the rest is opaque padding so the struct
// matches the original's 84-byte layout exactly.
#pragma pack(push, 1)
struct EntityMoveRecord {
    u8  pad0[48];      // +0x00
    i32 suppress;      // +0x30 (+48) redirect-suppress word
    u8  pad1[8];       // +0x34
    i32 type;          // +0x3C (+60) kind (5/8 = linked pair)
    i32 refcount;      // +0x40 (+64) use count (decremented here)
    u8  pad2[8];       // +0x44
    i32 linkIndex;     // +0x4C (+76) partner record index
    u8  pad3[4];       // +0x50
};
#pragma pack(pop)
static_assert(sizeof(EntityMoveRecord) == 84, "entity record must be 84 bytes");

// gilde.exe 0x412f18 — VIBE_GameLogic_Movement (d2_SubRefcount).
//   v1 = index;
//   type = records[index].type;
//   if ( type == 5 || type == 8 )
//     if ( !records[index].suppress )
//       v1 = records[index].linkIndex;     // redirect to the partner record
//   refcount = records[v1].refcount;
//   if ( refcount <= 0 )
//     ErrorLog_ReportMessage("d2_SubRefcount: invalid refcount!");
//   else
//     records[v1].refcount = refcount - 1;
//
// `records` is the record-array base (the original's dword_62D204); `index` is the
// record to release. `onUnderflow` (optional) stands in for the engine's
// VIBE_ErrorLog_ReportMessage leaf — it is called with the exact original message
// text when the chosen record's refcount is already <= 0.
void EntitySubRefcount(EntityMoveRecord* records, int index,
                       const std::function<void(const char*)>& onUnderflow = {});

} // namespace guild::app
