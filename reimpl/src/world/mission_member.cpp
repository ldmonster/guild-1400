#include "world/mission_member.h"

// Faithful port of VIBE_Mission_FindGuildMemberState (gilde.exe 0x539cbc). The
// person iteration + the byte_12CE912 kind-byte read are cross-cluster leaves
// (the sim Person module); they are injected via MissionMemberQuery so the
// guild-member rule is reproduced 1:1 and testable in isolation.

namespace guild::world {

// gilde.exe 0x539cbc — VIBE_Mission_FindGuildMemberState.
i32 MissionFindGuildMemberState(const MissionMemberQuery& q, i32 ctx, u8 filter) {
    i32 count = 0;                                  // v3 = 0
    if (!q.begin(q.self, ctx, filter))              // Begin = Person_QueryBegin(...)
        return count;                               // !Begin -> return 0
    while (true) {
        u16 linked = q.linkedIndex(q.self);         // v5 = *(WORD*)(Begin + 39)
        if (linked == 0xFFFF)                        // v5 == 0xFFFF -> break
            break;
        u8 kind = q.kindOf(q.self, linked);          // v6 = byte_12CE912[536 * v5]
        if (kind != kGuildMemberKindA && kind != kGuildMemberKindB)
            break;                                  // not a guild-member state -> break
        bool more = q.next(q.self);                  // Begin = Person_IterNext()
        ++count;                                     // v3 = v8 + 1
        if (!more)                                   // !Begin -> return v3
            return count;
    }
    return -1;                                       // both breaks fall through here
}

} // namespace guild::world
