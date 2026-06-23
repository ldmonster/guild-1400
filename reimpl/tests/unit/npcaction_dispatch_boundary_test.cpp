// Wave-12 hardening — boundary tests for the NpcAction dispatch jump table
// (VIBE_NpcAction_Dispatch @0x5766a0, npcaction.cpp). Focus: the action-type index
// drives a 69-entry table (indices 0..0x44); an out-of-range type must return the
// engine's -2 sentinel WITHOUT indexing the table, a null record returns -1, and
// the day<8 gate short-circuits to 0. NpcAction_TableEntry must clamp negative /
// over-range indices. Built under ASAN+UBSAN.
#include "test.h"

#include "sim/npcaction.h"
#include "sim/he.h"
#include "sim/types.h"

using namespace guild;
using namespace guild::sim;

namespace {
void SetType(HeRecord& h, u16 type) {
    *reinterpret_cast<u16*>(HeBytes(&h) + 4) = type;  // dispatch reads the +4 word
}
} // namespace

TEST(NpcDispatchBoundary, DayGateBelow8ReturnsZero) {
    GameTime t{}; t.day = 7;            // before day 8 the dispatcher no-ops -> 0
    SetNpcClock(t);
    HeRecord h{}; SetType(h, 0);
    CHECK_EQ(NpcAction_Dispatch(&h), 0);
}

TEST(NpcDispatchBoundary, NullRecordReturnsMinusOne) {
    GameTime t{}; t.day = 100;
    SetNpcClock(t);
    CHECK_EQ(NpcAction_Dispatch(nullptr), -1);
}

TEST(NpcDispatchBoundary, TypeAtCapacityIsOutOfRange) {
    GameTime t{}; t.day = 100;
    SetNpcClock(t);
    HeRecord h{};
    // 0x45 (== kNpcActionTableSize) is the first out-of-range type -> -2.
    SetType(h, 0x45);
    CHECK_EQ(NpcAction_Dispatch(&h), -2);
    SetType(h, 0xFFFF);                 // max word, well past the table -> -2
    CHECK_EQ(NpcAction_Dispatch(&h), -2);
}

TEST(NpcDispatchBoundary, LastInRangeTypeDispatches) {
    GameTime t{}; t.day = 100;
    SetNpcClock(t);
    HeRecord h{};
    SetType(h, 0x44);                   // last valid index (deferred -> no-op leaf 0)
    CHECK_EQ(NpcAction_Dispatch(&h), 0);
}

TEST(NpcDispatchBoundary, TableEntryClampsOutOfRange) {
    // Direct probe of the table accessor: negative and >= size must yield null
    // (no negative / past-the-end indexing of kNpcActionTableAddrs).
    CHECK_EQ(NpcAction_TableEntry(-1), static_cast<NpcActionStepFn>(nullptr));
    CHECK_EQ(NpcAction_TableEntry(-1000), static_cast<NpcActionStepFn>(nullptr));
    CHECK_EQ(NpcAction_TableEntry(kNpcActionTableSize),
             static_cast<NpcActionStepFn>(nullptr));
    CHECK_EQ(NpcAction_TableEntry(kNpcActionTableSize + 50),
             static_cast<NpcActionStepFn>(nullptr));
    // The one wired entry (0x2D == VIBE_DebugCmd_RetZero) resolves to a real fn.
    CHECK(NpcAction_TableEntry(0x2D) != nullptr);
}
