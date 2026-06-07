#pragma once
// Mission guild-member objective scan — VIBE_Mission_FindGuildMemberState
// (gilde.exe 0x539cbc). Walks the active person query (VIBE_Person_QueryBegin /
// VIBE_Person_IterNext) and, for every iterated person, reads its linked-person
// index (+0x27 word) and checks that linked person's kind byte (byte_12CE912 ==
// person record +0x02) is a guild-member state (6 or 7).
//
// The person query + the person-kind column are cross-cluster leaves owned by the
// sim Person module; this module codes against a tiny iterator interface so the
// guild-member rule stays faithful and unit-testable in isolation.
#include "guild/common/types.h"

namespace guild::world {

// Person-iteration leaf set (models VIBE_Person_QueryBegin / VIBE_Person_IterNext
// plus the byte_12CE912 kind-byte read). Returns the linked-person index stored at
// record+0x27 (0xFFFF == none) for each iterated person, and that index's kind.
struct MissionMemberQuery {
    // Begin the query for context `ctx` with filter byte `filter` (QueryBegin(ctx,
    // 1, 5, filter)). Returns true if there is a first person.
    bool (*begin)(void* self, i32 ctx, u8 filter) = nullptr;
    // Advance; returns true while a person remains.
    bool (*next)(void* self) = nullptr;
    // Linked-person index of the current person (record+0x27 word; 0xFFFF == none).
    u16  (*linkedIndex)(void* self) = nullptr;
    // Kind byte (byte_12CE912[536*index]) of the person at `index`.
    u8   (*kindOf)(void* self, u16 index) = nullptr;
    void* self = nullptr;
};

// gilde.exe 0x539cbc — VIBE_Mission_FindGuildMemberState  (al=filter, esi=ctx).
// Iterates the person query; for each person reads its linked index. Two `break`
// paths both fall through to `return -1`: a linked index of 0xFFFF (no link), and
// a linked person whose kind is not a guild-member state (not 6 and not 7). The
// scan returns the iterated count ONLY when the query iterator runs out
// (VIBE_Person_IterNext returns null) with every person passing the check. An
// empty query (no first person) returns 0.
i32 MissionFindGuildMemberState(const MissionMemberQuery& q, i32 ctx, u8 filter);

// The two guild-member kind bytes the scan accepts (record+0x02 values).
constexpr u8 kGuildMemberKindA = 6;
constexpr u8 kGuildMemberKindB = 7;

} // namespace guild::world
