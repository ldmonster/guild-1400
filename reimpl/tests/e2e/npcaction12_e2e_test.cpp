#include "test.h"

// E2E: a single NPC walks a multi-step daily flow across npcaction12 functions:
//   InitWalkState (arm the 06:15 walk window)
//   -> BeginGotoHomeStep (re-home at 05:00, +1 day-hour)
//   -> BeginScanType63 (no conflict -> keep going, +1s)
//   -> InitDualCoordWalk (arm the +48-day secondary appointment)
//   -> QueueRandomActions (LCG-driven idle bursts)
//   -> a final DemolishBuildingStep state machine run-out (state 2 -> 3 -> free).
// All clock stamping flows from the shared NpcClock(); the LCG from g_lcgState.
#include "sim/npcaction12.h"
#include "sim/npcaction.h"
#include "sim/he.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
struct Rec { u8 b[600]; };
HeRecord* AsHe(Rec& r) { return reinterpret_cast<HeRecord*>(r.b); }
void SetClock(int day, int hour, int minute, int second) {
    GameTime t{}; t.day = day; t.hour = (u16)hour; t.minute = minute; t.second = second;
    SetNpcClock(t);
}
const GameTime* At(Rec& r, int off) { return reinterpret_cast<GameTime*>(r.b + off); }

struct E2ESink {
    int frees = 0;
    int slotResets = 0;
    int single59 = 0;
} g_e;
i32 EFree(HeRecord*) { g_e.frees++; return 0; }
void ESlot(void*, int) { g_e.slotResets++; }
void ESingle59(i32) { g_e.single59++; }
} // namespace

TEST(NpcAction12E2E, DailyWalkFlow) {
    g_e = E2ESink{};
    SetClock(20, 9, 0, 0);

    NpcAction12Hooks h{};
    h.freeHandlerEntry = &EFree;
    h.requestSlotReset28 = &ESlot;
    h.scanFilterHasForeignMatch = [](HeRecord*, int) { return false; };
    h.findConflictingHandler = [](HeRecord*, int, i32) -> void* { return nullptr; };
    h.requestSingle59 = &ESingle59;
    h.personQueryBegin = [](i32) -> void* { return nullptr; };  // no worker -> demolish state2 no-ops cleanly
    SetNpcAction12Hooks(&h);

    Rec r{};

    // 1) InitWalkState -> appt 06:15 on day 20.
    NpcAction12_InitWalkState(AsHe(r));
    CHECK_EQ((int)At(r, 82)->hour, 6);
    CHECK_EQ(At(r, 82)->minute, 15);
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 176), 3);

    // 2) BeginGotoHomeStep -> appt hour 6 (5+1), +96 carries 05:00.
    NpcAction12_BeginGotoHomeStep(AsHe(r));
    CHECK_EQ((int)At(r, 96)->hour, 5);
    CHECK_EQ((int)At(r, 82)->hour, 6);

    // 3) BeginScanType63 (no conflict) -> +1s, state unchanged.
    *reinterpret_cast<i32*>(r.b + 112) = 0;
    NpcAction12_BeginScanType63(AsHe(r));
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 112), 0);
    CHECK_EQ(At(r, 82)->second, 1);

    // 4) InitDualCoordWalk -> +204 advances 48 day-hours (+2d +8h from 09:00 day20).
    SetClock(20, 9, 0, 0);
    NpcAction12_InitDualCoordWalk(AsHe(r));
    CHECK_EQ(At(r, 204)->day, 22);
    CHECK_EQ((int)At(r, 204)->hour, 9);
    CHECK_EQ(At(r, 82)->second, 1);

    // 5) QueueRandomActions: seed the LCG and run a draw.
    g_lcgState = 1;
    i32 count = NpcAction12_QueueRandomActions(5, 0);
    CHECK_EQ(count, 2);                 // oracle: seed1 -> 2
    CHECK_EQ(g_e.slotResets, 2);

    // 6) DemolishBuildingStep: state 2 (worker absent -> no recall, no advance,
    //    no ++state), then drive state 3 (worker absent -> just free).
    *reinterpret_cast<i32*>(r.b + 112) = 0;   // state 0 -> state+2 == 2
    NpcAction12_DemolishBuildingStep(AsHe(r));
    // worker null in state 2 -> state NOT advanced, no free.
    CHECK_EQ(*reinterpret_cast<i32*>(r.b + 112), 0);
    CHECK_EQ(g_e.frees, 0);

    *reinterpret_cast<i32*>(r.b + 112) = 1;   // state 1 -> state+2 == 3
    NpcAction12_DemolishBuildingStep(AsHe(r));
    CHECK_EQ(g_e.frees, 1);                    // state 3 always frees

    SetNpcAction12Hooks(nullptr);
}

// E2E: the negative-state "free" fast-paths are consistent across the step family.
TEST(NpcAction12E2E, NegativeStateFreeAcrossSteps) {
    g_e = E2ESink{};
    NpcAction12Hooks h{};
    h.freeHandlerEntry = &EFree;
    SetNpcAction12Hooks(&h);

    Rec r{};
    *reinterpret_cast<i32*>(r.b + 112) = -2;
    NpcAction12_AssignWorkPlaceStep(AsHe(r));   // free
    NpcAction12_NotifyTrainingStep(AsHe(r));    // free
    NpcAction12_DemolishBuildingStep(AsHe(r));  // state+2==0 -> free
    CHECK_EQ(g_e.frees, 3);

    SetNpcAction12Hooks(nullptr);
}
