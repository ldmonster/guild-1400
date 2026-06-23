// Golden-vector tests for the Command sync-range barrier markers.
//   VIBE_Command_MarkSyncRangeStart @0x493a1c
//   VIBE_Command_MarkSyncRangeEnd   @0x493a28
// Plus a round-trip against the already-reconstructed pure ACK scan
//   VIBE_Command_CheckSyncRangeAcked @0x493a34  (command_apply10.h)
#include "test.h"
#include "sim/command_recon_syncrange.h"
#include "sim/command_apply10.h"   // CheckSyncRangeAcked — reused, not redefined

#include <vector>

using namespace guild;
using guild::sim::SyncRangeState;

// --- MarkSyncRangeStart: start = sendCount + 1, returns same ----------------
TEST(CommandReconSyncRange, StartSetsStartToSendCountPlusOne) {
    SyncRangeState s;
    s.start = 0xDEAD;  // pre-existing garbage must be overwritten
    s.end   = 0xBEEF;  // must be left untouched
    u32 ret = sim::MarkSyncRangeStart(s, 41);
    CHECK_EQ(s.start, 42u);
    CHECK_EQ(ret, 42u);
    CHECK_EQ(s.end, 0xBEEFu);   // MarkSyncRangeStart never touches end
}

// --- MarkSyncRangeEnd: end = sendCount + 1, returns same --------------------
TEST(CommandReconSyncRange, EndSetsEndToSendCountPlusOne) {
    SyncRangeState s;
    s.start = 0x1234;  // must be left untouched
    s.end   = 0x5678;
    u32 ret = sim::MarkSyncRangeEnd(s, 99);
    CHECK_EQ(s.end, 100u);
    CHECK_EQ(ret, 100u);
    CHECK_EQ(s.start, 0x1234u);  // MarkSyncRangeEnd never touches start
}

// --- Zero send count ---------------------------------------------------------
TEST(CommandReconSyncRange, ZeroSendCountGivesOne) {
    SyncRangeState s;
    CHECK_EQ(sim::MarkSyncRangeStart(s, 0), 1u);
    CHECK_EQ(s.start, 1u);
    CHECK_EQ(sim::MarkSyncRangeEnd(s, 0), 1u);
    CHECK_EQ(s.end, 1u);
}

// --- 32-bit wraparound is preserved (sendCount == 0xFFFFFFFF -> 0) -----------
TEST(CommandReconSyncRange, WrapsAtU32Max) {
    SyncRangeState s;
    u32 r1 = sim::MarkSyncRangeStart(s, 0xFFFFFFFFu);
    CHECK_EQ(r1, 0u);
    CHECK_EQ(s.start, 0u);
    u32 r2 = sim::MarkSyncRangeEnd(s, 0xFFFFFFFFu);
    CHECK_EQ(r2, 0u);
    CHECK_EQ(s.end, 0u);
}

// --- Typical bracket: Start then End with the same send count ----------------
// The game calls Start, enqueues N commands (bumping the send counter), then
// End. Modelled here by a sendCount that advances between the two calls.
TEST(CommandReconSyncRange, BracketAdvancingSendCounter) {
    SyncRangeState s;
    sim::MarkSyncRangeStart(s, 10);  // start = 11
    // ... 3 commands enqueued, counter now 13 ...
    sim::MarkSyncRangeEnd(s, 13);    // end = 14
    CHECK_EQ(s.start, 11u);
    CHECK_EQ(s.end, 14u);
    // Range spans seqs [11, 14): three ack slots, exactly the three enqueued.
    CHECK_EQ(s.end - s.start, 3u);
}

// --- Round-trip with CheckSyncRangeAcked: empty range is trivially acked -----
// CheckSyncRangeAcked returns 1 when start == end (nothing pending).
TEST(CommandReconSyncRange, EmptyRangeIsAcked) {
    SyncRangeState s;
    sim::MarkSyncRangeStart(s, 7);  // start = 8
    sim::MarkSyncRangeEnd(s, 7);    // end   = 8
    std::vector<u8> ack(10 * 0x8000, 0);  // all pending; irrelevant for empty
    i32 r = sim::CheckSyncRangeAcked(s.start, s.end, ack.data());
    CHECK_EQ(r, 1);
}

// --- Round-trip: a fully-pending range is NOT acked (returns 0) --------------
TEST(CommandReconSyncRange, PendingRangeNotAcked) {
    SyncRangeState s;
    sim::MarkSyncRangeStart(s, 4);  // start = 5
    sim::MarkSyncRangeEnd(s, 7);    // end   = 8  -> seqs 5,6,7
    std::vector<u8> ack(10 * 0x8000, 0);  // status 0 == still pending
    i32 r = sim::CheckSyncRangeAcked(s.start, s.end, ack.data());
    CHECK_EQ(r, 0);   // first slot pending -> break -> 0
}

// --- Round-trip: a fully-acked range (all status 1) reports done -------------
TEST(CommandReconSyncRange, AckedRangeReportsDone) {
    SyncRangeState s;
    sim::MarkSyncRangeStart(s, 4);  // start = 5
    sim::MarkSyncRangeEnd(s, 7);    // end   = 8  -> seqs 5,6,7
    std::vector<u8> ack(10 * 0x8000, 0);
    for (u32 seq = s.start; seq < s.end; ++seq) ack[10 * (seq & 0x7FFF)] = 1;  // acked
    i32 r = sim::CheckSyncRangeAcked(s.start, s.end, ack.data());
    CHECK_EQ(r, 1);
}

// --- Round-trip: a NAK (status 2) in an otherwise-acked range yields -1 ------
TEST(CommandReconSyncRange, NakInRangeReportsMinusOne) {
    SyncRangeState s;
    sim::MarkSyncRangeStart(s, 4);  // start = 5
    sim::MarkSyncRangeEnd(s, 7);    // end   = 8  -> seqs 5,6,7
    std::vector<u8> ack(10 * 0x8000, 0);
    ack[10 * (5 & 0x7FFF)] = 1;  // acked
    ack[10 * (6 & 0x7FFF)] = 2;  // NAK
    ack[10 * (7 & 0x7FFF)] = 1;  // acked
    i32 r = sim::CheckSyncRangeAcked(s.start, s.end, ack.data());
    CHECK_EQ(r, -1);
}
