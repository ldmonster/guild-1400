// tests/integration/wire_npc_actions_itest.cpp — INTEGRATION: step a small world
// of NPCs over a few ticks through the REAL daily-routine director with the
// bridge installed vs. not. Asserts the live NPC state (the +456 turn-bits column
// of g_persons + the built command packets) EVOLVES across ticks ONLY with the
// real dispatch installed, and is byte-deterministic across reruns.
#include "test.h"

#include "play/wire_npc_actions.h"
#include "sim/npc_daily.h"
#include "sim/npcaction.h"
#include "sim/entity.h"
#include "sim/he.h"
#include "sim/types.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

template <typename T>
void PutCol(int i, int off, T v) {
    std::memcpy(reinterpret_cast<u8*>(&sim::g_persons[i]) + off, &v, sizeof(T));
}
template <typename T>
T GetCol(int i, int off) {
    T v; std::memcpy(&v, reinterpret_cast<u8*>(&sim::g_persons[i]) + off, sizeof(T));
    return v;
}

constexpr int kNpc = 4;   // a small world of 4 NPC workers

// Build a small world: kNpc production workers in slots 0..kNpc-1 (each with a
// distinct home/dest building), rest free. One production building per worker.
void SetupSmallWorld() {
    sim::ResetEntityArrays();
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        PutCol<i16>(i, 0x00, -1);   // free
    for (int n = 0; n < kNpc; ++n) {
        std::memset(&sim::g_persons[n], 0, sizeof(sim::Person));
        PutCol<i16>(n, 0x00, 0);                // live marker
        PutCol<u8>(n, 357, 1);                  // activeB
        PutCol<i32>(n, 364, 1000 + n);          // homeBld id
        PutCol<i32>(n, 388, 50 + n);            // destBld id
        PutCol<u32>(n, 456, 0u);                // turnBits
        PutCol<i32>(n, 4, 700 + n);             // person id
        sim::g_objects[n].alive = 11;           // production building
        sim::g_objects[n].id    = 1000 + n;
    }
}

// Deterministic provider keyed off the person index (no RNG, no scene state).
int PvFind(int i, i32* u, i32* o) { *u = 100 + i; *o = 200 + i; return 1; }
bool PvDoor(int, i32* a, i32* b) { *a = 1; *b = 1; return true; }
NpcActionsTargetProvider MakeProvider() {
    NpcActionsTargetProvider p{};
    p.findTarget  = PvFind;
    p.destDoorIds = PvDoor;
    return p;
}

// Run a few ticks of the morning director at a fixed clock and collect the live
// turn-bits column + the total commands built. Each call is one director step.
struct RunResult {
    std::vector<u32> turnBits;   // per-NPC +456 after the run
    int totalCommands = 0;
};
RunResult RunTicks(bool install) {
    SetupSmallWorld();
    NpcActionsTargetProvider p = MakeProvider();
    SetNpcActionsTargetProvider(&p);
    ResetNpcActionsQueue();
    ResetNpcActionsTallies();
    crt::Srand(2024);                       // pin RNG (tavern roll path)
    sim::SetNpcClock(sim::GameTime{ 0, 6, 0, 0 });   // morning, hour 6

    if (install) InstallRealNpcActions(); else sim::SetNpcDailyHooks(nullptr);

    // 3 ticks of the morning sweep (state stays 0 while hour < workStart=8).
    for (int t = 0; t < 3; ++t) {
        sim::HeRecord rec{};
        sim::He_State(&rec) = 0;
        sim::NpcDaily_DailyRoutineStep(&rec);
    }

    RunResult r;
    for (int n = 0; n < kNpc; ++n)
        r.turnBits.push_back(GetCol<u32>(n, 456));
    r.totalCommands = (int)NpcActionsQueue().send_count();

    if (install) UninstallRealNpcActions();
    SetNpcActionsTargetProvider(nullptr);
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// NPC state evolves ONLY with the real dispatch installed.
// ---------------------------------------------------------------------------
TEST(WireNpcActionsItest, StateEvolvesOnlyWithRealDispatch) {
    RunResult inert = RunTicks(/*install=*/false);
    RunResult real  = RunTicks(/*install=*/true);

    // Inert: no NPC's turn-bits moved, no command built.
    for (int n = 0; n < kNpc; ++n)
        CHECK_EQ((int)inert.turnBits[n], 0);
    CHECK_EQ(inert.totalCommands, 0);

    // Real: every worker got dispatched-to-work, real packets were built.
    for (int n = 0; n < kNpc; ++n)
        CHECK((real.turnBits[n] & sim::kDailyDispWork) != 0);
    CHECK(real.totalCommands > 0);
    // Each production worker emits op77+chrmove+str47 = 3 commands the FIRST tick,
    // then the dispatch bit suppresses the named branch but the move path still
    // fires each tick; so commands >= 3 * kNpc.
    CHECK(real.totalCommands >= 3 * kNpc);
}

// ---------------------------------------------------------------------------
// Determinism: two identical real runs produce byte-identical NPC state.
// ---------------------------------------------------------------------------
TEST(WireNpcActionsItest, RealRunDeterministic) {
    RunResult a = RunTicks(/*install=*/true);
    RunResult b = RunTicks(/*install=*/true);
    CHECK_EQ((int)a.turnBits.size(), (int)b.turnBits.size());
    for (size_t i = 0; i < a.turnBits.size(); ++i)
        CHECK_EQ((int)a.turnBits[i], (int)b.turnBits[i]);
    CHECK_EQ(a.totalCommands, b.totalCommands);
}
