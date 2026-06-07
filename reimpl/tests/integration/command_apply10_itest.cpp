// Integration: drive command_apply10's CheckTargetNotInUse target-validation
// predicate against a REAL reconstructed sibling — VIBE_Person_FindRecordById
// (entity.cpp 0x58bc6c), the linear scan over the real g_persons[] / g_personIds[]
// arrays. This is exactly the live wiring: the Check* predicates resolve person
// ids through VIBE_Person_FindRecordById, which command_apply10 reaches via its
// installable `personFindRecordById` hook. Here we forward that hook straight into
// the genuine entity::PersonFindRecordById, populate the REAL person table, and let
// CheckTargetNotInUse's "is this person already bound to a different object?" gate
// run end to end against the real lookup (state byte +2, bound dword +520).
//
// We additionally pin CheckSyncRangeAcked, the module's fully-golden hook-free ACK
// scan, as a deterministic oracle (no sibling needed there — it is pure).
#include "test.h"

#include "sim/command_apply10.h"
#include "sim/entity.h"      // REAL PersonFindRecordById, g_persons, g_personIds

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Forward command_apply10's personFindRecordById hook into the REAL sibling.
void* RealPersonFind(i32 id) { return PersonFindRecordById(id); }

// Clear the real person table to all-free.
void ClearPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;          // free slot
        g_personIds[i] = 0;
    }
}

// Seat a person in slot `i` with id, kind (+2), and bound-object dword (+520).
void SeatPerson(int i, i32 id, u8 kind, i32 boundAt520) {
    g_persons[i].marker = 0;               // occupied
    g_persons[i].id     = id;              // record +4 (not the lookup key)
    g_personIds[i]      = id;              // parallel column the lookup keys on
    auto* base = reinterpret_cast<u8*>(&g_persons[i]);
    base[2] = kind;                        // state/kind byte
    std::memcpy(base + 520, &boundAt520, 4);
}

// A command record big enough for CheckTargetNotInUse's reads (cmd+16 owner key,
// cmd+20..+20+4*15 person ids).
struct CmdBuf {
    u8 bytes[128];
    CmdBuf() { std::memset(bytes, 0, sizeof bytes); }
    void put32(int off, i32 v) { std::memcpy(bytes + off, &v, 4); }
};

} // namespace

// CheckTargetNotInUse: with the REAL person lookup, a person in state 6/7 bound to
// a DIFFERENT object than the command's own object id (cmd+16) rejects (returns 1).
TEST(CommandApply10Itest, NotInUseRejectsForeignBoundPersonViaRealLookup) {
    ClearPersons();
    ApplyTargetHooks h{};
    h.personFindRecordById = RealPersonFind;     // <-- real sibling wiring
    SetApplyTargetHooks(&h);

    // The command's own object id (cmd+16) is 0x100; one person id at cmd+20.
    CmdBuf cmd;
    cmd.put32(16, 0x100);          // owner key
    cmd.put32(20, 0x777);          // person id slot 0

    // Seat person 0x777, state 7, bound to a DIFFERENT object (0x200) -> conflict.
    SeatPerson(5, 0x777, 7, 0x200);

    int r = CheckTargetNotInUse(cmd.bytes);
    CHECK_EQ(r, 1);                // conflict -> in-use

    // Re-bind the same person to the command's own object (0x100): no conflict.
    SeatPerson(5, 0x777, 7, 0x100);
    int r2 = CheckTargetNotInUse(cmd.bytes);
    CHECK_EQ(r2, 0);               // bound to us -> not a conflict

    // A person not in state 6/7 is ignored even when bound elsewhere.
    SeatPerson(5, 0x777, 3, 0x200);
    int r3 = CheckTargetNotInUse(cmd.bytes);
    CHECK_EQ(r3, 0);

    // An unbound person (+520 == -1) never conflicts.
    SeatPerson(5, 0x777, 6, -1);
    int r4 = CheckTargetNotInUse(cmd.bytes);
    CHECK_EQ(r4, 0);

    SetApplyTargetHooks(nullptr);
}

// When the REAL lookup finds NO record for the listed person id, the gate cannot
// flag a conflict and returns 0 (not in use).
TEST(CommandApply10Itest, NotInUseClearWhenRealLookupMisses) {
    ClearPersons();
    ApplyTargetHooks h{};
    h.personFindRecordById = RealPersonFind;
    SetApplyTargetHooks(&h);

    CmdBuf cmd;
    cmd.put32(16, 0x100);
    cmd.put32(20, 0x9999);         // id absent from the real table

    int r = CheckTargetNotInUse(cmd.bytes);
    CHECK_EQ(r, 0);

    // First id == -1 short-circuits to "not in use" (returns 1 per the original).
    CmdBuf cmd2;
    cmd2.put32(16, -1);
    int r2 = CheckTargetNotInUse(cmd2.bytes);
    CHECK_EQ(r2, 1);

    SetApplyTargetHooks(nullptr);
}

// CheckSyncRangeAcked is the module's pure, hook-free ACK scan (no sibling): pin
// its golden cases as a deterministic oracle.
TEST(CommandApply10Itest, SyncRangeAckedPureScan) {
    // status byte lives at ackTable[10 * (seq & 0x7FFF)].
    u8 table[10 * 8];
    std::memset(table, 0, sizeof table);
    // mark seq 0..4 as acked (status 1).
    for (u32 s = 0; s < 5; ++s) table[10 * s] = 1;

    // empty range -> 1.
    CHECK_EQ(CheckSyncRangeAcked(3, 3, table), 1);
    // start > end -> 1.
    CHECK_EQ(CheckSyncRangeAcked(4, 2, table), 1);
    // [0,5) all status 1 -> fully acked (1).
    CHECK_EQ(CheckSyncRangeAcked(0, 5, table), 1);
    // a NAK (status 2) at seq 2 -> result -1 (negatively acked).
    table[10 * 2] = 2;
    CHECK_EQ(CheckSyncRangeAcked(0, 5, table), -1);
    // a pending (status 0) at seq 3 -> 0 (still pending), short-circuits before end.
    table[10 * 3] = 0;
    CHECK_EQ(CheckSyncRangeAcked(0, 5, table), 0);
}
