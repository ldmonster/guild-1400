#include "test.h"

#include "sim/command_apply11.h"
#include "sim/command_codec.h"
#include "sim/command.h"

#include <cstdlib>
#include <cstring>

// E2E: a full debug-console session. A player types several cheat commands; each
// command_apply11 emitter stages packets onto ONE shared CommandQueue + PendingState.
// We then drain the queue in standalone mode (FlushSendQueue applies locally onto
// the received list) and dispatch via the real ExecCommands with an opcode-counting
// handler — proving the whole build -> enqueue -> flush -> exec lockstep flow works
// end to end across the (A) and (B) families together.

using namespace guild;
using namespace guild::sim;

namespace {

i32 E2eParse(const char* s) { return static_cast<i32>(std::atoi(s)); }

int g_dispatched[96];
void CountHandler(CommandQueue&, CommandPacket& pkt, AckEntry*) {
    u8 op = pkt.opcode();
    if (op < 96) ++g_dispatched[op];
}

// Two-person selection table shared by the scan emitters.
int g_active[768];
i32 g_ids[768];
int SelActive(int i) { return g_active[i]; }
i32 SelId(int i)     { return g_ids[i]; }

} // namespace

TEST(CmdApply11E2E, FullDebugConsoleSession) {
    std::memset(g_dispatched, 0, sizeof(g_dispatched));
    std::memset(g_active, 0, sizeof(g_active));
    std::memset(g_ids, 0, sizeof(g_ids));
    g_active[0] = 1; g_ids[0] = 0x1001;
    g_active[3] = 1; g_ids[3] = 0x1002;

    CommandQueue q; q.Init();
    q.set_standalone(true);                 // dword_764CE0 == -1: apply locally
    PendingState pending;
    DeltaWriter dw;

    DebugCmdCtx ctx;
    ctx.playerId = 0x42;
    for (int i = 0; i < 14; ++i) ctx.gameTime[i] = static_cast<u8>(i);
    ctx.selectionActive = &SelActive;
    ctx.selectedId      = &SelId;

    Apply11CmdHooks h{};
    h.parseInt = E2eParse;
    h.toLower  = [](char*) {};
    h.countActiveObjects = []() -> i16 { return 2; };
    SetApply11CmdHooks(h);

    // 1) "give gold"   -> opcode 28
    CHECK_EQ(QueueGiveGold(q, pending, ctx), 1);
    // The first pending block occupies the staging slot; reset so the next opcode-28
    // emitter can stage its own body (the live game flushes between commands).
    int flushed = q.FlushSendQueue();
    CHECK_EQ(flushed, 0);
    pending.Reset();

    // 2) "adjust reputation"  -> opcode 28
    CHECK_EQ(QueueAdjustReputation(q, pending, ctx), 1);
    CHECK_EQ(q.FlushSendQueue(), 0);
    pending.Reset();

    // 3) "spawn guard"  -> opcode 28
    CHECK_EQ(QueueSpawnGuard(q, pending, ctx), 1);
    CHECK_EQ(q.FlushSendQueue(), 0);
    pending.Reset();

    // 4) "adjust selected stat PLUS_50"  -> opcode 16
    CHECK_EQ(QueueAdjustSelectedStat(q, ctx, /*selBase*/0x90, /*a2*/0, "-PLUS_50"), 1);
    // 5) "move persons +10"  -> opcode 27 x2 (two active selections)
    CHECK_EQ(QueueMovePersonsToCoord(q, ctx, 0x90, 0, "-PLUS_10"), 1);
    // 6) "reveal all"  -> opcode 17 x2
    char arg[] = "-XXXX";
    CHECK_EQ(QueueRevealAllPersons(q, ctx, arg), 1);

    // Drain the remaining packets locally.
    CHECK(q.pending_head() != nullptr);
    CHECK_EQ(q.FlushSendQueue(), 0);
    CHECK(q.pending_head() == nullptr);     // fully drained
    CHECK(q.received_head() != nullptr);    // delivered to the received list

    // Dispatch the received list through the real ExecCommands.
    q.set_handler(16, &CountHandler);
    q.set_handler(17, &CountHandler);
    q.set_handler(27, &CountHandler);
    q.set_handler(28, &CountHandler);
    q.ExecCommands();

    // Opcode tally: three opcode-28, one opcode-16, two opcode-27, two opcode-17.
    CHECK_EQ(g_dispatched[28], 3);
    CHECK_EQ(g_dispatched[16], 1);
    CHECK_EQ(g_dispatched[27], 2);
    CHECK_EQ(g_dispatched[17], 2);

    SetApply11CmdHooks(Apply11CmdHooks{});
}

// A rejected-command session: gates fail, nothing is queued, the flow stays inert.
TEST(CmdApply11E2E, RejectedCommandsQueueNothing) {
    CommandQueue q; q.Init();
    q.set_standalone(true);
    PendingState pending;
    DeltaWriter dw;
    DebugCmdCtx ctx;

    Apply11CmdHooks h{}; h.parseInt = E2eParse; SetApply11CmdHooks(h);

    CHECK_EQ(QueueSpawnSelected(q, pending, ctx, "noflag"), 0);     // missing '-'
    CHECK_EQ(QueueAdjustSelectedStat(q, ctx, 0, 0, "-PLUS_1"), 0);  // !selBase
    CHECK_EQ(QueueRevealAllPersons(q, ctx, "-AB"), 0);              // tail too short
    CHECK_EQ(QueueMovePersonsToCoord(q, ctx, 0x1, 0, "-FOO_1"), 0); // bad sign

    CHECK_EQ(q.send_count(), 0u);
    CHECK(q.pending_head() == nullptr);
    SetApply11CmdHooks(Apply11CmdHooks{});
}
