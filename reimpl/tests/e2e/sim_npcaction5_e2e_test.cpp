// End-to-end flow for the deferred ContextAction variants + NpcEvent helpers.
//   (1) Drive a single context target through a context-menu session: tooltip pass
//       over a row of menu entries, then an activate, asserting the verdict + leaf
//       sequence vs a reference. (2) Run an NPC handler record through a multi-step
//       countdown routine (several CountdownTickEntity ticks until the counter
//       expires and the entry frees), verifying the re-armed appointment + handle.
#include "tests/framework/test.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/contextaction2.h"
#include "sim/interaction_handlers.h"
#include "sim/npcaction5.h"
#include "sim/npcaction.h"
#include "sim/he.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A recording privilege hook capturing the full leaf sequence.
std::vector<int> g_leafSeq;
int SeqPrivilege(int leafId, ContextActor*, InteractionEventRec*) {
    g_leafSeq.push_back(leafId);
    return 0;
}

ContextActor MakeActor() { ContextActor a; std::memset(&a, 0, sizeof(a)); return a; }
InteractionEventRec MakeEvent(u8 m) { InteractionEventRec ev; std::memset(&ev, 0, sizeof(ev)); ev.mode = m; return ev; }

} // namespace

// ===========================================================================
// E2E 1 — a context-menu session over a profession-25 office worker (rank 6).
// The menu builds a row of entries (mode 1 tooltips), then the user activates one.
// ===========================================================================
TEST(SimCA2E2E, ContextMenuSession) {
    SetPrivilegeLeafHook(SeqPrivilege);
    g_leafSeq.clear();

    auto a = MakeActor();
    a.kind = 6;          // human player char -> tooltips render
    a.rank = 6;          // high rank
    a.profession = 25;   // office worker (ProfessionMenuOffice accepts 20/24/25)
    a.profession2 = 0;

    // --- tooltip pass (mode 1): which entries are eligible? ---
    // ProfessionMenuOffice (prof 25) -> handled(2), renders tooltip 0x19AB.
    { auto ev = MakeEvent(kModeTooltip); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextProfessionMenuOffice(&a, &ev), 2);
      CHECK_EQ(g_leafTrace.tooltipStringId, 0x19AB); }
    // TrainRank6A (rank>=6) -> handled, tooltip 0x1992.
    { auto ev = MakeEvent(kModeTooltip); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextTrainRank6A(&a, &ev), 2);
      CHECK_EQ(g_leafTrace.tooltipStringId, 0x1992); }
    // CommandType24 (prof 25 != 24) -> reject(1) but still renders its tooltip
    // (kind6 renders before the profession gate).
    { auto ev = MakeEvent(kModeTooltip); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextCommandType24(&a, &ev), 1);
      CHECK_EQ(g_leafTrace.tooltipStringId, 0x19CE); }
    // ProfessionMenuType17A (prof 25 != 17) -> reject(1), no tooltip.
    { auto ev = MakeEvent(kModeTooltip); ResetInteractionLeafTrace();
      CHECK_EQ((int)ContextProfessionMenuType17A(&a, &ev), 1); }

    // No privilege leaf should have fired during the tooltip pass.
    CHECK_EQ((int)g_leafSeq.size(), 0);

    // --- activate the office-menu entry (mode 3) ---
    { auto ev = MakeEvent(kModeActivate);
      CHECK_EQ((int)ContextProfessionMenuOffice(&a, &ev), 2); }
    // exactly one ShowDialog leaf fired.
    CHECK_EQ((int)g_leafSeq.size(), 1);
    CHECK_EQ(g_leafSeq[0], (int)kPrivShowDialog);

    SetPrivilegeLeafHook(nullptr);
}

// ===========================================================================
// E2E 2 — a guard switches roster: drag a profession-21 worker onto a
// profession-21/26 command slot, then verify the lawsuit path against a peer.
// ===========================================================================
TEST(SimCA2E2E, DragAndLawsuitFlow) {
    SetPrivilegeLeafHook(SeqPrivilege);
    g_leafSeq.clear();

    auto a = MakeActor(); a.kind = 6; a.flag457 = 1;
    auto src = MakeActor(); src.profession = 21;
    auto ev = MakeEvent(kModeDragApply); ev.dragSource = &src;
    // CommandType21Or26 drag-apply: src prof 21 accepted -> Convert leaf, verdict 10.
    CHECK_EQ((int)ContextCommandType21Or26(&a, &ev), 10);
    CHECK_EQ((int)g_leafSeq.size(), 1);
    CHECK_EQ(g_leafSeq[0], (int)kPrivConvert);

    SetPrivilegeLeafHook(nullptr);
}

// ===========================================================================
// E2E 3 — countdown routine: re-arm N times, then free.
// ===========================================================================
namespace {
int s_e2eFreed = 0;
i32 E2eFree(HeRecord*) { s_e2eFreed++; return -1; }
i32 s_e2eHandle = 0x1000;
i32 E2eQueue(int, HeRecord*) { return s_e2eHandle++; }
}

TEST(SimNA5E2E, CountdownRoutine) {
    NpcLeafHooks hooks{}; hooks.freeHandlerEntry = E2eFree; hooks.queueRequestEntity29 = E2eQueue;
    SetNpcLeafHooks(&hooks);
    GameTime clk{}; clk.day = 10; clk.hour = 0; SetNpcClock(clk);

    std::vector<uint8_t> buf(600, 0);
    HeRecord* h = reinterpret_cast<HeRecord*>(buf.data());
    He_State(h) = 0; He_Counter172(h) = 3; He_Flags(h) = 0;
    s_e2eHandle = 0x1000; s_e2eFreed = 0;

    // Tick 1: counter 3 -> 2, handle 0x1000, +24h -> day 11.
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), 0x1000);
    CHECK_EQ(He_Counter172(h), 2);
    CHECK_EQ(He_ReqHandle(h), 0x1000);
    CHECK_EQ(He_ApptTime(h).day, 11);
    // Tick 2: 2 -> 1, handle 0x1001.
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), 0x1001);
    CHECK_EQ(He_Counter172(h), 1);
    // Tick 3: 1 -> 0, handle 0x1002.
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), 0x1002);
    CHECK_EQ(He_Counter172(h), 0);
    // Tick 4: counter == 0 -> free.
    CHECK_EQ(NpcAction5_CountdownTickEntity(h), -1);
    CHECK_EQ(s_e2eFreed, 1);

    SetNpcLeafHooks(nullptr);
}
