// Wave-12 hardening — boundary / malformed-input tests for the ContextAction2
// command-executor variants (contextaction2.cpp). Focus: a context action invoked
// with a bad object (null drag-source in a drag mode), an out-of-range / unknown
// `mode` byte, and the address-keyed lookup miss must all return cleanly without
// dereferencing a bad pointer. Built under ASAN+UBSAN.
#include "test.h"

#include "sim/contextaction2.h"
#include "sim/interaction_handlers.h"

using namespace guild;
using namespace guild::sim;

namespace {
ContextActor MakeActor(u8 kind, u8 rank, u8 prof, u8 prof2) {
    ContextActor a{};
    a.kind = kind; a.rank = rank; a.profession = prof; a.profession2 = prof2;
    return a;
}
} // namespace

// --- Bad object: drag mode with a NULL drag-source -> reject (1), no deref ---
TEST(ContextAction2Boundary, FileLawsuitDragNullSource) {
    ContextActor actor = MakeActor(/*kind*/6, /*rank*/0, /*prof*/5, 0);
    actor.flag457 = 1;                 // drag enabled
    InteractionEventRec ev{};
    ev.mode = 4;                       // drag-activate
    ev.dragSource = nullptr;           // BAD object: no source record
    CHECK_EQ((int)ContextFileLawsuitType1(&actor, &ev), 1);
    CHECK_EQ((int)ContextFileLawsuitType2(&actor, &ev), 1);
}

TEST(ContextAction2Boundary, CommandByProfessionDragNullSource) {
    ContextActor actor = MakeActor(6, 0, 24, 0);
    actor.flag457 = 1;
    InteractionEventRec ev{};
    ev.mode = 4;
    ev.dragSource = nullptr;           // BAD object
    CHECK_EQ((int)ContextCommandType24(&actor, &ev), 1);
    CHECK_EQ((int)ContextCommandType24Drag(&actor, &ev), 1);
    CHECK_EQ((int)ContextCommandType26(&actor, &ev), 1);
}

// --- Unknown / out-of-range mode bytes -> reject (1) ------------------------
TEST(ContextAction2Boundary, UnknownModeRejected) {
    ContextActor actor = MakeActor(6, 6, 24, 30);
    for (u8 m : {static_cast<u8>(0), static_cast<u8>(5), static_cast<u8>(200)}) {
        InteractionEventRec ev{};
        ev.mode = m;                   // neither 1/3 (activate) nor 2/4 (drag)
        CHECK_EQ((int)ContextTrainRank4B(&actor, &ev), 1);
        CHECK_EQ((int)ContextProfessionMenu28(&actor, &ev), 1);
        CHECK_EQ((int)ContextProfessionMenuOffice(&actor, &ev), 1);
        CHECK_EQ((int)ContextCommandType24(&actor, &ev), 1);
        CHECK_EQ((int)ContextFileLawsuitType1(&actor, &ev), 1);
    }
}

// --- Profession-menu gate: a profession not in the accept set -> reject ------
TEST(ContextAction2Boundary, ProfessionMenuNonMatching) {
    ContextActor actor = MakeActor(6, 0, /*prof*/99, /*prof2*/99);
    InteractionEventRec ev{};
    ev.mode = 3;                       // activate
    CHECK_EQ((int)ContextProfessionMenu28(&actor, &ev), 1);     // wants submethod 28
    CHECK_EQ((int)ContextProfessionMenuOffice(&actor, &ev), 1); // wants 20/24/25
    CHECK_EQ((int)ContextProfessionMenuRange30(&actor, &ev), 1);// wants 30..33
}

// --- Rank-train tiers: rank below the `>=N` threshold must reject (1) --------
// Regression for the overRank==0 sentinel bug: a rank-0 actor must NOT be
// treated as the "exact over" tier. The >=N variants (5B/6A/6B) reject rank 0
// via `a1[13] < N -> return 1` (gilde.exe 0x56f727/0x56f873/0x56f917). Only
// TrainRank4Plus has a real exact-over (rank == 6 -> 4 @0x56f5da).
TEST(ContextAction2Boundary, RankTrainBelowThresholdRejected) {
    ContextActor actor = MakeActor(/*kind*/6, /*rank*/0, /*prof*/5, 0);
    InteractionEventRec ev{};
    ev.mode = 3;                       // activate
    CHECK_EQ((int)ContextTrainRank5B(&actor, &ev), 1);  // rank 0 < 5
    CHECK_EQ((int)ContextTrainRank6A(&actor, &ev), 1);  // rank 0 < 6
    CHECK_EQ((int)ContextTrainRank6B(&actor, &ev), 1);  // rank 0 < 6
    // TrainRank4Plus: rank 0 < 4 also rejects (only rank == 6 yields 4).
    CHECK_EQ((int)ContextTrainRank4Plus(&actor, &ev), 1);
}

// rank == 6 hits the exact-over only for 4Plus; the >=N variants accept it (2).
TEST(ContextAction2Boundary, RankTrainExactOverIsolated) {
    ContextActor actor = MakeActor(/*kind*/6, /*rank*/6, /*prof*/5, 0);
    InteractionEventRec ev{};
    ev.mode = 3;                                            // activate, kind 6
    CHECK_EQ((int)ContextTrainRank4Plus(&actor, &ev), 4);  // rank == 6 -> 4
    CHECK_EQ((int)ContextTrainRank5B(&actor, &ev), 2);     // rank 6 >= 5 -> dialog/2
    CHECK_EQ((int)ContextTrainRank6A(&actor, &ev), 2);     // rank 6 >= 6 -> dialog/2
}

// --- Address-keyed lookup: a bad address yields null, valid yields the fn ----
TEST(ContextAction2Boundary, LookupMissAndHit) {
    CHECK_EQ((int)RegisterContextActions(), 23);
    CHECK_EQ(ContextAction2_Lookup(/*bogus addr*/0xDEADBEEF),
             static_cast<ContextAction2Fn>(nullptr));
    CHECK(ContextAction2_Lookup(0x56f514) == &ContextTrainRank4B);
    CHECK(ContextAction2_Lookup(0x571134) == &ContextCommandType26);
}
